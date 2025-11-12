// fat.c
// Implements fatInit(), fatOpen(), fatRead() for FAT12/FAT16
// Uses ata_lba_read(lba, buffer, nsectors) provided in ide.s / ide.h

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "fat.h"
#include "ide.h"   // declares ata_lba_read

/* Globals the assignment expects (simple driver state) */
static struct boot_sector bootsector_buf __attribute__((aligned(4)));
static unsigned char *fat_table = NULL;
static unsigned int fat_sectors = 0;
static unsigned int reserved_sectors = 0;
static unsigned int num_fats = 0;
static unsigned int root_dir_entries = 0;
static unsigned int root_dir_sectors = 0;
static unsigned int root_dir_start_sector = 0;
static unsigned int sectors_per_fat = 0;
static unsigned int bytes_per_sector = SECTOR_SIZE;
static unsigned int sectors_per_cluster = 0;
static unsigned int first_data_sector = 0;
static unsigned int total_sectors = 0;
static int fat_type = 16; /* 12 or 16; default 16 */

/* Helper: read sectors from disk into buffer (wrap ata_lba_read) */
static int read_sectors(unsigned int start_sector, unsigned char *buf, unsigned int nsectors) {
    int r = ata_lba_read(start_sector, buf, nsectors);
    if (r < 0) return -1;
    return 0;
}

/* Helper: determine FAT type (12/16) based on total clusters */
static void determine_fat_type(void) {
    unsigned int data_sectors = total_sectors - first_data_sector;
    unsigned int total_clusters = data_sectors / bootsector_buf.num_sectors_per_cluster;
    if (total_clusters < 4085) fat_type = 12;
    else if (total_clusters < 65525) fat_type = 16;
    else fat_type = 32; /* not supported here */
}

/* Helper: compute root_dir_sectors and first_data_sector */
static void compute_layout(void) {
    reserved_sectors = bootsector_buf.num_reserved_sectors;
    num_fats = bootsector_buf.num_fat_tables;
    root_dir_entries = bootsector_buf.num_root_dir_entries;
    sectors_per_fat = bootsector_buf.num_sectors_per_fat;
    sectors_per_cluster = bootsector_buf.num_sectors_per_cluster;
    /* total_sectors field selection */
    if (bootsector_buf.total_sectors != 0) total_sectors = bootsector_buf.total_sectors;
    else total_sectors = bootsector_buf.total_sectors_in_fs;

    /* Root dir size in sectors */
    root_dir_sectors = ((root_dir_entries * 32) + (bytes_per_sector - 1)) / bytes_per_sector;

    /* Root directory start (reserved + fats) */
    root_dir_start_sector = reserved_sectors + (num_fats * sectors_per_fat);

    /* First data sector */
    first_data_sector = root_dir_start_sector + root_dir_sectors;
}

/* Helper: Get FAT entry for given cluster (works for FAT12 and FAT16) */
static uint32_t get_fat_entry(uint32_t cluster) {
    if (!fat_table) return 0xFFFFFFFF;

    if (fat_type == 16) {
        uint16_t *ft = (uint16_t*)fat_table;
        return (uint32_t)ft[cluster];
    } else if (fat_type == 12) {
        /* FAT12: 12 bits per entry (packed). Compute byte offset */
        uint32_t offset = (cluster * 3) / 2;
        uint16_t value = fat_table[offset] | (fat_table[offset + 1] << 8);
        if (cluster & 1) {
            value = (value >> 4) & 0x0FFF;
        } else {
            value = value & 0x0FFF;
        }
        return (uint32_t)value;
    } else {
        /* FAT32 unsupported in this implementation */
        return 0xFFFFFFFF;
    }
}

/* Convert a standard filename into 8.3 padded uppercase array for comparison.
   buf must be at least 11 bytes. input name can be "FOO.TXT" or "FOO" */
static void make_8dot3(const char *input, char out11[11]) {
    /* Fill with spaces */
    for (int i = 0; i < 11; ++i) out11[i] = ' ';

    /* Copy name and extension */
    const char *dot = strchr(input, '.');
    int nname = (dot ? (int)(dot - input) : (int)strlen(input));
    if (nname > 8) nname = 8;
    int i;
    for (i = 0; i < nname; ++i) out11[i] = toupper((unsigned char)input[i]);

    if (dot) {
        const char *ext = dot + 1;
        int iext = 0;
        while (iext < 3 && ext[iext]) {
            out11[8 + iext] = toupper((unsigned char)ext[iext]);
            ++iext;
        }
    }
}

/*-------------------- Public API required by assignment --------------------*/

/*
 * fatInit:
 *  - read the boot sector
 *  - validate signature / basic checks
 *  - read the first FAT into memory (only first FAT required for reading chain)
 *  - compute root_dir_start and first_data_sector
 * Returns 0 on success, -1 on failure.
 */
int fatInit(void) {
    unsigned char sector[SECTOR_SIZE];
    if (read_sectors(0, sector, 1) < 0) return -1;

    /* copy into typed struct */
    memcpy(&bootsector_buf, sector, sizeof(struct boot_sector));

    /* basic validation: boot signature 0xAA55 (stored little-endian) */
    if (bootsector_buf.boot_signature != 0xAA55) {
        /* still continue but warn */
        /* In kernel you might print; here we return error */
        return -1;
    }

    /* compute layout */
    compute_layout();

    /* set global vars we might need */
    bytes_per_sector = SECTOR_SIZE;
    fat_sectors = sectors_per_fat;

    /* figure out total sectors (already set in compute_layout) and fat_type */
    determine_fat_type();
    if (fat_type == 32) {
        /* FAT32 not supported in this implementation */
        return -1;
    }

    /* Allocate and read first FAT (we only need one copy to walk chains) */
    size_t fat_bytes = (size_t)sectors_per_fat * bytes_per_sector;
    fat_table = (unsigned char*)malloc(fat_bytes);
    if (!fat_table) return -1;
    if (read_sectors(reserved_sectors, fat_table, sectors_per_fat) < 0) {
        free(fat_table);
        fat_table = NULL;
        return -1;
    }

    return 0;
}

/*
 * fatOpen:
 *  - open a file (only files in rootdir supported by assignment)
 *  - returns pointer to dynamically allocated struct file (caller must free when done)
 *  - returns NULL on not found / error
 *
 *  filename expected in normal string form e.g. "TEST.TXT" or "README"
 */
struct file *fatOpen(const char *filename) {
    if (!filename || !fat_table) return NULL;

    /* prepare 8.3 for comparison */
    char want11[11];
    make_8dot3(filename, want11);

    /* read root directory region (root_dir_sectors long) into buffer */
    size_t rbsz = root_dir_sectors * SECTOR_SIZE;
    unsigned char *rdbuf = (unsigned char*)malloc(rbsz);
    if (!rdbuf) return NULL;

    if (read_sectors(root_dir_start_sector, rdbuf, root_dir_sectors) < 0) {
        free(rdbuf);
        return NULL;
    }

    /* iterate over entries: each entry is 32 bytes */
    int entries = (int)root_dir_entries;
    for (int i = 0; i < entries; ++i) {
        unsigned char *entry = rdbuf + (i * 32);
        /* first byte 0x00 => no more entries; 0xE5 => deleted */
        unsigned char first = entry[0];
        if (first == 0x00) break;
        if (first == 0xE5) continue;

        /* skip volume labels and directories if attribute indicates */
        uint8_t attr = entry[11];
        if (attr & 0x08) continue; /* volume label */

        /* compare name+ext (11 bytes) */
        if (memcmp(entry, want11, 11) == 0) {
            /* found */
            struct file *f = (struct file*)malloc(sizeof(struct file));
            if (!f) { free(rdbuf); return NULL; }
            memset(f, 0, sizeof(*f));
            /* copy RDE into file->rde */
            memcpy(&f->rde, entry, sizeof(struct root_directory_entry));
            /* cluster field in RDE is 16-bit for FAT12/16 */
            f->start_cluster = f->rde.cluster;
            free(rdbuf);
            return f;
        }
    }

    free(rdbuf);
    return NULL;
}

/*
 * fatRead:
 *  - read 'count' bytes from file into buffer 'buf' starting at file offset 'offset'
 *  - returns number of bytes actually read (0..count) or -1 on error
 *
 * Note: This implementation follows cluster chains using the loaded FAT and reads
 * clusters using ata_lba_read.
 */
int fatRead(struct file *f, void *buf, uint32_t count, uint32_t offset) {
    if (!f || !buf || !fat_table) return -1;

    uint32_t file_size = f->rde.file_size;
    if (offset >= file_size) return 0; /* nothing to read */

    /* clamp count to remaining bytes */
    if (offset + count > file_size) count = file_size - offset;

    uint8_t *out = (uint8_t*)buf;
    uint32_t bytes_per_cluster = (uint32_t)sectors_per_cluster * bytes_per_sector;

    /* find cluster containing 'offset' */
    uint32_t cluster = f->start_cluster;
    if (cluster < 2) return -1; /* invalid start */

    uint32_t cluster_index = offset / bytes_per_cluster;
    uint32_t cluster_offset = offset % bytes_per_cluster;

    /* walk cluster_index times */
    for (uint32_t i = 0; i < cluster_index; ++i) {
        uint32_t next = get_fat_entry(cluster);
        if (next >= 0xFFF8 && fat_type == 16) { /* EOF for FAT16 */
            return 0; /* offset beyond EOF */
        }
        if (fat_type == 12 && next >= 0xFF8) {
            return 0;
        }
        if (next == 0x0000 || next == 0xFFFFFFFF) return -1;
        cluster = next;
    }

    uint32_t bytes_read = 0;
    while (bytes_read < count) {
        /* compute first sector of this cluster */
        uint32_t first_sector_of_cluster = first_data_sector + (cluster - 2) * sectors_per_cluster;

        /* read entire cluster into a temporary buffer (or directly read needed sectors) */
        unsigned char *cluster_buf = (unsigned char*)malloc(bytes_per_cluster);
        if (!cluster_buf) return -1;
        if (read_sectors(first_sector_of_cluster, cluster_buf, sectors_per_cluster) < 0) {
            free(cluster_buf);
            return -1;
        }

        /* how many bytes we can copy from this cluster starting at cluster_offset */
        uint32_t to_copy = bytes_per_cluster - cluster_offset;
        if (to_copy > (count - bytes_read)) to_copy = (count - bytes_read);

        memcpy(out + bytes_read, cluster_buf + cluster_offset, to_copy);
        bytes_read += to_copy;
        free(cluster_buf);

        /* move to next cluster */
        if (bytes_read >= count) break;
        cluster_offset = 0; /* subsequent clusters start at 0 */
        uint32_t next = get_fat_entry(cluster);
        if (fat_type == 16) {
            if (next >= 0xFFF8) break; /* EOF */
        } else if (fat_type == 12) {
            if (next >= 0xFF8) break;
        } else {
            return -1;
        }
        if (next == 0x0000 || next == 0xFFFFFFFF) return -1;
        cluster = next;
    }

    return (int)bytes_read;
}

/* Optional helper to free resources (not required but helpful for tests) */
void fatShutdown(void) {
    if (fat_table) {
        free(fat_table);
        fat_table = NULL;
    }
}
