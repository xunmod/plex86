#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

unsigned char area[64 * 1024];

unsigned loadImage(unsigned char *path, unsigned sizeMax);

  int
main(int argc, char *argv[])
{
  int fd=-1;
  unsigned rsize, wsize;

  rsize = loadImage("/tmp/bootloader.img", sizeof(area));
  strcpy(&area[0x0800],
         "root=/dev/ram0 ramdisk_size=2048 init=/linuxrc mem=nopentium rw");
  sprintf(&area[0x1800],
    "-megs 32 -init-io -initrd-image 0x%x 0x%x",
    0x1a00000, 2097152);
  fd = open("/tmp/bootloader2.img", O_WRONLY | O_TRUNC | O_CREAT,
            S_IREAD | S_IWRITE);
  if (fd < 0) {
    fprintf(stderr, "Could not open '%s' for writing.\n",
            "/tmp/bootloader2.img");
    return(1);
    }
  wsize = write(fd, area, rsize);
  if (wsize != rsize) {
    fprintf(stderr, "Error: write() returned %u.\n", wsize);
    return(1);
    }
  close(fd);
  return(0);
}

  unsigned
loadImage(unsigned char *path, unsigned sizeMax)
{
  struct stat stat_buf;
  int fd=-1, ret;
  unsigned size;
  unsigned char *imageLocation = area;

  fd = open(path, O_RDONLY
#ifdef O_BINARY
            | O_BINARY
#endif
           );
  if (fd < 0) {
    fprintf(stderr, "loadImage: couldn't open image file '%s'.\n", path);
    goto error;
    }
  ret = fstat(fd, &stat_buf);
  if (ret) {
    fprintf(stderr, "loadImage: couldn't stat image file '%s'.\n", path);
    goto error;
    }

  size = stat_buf.st_size;

  if (size > sizeMax) {
    fprintf(stderr, "loadImage: image '%s' would be loaded beyond physical "
            "memory boundary.\n", path);
    goto error;
    }

  while (size > 0) {
    ret = read(fd, imageLocation, size);
    if (ret <= 0) {
      fprintf(stderr, "loadImage: read on image '%s' failed.\n", path);
      goto error;
      }
    size -= ret; // Less remaining bytes to read.
    imageLocation += ret; // Advance physical memory pointer.
    }
  fprintf(stderr, "loadImage: image '%s', size=%u.\n",
          path,
          (unsigned) stat_buf.st_size);
  close(fd);
  return( (unsigned) stat_buf.st_size );

error:
  if (fd!=-1)
    close(fd);
  return(0); // Failed.
}
