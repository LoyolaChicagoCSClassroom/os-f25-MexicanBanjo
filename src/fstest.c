

#include "fat.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

char sector_buf[512];
char rde_region[16384];
int fd = 0;

int read_sector_from_disk_image(unsigned int sector_num, char *buf, unsigned int nsectors) {

  // position the OS index 
  lseek(fd, sector_num * 512, SEEK_SET);

  // Read one sector from disk image
  int n = read(fd, buf, 512 * nsectors);
}

void extract_filename(struct root_directory_entry *rde, char *fname){
  int k = 0; // index into fname

  // Iterate thru rde->file_name, copying characters into fname[]
  while(((rde->file_name)[k] != ' ') && (k < 8)){
    fname[k] = (rde->file_name)[k];
    k++;
  }
  fname[k] = '\0'; // add a NULL terminator at the end of the fname string

  // If the file_extension field has a space as its first character, there is no file extension. We can be done.
  if((rde->file_extension)[0] == ' ') { 
   // We're done. return
   return;
  }

  // If the file_extension field doesn't have a space as its first character,
  // we need to add a dot plus the file extension to the fname string.
  fname[k++] = '.'; // Add a dot
  fname[k] = '\0'; // Add NULL terminator
  int n = 0; // Index into the file_extension field of the RDE

  // Copy bytes out of the RDE's file_extension field into the fname[] string.
  while(((rde->file_extension)[n] != ' ') && (n < 3)){
    fname[k] = (rde->file_extension)[n];
    k++;
    n++;
  }
  fname[k] = '\0'; // Add NULL terminator to end of fname[] string
}
// strcpy()
void strcpy_neil(char *dest, char *src){
// copy from arr1 to arr2
  int k = 0;
  while(src[k] != 0) {
    dest[k] = src[k];
    k++;
  }
}

int main() {
  struct boot_sector *bs = (struct boot_sector*)sector_buf;
  struct root_directory_entry *rde_tbl = (struct root_directory_entry*)rde_region;
  fd = open("disk.img", O_RDONLY);
  read_sector_from_disk_image(0, sector_buf, 1);

  printf("sectors per cluster = %d\n", bs->num_sectors_per_cluster);
  printf("reserved sectors = %d\n", bs->num_reserved_sectors);
  printf("num fat tables = %d\n", bs->num_fat_tables);
  printf("num RDEs = %d\n", bs->num_root_dir_entries);

  // Read RDE Region
  read_sector_from_disk_image( 0 + bs->num_reserved_sectors + bs->num_fat_tables * bs->num_sectors_per_fat, // Sector num
                               rde_region,  // buffer to hold the RDE table
                               32); // Number of sectors in the RDE table
  for(int k = 0; k < 512; k++) {
    char temp_str[16]; // Temporary string to hold filename extracted from the current RDE

    extract_filename(&rde_tbl[k], temp_str);
    printf("fname = %s\n", temp_str);

    // In the file open function, we would compare temp_str[] to the name of the
    // file we're looking for (possibly by calling C's strcmp() function). Note
    // if you use strcmp() you'd have to write that yourself. When we find an
    // RDE with a matching filename, we're done with the fatOpen() function.
  }

  return 0;
}
