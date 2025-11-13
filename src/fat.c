
/*
 * fat.c - heapless FAT12/FAT16 reader for a freestanding kernel
 *
 * Provides:
 *   int fatInit(void);
 *   struct file *fatOpen(const char *filename);
 *   int fatRead(struct file *f, void *buf, uint32_t count, uint32_t offset);
 *
 * Uses ata_lba_read(lba, buffer, nsectors) from ide.s (declared in ide.h).
 *
 * No dynamic memory; no libc calls. Conservative static buffers.
 */

#include "fat.h"
#include "ide.h"   /* provides ata_lba_read(unsigned int lba, unsigned char *buf, unsigned int nsectors) */
#include <stdint.h>

/* If SECTOR_SIZE wasn't defined in fat.h, define it here */
#ifndef SECTOR_SIZE
#define SECTOR_SIZE 512
#endif

/* Configuration limits (adjust if your image is larger) */
#define ROOTDIR_MAX_SECTORS 64    /* 64 * 512 = 32768 bytes for root dir */
#define FAT_SECTOR_CACHE_COUNT 2  /* we may need to peek into adjacent FAT sector for FAT12 */

/* Static driver state (no malloc) */
static struct boot_sector bs __attribute__((aligned(4)));
static unsigned int reserved_sectors = 0;
static unsigned int num_fats = 0;
static unsigned int sectors_per_fat = 0;
static unsigned int root_dir_entries = 0;
static unsigned int root_dir_sectors = 0;
static unsigned int root_dir_start = 0;
static unsigned int first_data_sector = 0;
static unsigned int sectors_per_cluster = 0;
static unsigned int total_sectors = 0;
static int fat_type = 16; /* 12 or 16; 32 unsupported */

/* Static buffers */
static unsigned char rootdir_buf[ROOTDIR_MAX_SECTORS * SECTOR_SIZE] __attribute__((aligned(4)));
static unsigned int rootdir_sectors_read = 0;

/* FAT sector cache (we will cache up to 2 consecutive FAT sectors for FAT12 reads) */
static unsigned char fat_cache[FAT_SECTOR_CACHE_COUNT * SECTOR_SIZE] __attribute__((aligned(4)));
static unsigned int fat_cache_base_sector = (unsigned int)-1; /* invalid */

/* --------------------------------------------------
   Minimal helpers (no libc)
   -------------------------------------------------- */

/* simple memory copy */
static void kmemcpy(void *dst, const void *src, unsigned int n) {
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
}

/* simple memcmp */
static int kmemcmp(const void *a, const void *b, unsigned int n) {
    const unsigned char *x = (const unsigned char*)a;
    const unsigned char *y = (const unsigned char*)b;
    for (unsigned int i = 0; i < n; ++i) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

/* toupper for ASCII letters */
static int ktoupper(int c) {
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

/* Simple strlen */
static unsigned int kstrlen(const char *s) {
    unsigned int n = 0;
    while (s[n]) ++n;
    return n;
}

/* --------------------------------------------------
   Disk helpers
   -------------------------------------------------- */

/* Read sectors using provided SD/IDE driver */
static int read_sectors(unsigned int lba, unsigned char *buf, unsigned int nsectors) {
    /* ata_lba_read returns 0 on success (as ide.s coded) or -1 on failure */
    return ata_lba_read(lba, buf, nsectors);
}

/* Read a single FAT sector into fat_cache at index 0 (and optionally index 1 for neighbor) */
static int cache_fat_sector(unsigned int fat_sector_index_abs) {
    unsigned int fat_lba = reserved_sectors + fat_sector_index_abs; /* FAT table starts at reserved_sectors */
    /* read the requested sector into cache[0] */
    if (read_sectors(fat_lba, fat_cache, 1) < 0) return -1;
    /* try to read the next sector into cache[SECTOR_SIZE] (for cross-boundary reads) */
    if (read_sectors(fat_lba + 1, fat_cache + SECTOR_SIZE, 1) < 0) {
        /* it's ok if next sector read fails (maybe last sector) - zero it */
        for (unsigned int i = 0; i < SECTOR_SIZE; ++i) fat_cache[SECTOR_SIZE + i] = 0;
    }
    fat_cache_base_sector = fat_sector_index_abs;
    return 0;
}

/* Access FAT entry for cluster (supports FAT12 and FAT16) */
static uint32_t get_fat_entry(uint32_t cluster) {
    if (fat_type == 16) {
        /* 2 bytes per entry */
        uint32_t byte_offset = cluster * 2;
        unsigned int sector_index = byte_offset / SECTOR_SIZE;
        unsigned int in_sector = byte_offset % SECTOR_SIZE;

        if (sector_index != fat_cache_base_sector) {
            if (cache_fat_sector(sector_index) < 0) return 0xFFFFFFFF;
        }
        /* little endian */
        uint16_t lo = fat_cache[in_sector];
        uint16_t hi;
        if (in_sector + 1 < SECTOR_SIZE) hi = fat_cache[in_sector + 1];
        else hi = fat_cache[SECTOR_SIZE]; /* from cached next sector */
        return (uint32_t)(lo | (hi << 8));
    } else if (fat_type == 12) {
        /* 12 bits per entry (packed). Each pair of entries occupy 3 bytes */
        uint32_t byte_offset = (cluster * 3) / 2;
        unsigned int sector_index = byte_offset / SECTOR_SIZE;
        unsigned int in_sector = byte_offset % SECTOR_SIZE;

        if (sector_index != fat_cache_base_sector) {
            if (cache_fat_sector(sector_index) < 0) return 0xFFFFFFFF;
        }
        /* we may need up to two bytes across boundary; we cached next sector into fat_cache + SECTOR_SIZE */
        uint16_t w = fat_cache[in_sector] | (fat_cache[in_sector + 1] << 8);
        if (cluster & 1) {
            /* odd cluster: high 12 bits */
            return (uint32_t)((w >> 4) & 0x0FFF);
        } else {
            /* even cluster: low 12 bits */
            return (uint32_t)(w & 0x0FFF);
        }
    } else {
        return 0xFFFFFFFF;
    }
}

/* --------------------------------------------------
   Name helper: convert input string into 11-byte 8.3 uppercase padded with spaces
   -------------------------------------------------- */
static void make_8dot3(const char *input, char out11[11]) {
    /* fill with spaces */
    for (int i = 0; i < 11; ++i) out11[i] = ' ';

    /* find dot (if any) */
    unsigned int len = kstrlen(input);
    unsigned int dotpos = len; /* sentinel => no dot */
    for (unsigned int i = 0; i < len; ++i) {
        if (input[i] == '.') { dotpos = i; break; }
    }

    unsigned int name_len = (dotpos < len ? dotpos : len);
    if (name_len > 8) name_len = 8;
    for (unsigned int i = 0; i < name_len; ++i) {
        out11[i] = (char)ktoupper((int)input[i]);
    }

    if (dotpos < len) {
        /* copy up to 3 extension chars */
        unsigned int ext_len = len - dotpos - 1;
        if (ext_len > 3) ext_len = 3;
        for (unsigned int j = 0; j < ext_len; ++j) {
            out11[8 + j] = (char)ktoupper((int)input[dotpos + 1 + j]);
        }
    }
}

/* --------------------------------------------------
   Public API: fatInit, fatOpen, fatRead
   -------------------------------------------------- */

int fatInit(void) {
    /* Read boot sector (LBA 0). The assignment uses sector 0 as the FS boot sector. */
    unsigned char tmpbuf[SECTOR_SIZE] __attribute__((aligned(4)));
    if (read_sectors(0, tmpbuf, 1) < 0) return -1;

    /* copy into bs struct (avoid memcpy) */
    kmemcpy(&bs, tmpbuf, sizeof(struct boot_sector));

    /* Validate boot signature 0xAA55 (little endian in struct) */
    if (bs.boot_signature != 0xAA55) return -1;

    /* compute layout */
    reserved_sectors = bs.num_reserved_sectors;
    num_fats = bs.num_fat_tables;
    sectors_per_fat = bs.num_sectors_per_fat;
    root_dir_entries = bs.num_root_dir_entries;
    sectors_per_cluster = bs.num_sectors_per_cluster;

    /* total sectors selection */
    if (bs.total_sectors != 0) total_sectors = bs.total_sectors;
    else total_sectors = bs.total_sectors_in_fs;

    /* root dir sectors (each entry 32 bytes) */
    root_dir_sectors = ((root_dir_entries * 32) + (SECTOR_SIZE - 1)) / SECTOR_SIZE;
    if (root_dir_sectors > ROOTDIR_MAX_SECTORS) return -1; /* too large for static buffer */

    /* start sectors */
    root_dir_start = reserved_sectors + (num_fats * sectors_per_fat);
    first_data_sector = root_dir_start + root_dir_sectors;

    /* determine FAT type by counting clusters */
    {
        unsigned int data_sectors = total_sectors - first_data_sector;
        unsigned int total_clusters = data_sectors / sectors_per_cluster;
        if (total_clusters < 4085) fat_type = 12;
        else fat_type = 16; /* we support only 12 or 16 here */
    }

    /* Pre-read root directory region into buffer */
    if (read_sectors(root_dir_start, rootdir_buf, root_dir_sectors) < 0) return -1;
    rootdir_sectors_read = root_dir_sectors;

    /* invalidate fat cache */
    fat_cache_base_sector = (unsigned int)-1;

    return 0;
}

/* file structure returned - same as in your fat.h */
struct file *fatOpen(const char *filename) {
    if (!filename) return (struct file*)0;

    char want[11];
    make_8dot3(filename, want);

    /* iterate root directory entries (each 32 bytes) */
    unsigned int entries = root_dir_entries;
    unsigned char *ptr = rootdir_buf;
    for (unsigned int i = 0; i < entries; ++i) {
        unsigned char first = ptr[0];
        if (first == 0x00) break;   /* no more files */
        if (first == 0xE5) { ptr += 32; continue; } /* deleted */
        uint8_t attr = ptr[11];
        if (attr & 0x08) { ptr += 32; continue; } /* volume label skip */

        /* compare 11-byte filename */
        if (kmemcmp(ptr, want, 11) == 0) {
            /* copy into returned struct file (use static allocation) */
            static struct file fbuf; /* single-file support; assignment didn't require multi-open */
            /* zero it */
            for (unsigned int z = 0; z < sizeof(struct file); ++z) ((unsigned char*)&fbuf)[z] = 0;

            /* copy rde bytes into fbuf.rde */
            for (unsigned int j = 0; j < sizeof(struct root_directory_entry); ++j) {
                ((unsigned char*)&fbuf.rde)[j] = ptr[j];
            }
            /* cluster field is 16-bit in root entry for FAT12/16 */
            fbuf.start_cluster = fbuf.rde.cluster;
            return &fbuf;
        }

        ptr += 32;
    }

    return (struct file*)0; /* not found */
}

/* Read 'count' bytes from file 'f' starting at 'offset' into 'buf'.
   Returns bytes read or -1 on error. */
int fatRead(struct file *f, void *buf, uint32_t count, uint32_t offset) {
    if (!f || !buf) return -1;

    uint32_t file_size = f->rde.file_size;
    if (offset >= file_size) return 0;
    if (offset + count > file_size) count = file_size - offset;

    unsigned char *out = (unsigned char*)buf;
    uint32_t bytes_per_cluster = sectors_per_cluster * SECTOR_SIZE;

    /* find initial cluster and offset within it */
    uint32_t cluster = f->start_cluster;
    if (cluster < 2) return -1;

    uint32_t cluster_index = offset / bytes_per_cluster;
    uint32_t cluster_off = offset % bytes_per_cluster;

    /* advance cluster chain cluster_index times */
    for (uint32_t i = 0; i < cluster_index; ++i) {
        uint32_t next = get_fat_entry(cluster);
        if (next == 0xFFFFFFFF) return -1;
        /* check for EOF markers */
        if (fat_type == 16) {
            if (next >= 0xFFF8) return 0;
        } else {
            if (next >= 0xFF8) return 0;
        }
        cluster = next;
    }

    uint32_t bytes_read = 0;
    unsigned char cluster_buf[SECTOR_SIZE * 4]; /* support up to 4 sectors per cluster on small systems; adjust if needed */
    if (sectors_per_cluster > 4) { /* safety: if cluster bigger than our temp buffer, fail (or handle per-sector) */
        /* fallback: read sector-by-sector (per-sector loop) */
        /* We'll implement per-sector read path below */
    }

    while (bytes_read < count) {
        uint32_t first_sector = first_data_sector + (cluster - 2) * sectors_per_cluster;

        /* if cluster fits into our cluster_buf */
        if (sectors_per_cluster <= 4) {
            if (read_sectors(first_sector, cluster_buf, sectors_per_cluster) < 0) return -1;
            uint32_t can_copy = bytes_per_cluster - cluster_off;
            if (can_copy > (count - bytes_read)) can_copy = (count - bytes_read);
            /* copy */
            for (uint32_t k = 0; k < can_copy; ++k) {
                out[bytes_read + k] = cluster_buf[cluster_off + k];
            }
            bytes_read += can_copy;
        } else {
            /* per-sector read - simpler but slower */
            for (unsigned int s = 0; s < sectors_per_cluster && bytes_read < count; ++s) {
                unsigned char sector_temp[SECTOR_SIZE];
                if (read_sectors(first_sector + s, sector_temp, 1) < 0) return -1;
                uint32_t start = (s == 0 ? cluster_off : 0);
                for (uint32_t k = start; k < SECTOR_SIZE && bytes_read < count; ++k) {
                    out[bytes_read++] = sector_temp[k];
                }
            }
        }

        if (bytes_read >= count) break;

        cluster_off = 0; /* subsequent clusters start at 0 */
        uint32_t next = get_fat_entry(cluster);
        if (next == 0xFFFFFFFF) return -1;
        if (fat_type == 16) {
            if (next >= 0xFFF8) break;
        } else {
            if (next >= 0xFF8) break;
        }
        cluster = next;
    }

    return (int)bytes_read;
}
