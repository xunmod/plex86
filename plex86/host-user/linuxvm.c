/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#include "plex86.h"
#include "linuxvm.h"
#include "linux-setup.h"
#include "eflags.h"
#include "hal.h"
#include "hal-user.h"


// NOTES:
//   Updating segment descriptor A bits upon descriptor load.
//   Clean assertRF passed to doInterrupt()


// ===================
// Defines / typesdefs
// ===================

#define Plex86StateMemAllocated     0x01
#define Plex86StateMemRegistered    0x02
#define Plex86StateReady            0x03 /* All bits set. */

#define SetupPageAddr 0x00090000
#define BootGDTAddr   0x00091000

#define PageSize 4096

typedef struct {
  Bit32u low;
  Bit32u high;
  } __attribute__ ((packed)) gdt_entry_t ;


#if 0
// From linux/include/asm-i386/setup.h
#define PARAM ((unsigned char *)empty_zero_page)

ok #define SCREEN_INFO (*(struct screen_info *) (PARAM+0))
ok #define EXT_MEM_K (*(unsigned short *) (PARAM+2))
ok #define ALT_MEM_K (*(unsigned long *) (PARAM+0x1e0))
ok #define APM_BIOS_INFO (*(struct apm_bios_info *) (PARAM+0x40))
-- #define DRIVE_INFO (*(struct drive_info_struct *) (PARAM+0x80))
ok #define SYS_DESC_TABLE (*(struct sys_desc_table_struct*)(PARAM+0xa0))
ok #define MOUNT_ROOT_RDONLY (*(unsigned short *) (PARAM+0x1F2))
ok #define RAMDISK_FLAGS (*(unsigned short *) (PARAM+0x1F8))
ok #define VIDEO_MODE (*(unsigned short *) (PARAM+0x1FA))
ok #define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
ok #define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
ok #define LOADER_TYPE (*(unsigned char *) (PARAM+0x210))
ok #define KERNEL_START (*(unsigned long *) (PARAM+0x214))
ok #define INITRD_START (*(unsigned long *) (PARAM+0x218))
ok #define INITRD_SIZE (*(unsigned long *) (PARAM+0x21c))
ok #define COMMAND_LINE ((char *) (PARAM+2048))

ok #define EDD_NR     (*(unsigned char *) (PARAM+EDDNR))
ok #define EDD_BUF     ((struct edd_info *) (PARAM+EDDBUF))
ok #define E820_MAP_NR (*(char*) (PARAM+E820NR))
ok #define E820_MAP    ((struct e820entry *) (PARAM+E820MAP))
#endif

// initrd= [BOOT] Specify the location of the initial ramdisk
// ramdisk_size= [RAM] Sizes of RAM disks in kilobytes
//         New name for the ramdisk parameter.
//         See Documentation/ramdisk.txt.
// ramdisk_start=  [RAM] Starting block of RAM disk image (so you can
//         place it after the kernel image on a boot floppy).
//         See Documentation/ramdisk.txt.
// Kernel command line. (Linux uses only 128 max bytes out of 2k buffer)
//memset(params->commandline, 0, sizeof(params->commandline));
//  w/ devfs:
//    root=/dev/ram0 init=/linuxrc rw
//  w/o devfs:
//    root=/dev/rd/0 init=/linuxrc rw
//strcpy((char*)params->commandline, "root=/dev/ram0 init=/linuxrc rw"
//                                   " mem=nopentium");
//strcpy((char*)params->commandline, "root=/dev/rd/0 init=/linuxrc rw");



// ===================
// Function prototypes
// ===================

static int      openFD(void);
static unsigned plex86CpuInfo(void);
static phyAddr_t loadImage(unsigned char *path, phyAddr_t imagePhyAddr,
                           phyAddr_t sizeMax);
static void initLinuxSetupPage(void);
static void initLinuxIOenvironment(void);
static void initLinuxCPUMemenvironment(void);
static unsigned  executeLinux(void);

static void      myCtrlCHandler(int signum);
static void doVGADump(void);


// =========
// Variables
// =========

__asm__ (".comm   plex86PrintBufferPage,4096,4096");
__asm__ (".comm   plex86GuestCPUPage,4096,4096");
extern Bit8u       plex86PrintBufferPage[];
extern Bit8u       plex86GuestCPUPage[];

static Bit8u       *plex86MemPtr_actual = 0;
       Bit8u       *plex86MemPtr = 0;
       size_t       plex86MemSize = 0;
static Bit8u       *plex86PrintBuffer = plex86PrintBufferPage;
       guest_cpu_t *plex86GuestCPU = (guest_cpu_t *) plex86GuestCPUPage;

       cpuid_info_t  guestCPUID;

static unsigned char linuxImagePathname[NAME_MAX];
static phyAddr_t linuxImageLoadAddr = 0x100000; // For now...
static phyAddr_t linuxImageSize=0;

static unsigned char initrdImagePathname[NAME_MAX];
static phyAddr_t initrdImageLoadAddr = 0; // Signal no initrd.
static phyAddr_t initrdImageSize=0;

static unsigned char tunscriptPathname[NAME_MAX];

static unsigned char kernelCommandLine[KernelCommandLineMax];

static unsigned      plex86State = 0;
static int           plex86FD = -1;

static unsigned vgaDump = 0;


static unsigned faultCount[32];
#define CyclesOverhead 980  // Fixme: apply this bias to the cycle counts.
#define BiasCycles( count ) \
    do { if ( (count) > CyclesOverhead ) count -= CyclesOverhead; } while (0)
static Bit64u   minCyclesExecuted = -1;


  int
main(int argc, char *argv[])
{
  int argi = 1; /* Start at 1st option, skip program name. */
  int nMegs;
  plex86IoctlRegisterMem_t ioctlMsg;

#define CheckArgsForParameter(n) \
    if ( (argi+(n)) >= argc ) goto errorArgsUnderflow

  // Sanity checks.
  if (sizeof(linuxSetupParams_t) != 4096) {
    fprintf(stderr, "plex86: sizeof(linuxSetupParams_t) is %u, "
                    "should be 4096.\n", sizeof(linuxSetupParams_t));
    return(1); // Error.
    }
  if ( sizeof(descriptor_t) != 8 ) {
    fprintf(stderr, "plex86: sizeof(descriptor_t) is %u, "
                    "should be 8.\n", sizeof(descriptor_t));
    return(1); // Error.
    }
  if ( sizeof(gdt_entry_t) != 8 ) {
    fprintf(stderr, "plex86: sizeof(gdt_entry_t) is %u, "
                    "should be 8.\n", sizeof(gdt_entry_t));
    return(1); // Error.
    }
if ( sizeof(descriptor_t) != 8 ) {
  fprintf(stderr, "plex86: sizeof(descriptor_t) is %u, "
                  "should be 8.\n", sizeof(descriptor_t));
  return(1); // Error.
  }
  if ( sizeof(gate_t) != 8 ) {
    fprintf(stderr, "plex86: sizeof(gate_t) is %u, "
                    "should be 8.\n", sizeof(gate_t));
    return(1); // Error.
    }

  /* Initialize before processing command line. */
  nMegs = 0;
  linuxImagePathname[0] = 0;
  initrdImagePathname[0] = 0;
  tunscriptPathname[0] = 0;
  kernelCommandLine[0] = 0;
  memset( &faultCount, 0, sizeof(faultCount) );

  /* Process command line. */
  while (argi < argc) {
    if ( !strcmp(argv[argi], "-megs") ) {
      CheckArgsForParameter(1);
      nMegs = atoi(argv[argi+1]);
      plex86MemSize = nMegs * (1<<20);
      argi += 2;
      }
    else if ( !strcmp(argv[argi], "-linux-image") ) {
      CheckArgsForParameter(1);
      strncpy(linuxImagePathname, argv[argi+1], NAME_MAX-1);
      linuxImagePathname[NAME_MAX-1] = 0; /* Make damn sure string ends. */
      argi += 2;
      }
    else if ( !strcmp(argv[argi], "-initrd-image") ) {
      CheckArgsForParameter(2);
      initrdImageLoadAddr = strtoul(argv[argi+1], NULL, 0);
fprintf(stderr, "initrdImageLoadAddr is 0x%x\n", initrdImageLoadAddr);
      if ( initrdImageLoadAddr == 0 ) {
        goto errorUsage;
        }
      strncpy(initrdImagePathname, argv[argi+2], NAME_MAX-1);
      initrdImagePathname[NAME_MAX-1] = 0; /* Make damn sure string ends. */
      argi += 3;
      }
    else if ( !strcmp(argv[argi], "-command-line") ) {
      CheckArgsForParameter(1);
      strncpy(kernelCommandLine, argv[argi+1], KernelCommandLineMax);
      /* Make damn sure string ends. */
      kernelCommandLine[KernelCommandLineMax-1] = 0;
      argi += 2;
      }
    else if ( !strcmp(argv[argi], "-dump-vga") ) {
      argi += 1;
      vgaDump = 1;
      }
    else if ( !strcmp(argv[argi], "-tun-script") ) {
      CheckArgsForParameter(1);
      strncpy(tunscriptPathname, argv[argi+1], NAME_MAX-1);
      tunscriptPathname[NAME_MAX-1] = 0; /* Make damn sure string ends. */
      argi += 2;
      }
    else {
      goto errorArgUnrecognized;
      }
    }

  // Argument consistency checks.
  if ( nMegs == 0 ) {
    fprintf(stderr, "You must provide  an '-megs' option.\n");
    goto errorUsage;
    }
#define MaxNMegs 32 // For now.
  if ( (nMegs>MaxNMegs) || (nMegs&3) ) {
    fprintf(stderr, "-megs value must be multiple of 4, up to %u.\n", MaxNMegs);
    goto errorUsage;
    }
  if ( linuxImagePathname[0] == 0 ) {
    fprintf(stderr, "You must provide a '-linux-image' option.\n");
    goto errorUsage;
    }
  if (linuxImageLoadAddr >= plex86MemSize) {
    fprintf(stderr, "Linux image load address of 0x%x beyond physical memory.\n",
            linuxImageLoadAddr);
    }
  if (initrdImageLoadAddr >= plex86MemSize) {
    fprintf(stderr, "initrd image load address of 0x%x beyond physical memory.\n",
            initrdImageLoadAddr);
    }
  if ( tunscriptPathname[0] == 0 ) {
    fprintf(stderr, "Note: you can specify a script to configure the tuntap "
                    "interface using -tun-script.\n");
    }

  // Allocate guest machine physical memory.
  // Note that plex86 requires our memory array to be page aligned.  So
  // we allocate an extra page, and round up to the next page aligned
  // address.
  plex86MemSize = nMegs << 20;
  plex86MemPtr_actual = malloc(plex86MemSize + 4096);
  if ( plex86MemPtr_actual == NULL ) {
    fprintf(stderr, "Allocation of %u Megs of guest physical memory failed.\n",
            nMegs);
    }
  plex86State |= Plex86StateMemAllocated;
  plex86MemPtr = (Bit8u *)
      ( (((Bit32u)plex86MemPtr_actual)+PageSize) & ~(PageSize-1) );

  // Clear print buffer window to plex86.
  memset(plex86PrintBuffer, 0, PageSize);

  // Clear guest physical memory and CPU window to plex86.
  memset(plex86MemPtr_actual, 0, plex86MemSize + 4096);
  memset(plex86GuestCPU, 0, PageSize);

  (void) signal(SIGINT, myCtrlCHandler);

  // Open a connection to the plex86 kernel module.
  if ( !openFD() ) {
    fprintf(stderr, "plex86: openFD failed.\n");
    (void) plex86TearDown();
    return(1); // Error.
    }

  fprintf(stderr, "plex86: setting to PLEX86_LINUX_VM_MODE.\n");
  if (ioctl(plex86FD, PLEX86_LINUX_VM_MODE, NULL) != 0) {
    fprintf(stderr, "plex86: ioctl(PLEX86_LINUX_VM_MODE) failed.\n");
    (void) plex86TearDown();
    return(1); // Error.
    }

  // Register guest machine physical memory with plex86.
  ioctlMsg.nMegs = nMegs;
  ioctlMsg.guestPhyMemVector = (Bit32u) plex86MemPtr;
  ioctlMsg.logBufferWindow   = (Bit32u) plex86PrintBuffer;
  ioctlMsg.guestCPUWindow    = (Bit32u) plex86GuestCPU;
  if (ioctl(plex86FD, PLEX86_REGISTER_MEMORY, &ioctlMsg) == -1) {
    fprintf(stderr, "plex86: ioctl(REGISTER_MEMORY) failed.\n");
    (void) plex86TearDown();
    return(1); // Error.
    }
  plex86State |= Plex86StateMemRegistered;
  fprintf(stderr, "plex86: RegisterGuestMemory: %u MB succeeded.\n",
          ioctlMsg.nMegs);

  if ( !plex86CpuInfo() ) {
    fprintf(stderr, "plex86: plex86CpuInfo() failed.\n");
    (void) plex86TearDown();
    return(1); // Error.
    }

  // Load linux kernel image, and optionally the initrd image into the
  // guest's physical memory.
  {
  phyAddr_t imageSizeMax;

  // Load the Linux image into guest physical memory.
  imageSizeMax = plex86MemSize - linuxImageLoadAddr;
  linuxImageSize = loadImage(linuxImagePathname, linuxImageLoadAddr,
                             imageSizeMax);
  if (linuxImageSize == 0) {
    (void) plex86TearDown();
    return(1); // Error.
    }

  // Conditionally load the initial ramdisk, if one is requested.
  if (initrdImageLoadAddr) {
    imageSizeMax = plex86MemSize - initrdImageLoadAddr;
    initrdImageSize = loadImage(initrdImagePathname, initrdImageLoadAddr,
                                imageSizeMax);
    if (initrdImageSize == 0) {
      (void) plex86TearDown();
      return(1); // Error.
      }
    }
  }

  // On a normal machine, the entire boot process distills boot information
  // into one page of memory for communication with the 32-bit kernel
  // image which is loaded into memory.  We need to initialize this page
  // to appropriate values for our boot.

  fprintf(stderr, "plex86: filling in Linux setup page.\n");
  initLinuxSetupPage();

  initLinuxIOenvironment();
  initLinuxCPUMemenvironment();

  if ( initHal( tunscriptPathname ) == 0 ) {
    (void) plex86TearDown();
    return(1); // Error.
    }

  (void) executeLinux();


  (void) plex86TearDown();
  return(0); // OK.


/* Error handling. */
errorArgsUnderflow:
  fprintf(stderr, "Not enough parameters in command line for "
          "option '%s'.\n", argv[argi]);
  return(1);

errorArgUnrecognized:
  fprintf(stderr, "Unrecognized command line option '%s'.\n", argv[argi]);
  return(1);

errorUsage:
  fprintf(stderr, "Usage: ...\n");
  return(1);
}

  int
openFD(void)
{
  if (plex86State != Plex86StateMemAllocated) {
    // This should be the first operation; no state should be set yet.
    fprintf(stderr, "plex86: openFD: plex86State = 0x%x\n", plex86State);
    return(0); // Error.
    }

  // Open a new VM.
  fprintf(stderr, "plex86: opening VM.\n");
  fprintf(stderr, "plex86: trying /dev/misc/plex86...");
  plex86FD = open("/dev/misc/plex86", O_RDWR);
  if (plex86FD < 0) {
    fprintf(stderr, "failed.\n");
    // Try the old name.
    fprintf(stderr, "plex86: trying /dev/plex86...");
    plex86FD = open("/dev/plex86", O_RDWR);
    if (plex86FD < 0) {
      fprintf(stderr, "failed.\n");
      fprintf(stderr, "plex86: did you load the kernel module?"
              "  Read the toplevel README file!\n");
      perror ("open");
      return(-1); // Error.
      }
    }
  fprintf(stderr, "OK.\n");
  return(1); // OK.
}

  unsigned
plex86CpuInfo(void)
{
  Bit32u eax, ebx, ecx, edx;

  if (plex86FD < 0) {
    // If the plex86 File Descriptor has not been opened yet.
    if ( !openFD() ) {
      return(0); // Error.
      }
    }

  /* We have to tell plex86 what the capabilities of our guest
   * machine are.  In our case, they are the same as the host
   * machine since we are not doing emulation.  So report the
   * values from the actual host CPUID instruction.
   */
   
  /* Get the highest allowed cpuid level. */
  __asm__ volatile (
    "xorl %%eax,%%eax\n\t"
    "cpuid"
    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
    :
    : "cc"
    );
  if (eax < 1)
    return(0); /* Not enough capabilities. */

  /* Copy vendor string. */
  guestCPUID.vendorDWord0 = ebx;
  guestCPUID.vendorDWord1 = edx;
  guestCPUID.vendorDWord2 = ecx;

  /* CPUID w/ EAX==1: Processor Signature & Feature Flags. */
  __asm__ volatile (
    "movl $1,%%eax\n\t"
    "cpuid"
    : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
    :
    : "cc"
    );
  guestCPUID.procSignature.raw = eax;
  guestCPUID.featureFlags.raw = edx;
  /* Plex86 needs the Time Stamp Counter. */
  if (guestCPUID.featureFlags.fields.tsc==0)
    return(0);
  if (guestCPUID.featureFlags.fields.vme==0)
    return(0);

  fprintf(stderr, "plex86: passing guest CPUID to plex86.\n");
  if ( ioctl(plex86FD, PLEX86_CPUID, &guestCPUID) ) {
    perror("ioctl CPUID: ");
    return(0); // Error.
    }

  return(1); // OK.
}

  unsigned
plex86TearDown(void)
{
  unsigned f;
  fprintf(stderr, "plex86: plex86TearDown called.\n");


  fprintf(stderr, "plex86: guest Fault Count (FYI):\n");
  for (f=0; f<32; f++) {
    if (faultCount[f])
      fprintf(stderr, "plex86:  FC[%u] = %u\n", f, faultCount[f]);
    }

  if ( plex86FD < 0 ) {
    fprintf(stderr, "plex86: plex86TearDown: FD not open.\n");
    }
  else {
    fprintf(stderr, "plex86: tearing down VM.\n");
    if (ioctl(plex86FD, PLEX86_UNREGISTER_MEMORY, NULL) == -1) {
      fprintf(stderr, "plex86: ioctl(UNREGISTER_MEMORY) failed.\n");
      return(0); // Failed.
      }
    plex86State &= ~Plex86StateMemRegistered;

    if (ioctl(plex86FD, PLEX86_TEARDOWN, 0) < 0) {
      perror("ioctl TEARDOWN: ");
      return(0); // Failed.
      }

    // Close the connection to the kernel module.
    fprintf(stderr, "plex86: closing VM device.\n");
    if (close(plex86FD) == -1) {
      perror("close of VM device\n");
      return(0); // Failed.
      }
    plex86FD = -1; // File descriptor is now closed.

    if ( vgaDump )
      doVGADump();
    fprintf(stderr, "minCyclesExecuted = %llu.\n", minCyclesExecuted);
    }

  if (plex86State & Plex86StateMemAllocated) {
    fprintf(stderr, "plex86: freeing guest physical memory.\n");
    free(plex86MemPtr_actual);
    plex86State &= ~Plex86StateMemAllocated;
    }

  if (plex86State != 0) {
    fprintf(stderr, "plex86: Error, state(0x%x) != 0.\n", plex86State);
    plex86State = 0; // For good measure.
    }

  return(1); // OK.
}

  phyAddr_t
loadImage(unsigned char *path, phyAddr_t imagePhyAddr, phyAddr_t sizeMax)
{
  struct stat stat_buf;
  int fd=-1, ret;
  phyAddr_t size;
  Bit8u *imageLocation = &plex86MemPtr[imagePhyAddr];

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
  fprintf(stderr, "loadImage: image '%s', size=%u,\n"
                  "loadImage:   read into memory at guest physical address "
                  "0x%08x.\n",
          path,
          (unsigned) stat_buf.st_size,
          (unsigned) imagePhyAddr);
  close(fd);
  return( (unsigned) stat_buf.st_size );

error:
  if (fd!=-1)
    close(fd);
  return(0); // Failed.
}

  void
initLinuxSetupPage(void)
{
  linuxSetupParams_t *params =
         (linuxSetupParams_t *) &plex86MemPtr[SetupPageAddr];
  phyAddr_t memNMegs = plex86MemSize >> 20;

  memset( params, '\0', sizeof(*params) );

#if 0
  // Stuff from older code.  These fields conflicted with new
  // definitions of the boot parameter area, as per the file
  // "linux/Documentation/i386/zero-page.txt".
  params->bootsect_magic = 0xaa55;
#endif

  params->orig_x = 0;
  params->orig_y = 0;
  /* Memory size (total mem - 1MB, in KB) */
  params->ext_mem_k = (memNMegs - 1) * 1024;
  params->orig_video_page = 0;
  params->orig_video_mode = 3;
  params->orig_video_cols = 80;
  params->orig_video_ega_bx = 3;
  params->orig_video_lines = 25;
  params->orig_video_isVGA = 1;
  params->orig_video_points = 16;

  // Commandline magic.  If it's 0xa33f, then the address of the commandline
  // is calculated as 0x90000 + cl_offset.
  params->cl_magic = 0xa33f;
  params->cl_offset = 0x800;

  //memset(&params->apm_bios_info, 0, sizeof(params->apm_bios_info));
  //memset(&params->hd0_info, 0, sizeof(params->hd0_info));
  //memset(&params->hd1_info, 0, sizeof(params->hd1_info));
  params->sys_description_len = 0; // Make at least 4.
  params->sys_description_table[0] = 0; // FIXME
  params->sys_description_table[1] = 0; // FIXME
  params->sys_description_table[2] = 0; // FIXME
  params->sys_description_table[3] = 0; // FIXME
  params->alt_mem_k = params->ext_mem_k; // FIXME
  params->e820map_entries = 0;
  params->eddbuf_entries = 0;
  params->setup_sects = 0; // size of setup.S, number of sectors
  params->mount_root_rdonly = 0;
  params->sys_size = 0; // FIXME: size of compressed kernel-part in the
                        // (b)zImage-file (in 16 byte units, rounded up)
  params->swap_dev = 0; // (unused AFAIK)
  if ( initrdImageLoadAddr ) {
    // params->ramdisk_flags = RAMDISK_LOAD_FLAG; // FIXME
    params->ramdisk_flags = 0; // FIXME
    }
  else {
    params->ramdisk_flags = 0; // FIXME
    }
  params->vga_mode = 3; // FIXME: (old one)
  params->orig_root_dev = 0x0000; // (high=Major, low=minor)
  params->aux_device_info = 0; // FIXME
  params->jump_setup = 0; // FIXME
  memcpy(params->setup_signature, "HdrS", 4); // Signature for SETUP-header.
  params->header_format_version = 0x0203; // Current header format.
  //memset(params->setup_S_temp0, 0, sizeof(params->setup_S_temp0));
  params->loader_type = 1; // Loadlin.  Should we use this?

  // bit0 = 1: kernel is loaded high (bzImage)
  // bit7 = 1: Heap and pointer (see below) set by boot loader.
  params->loadflags = 0x1;
  params->setup_S_temp1 = 0; // FIXME

  params->kernel_start = linuxImageLoadAddr;
  params->initrd_start = initrdImageLoadAddr;
  params->initrd_size  = initrdImageSize;

  //memset(params->setup_S_temp2, 0, sizeof(params->setup_S_temp2));
  params->setup_S_heap_end_pointer = 0; // FIXME

  // Int 15, ax=e820 memory map.
  // params->e820map[0].addr = ;
  // params->e820map[0].size = ;
  // params->e820map[0].type = ;
  // ...

  // BIOS Enhanced Disk Drive Services.
  // (From linux/include/asm-i386/edd.h, 'struct edd_info')
  // Each 'struct edd_info is 78 bytes, times a max of 6 structs in array.
  //memset(params->eddbuf, 0, sizeof(params->eddbuf));

  if ( kernelCommandLine[0] )
    strcpy((char*)params->commandline, kernelCommandLine);
}

  void
initLinuxIOenvironment(void)
{
  plex86GuestCPU->INTR = 0;
  plex86GuestCPU->halIrq = -1;
}

  void
initLinuxCPUMemenvironment(void)
{
  gdt_entry_t *gdt;

  // A20 line enabled.
  plex86GuestCPU->a20Enable = 1;

  // ======
  // Memory
  // ======

  // Setup a small bootstrap GDT.
  gdt = (gdt_entry_t *) &plex86MemPtr[BootGDTAddr];

  // Linux kernel code/data segments.
  gdt[LinuxBootCsSlot].high = 0x00cf9a00; // CS
  gdt[LinuxBootCsSlot].low  = 0x0000ffff;
  gdt[LinuxBootDsSlot].high = 0x00cf9200; // DS/SS/ES/FS/GS
  gdt[LinuxBootDsSlot].low  = 0x0000ffff;

  // Linux user code/data segments.
  gdt[LinuxUserCsSlot].high = 0x00cffa00; // CS
  gdt[LinuxUserCsSlot].low  = 0x0000ffff;
  gdt[LinuxUserDsSlot].high = 0x00cff200; // DS/SS/ES/FS/GS
  gdt[LinuxUserDsSlot].low  = 0x0000ffff;

  // ===
  // CPU
  // ===

  plex86GuestCPU->cr0.raw = 0x00000033; // CR0.PE=1
  plex86GuestCPU->eflags  = 0x00000002; // EFLAGS.IF=0 (reserved bit=1)
  plex86GuestCPU->gdtr.base  = BootGDTAddr;
  plex86GuestCPU->gdtr.limit = 0x400;

  // Addr of setup page passed to 32-bit code via ESI.
  plex86GuestCPU->genReg[GenRegESI] = SetupPageAddr;

  // CS:EIP = 0x10: linuxImageLoadAddr
  plex86GuestCPU->sreg[SRegCS].sel.raw = Selector(LinuxBootCsSlot,0,0);
  plex86GuestCPU->sreg[SRegCS].des = * (descriptor_t *) &gdt[LinuxBootCsSlot];
  plex86GuestCPU->sreg[SRegCS].valid = 1;
  plex86GuestCPU->eip      = linuxImageLoadAddr;

  // Plex86 requires a valid SS to iret to the guest.  Linux will reload
  // this immediately, so it's really not used.
  plex86GuestCPU->sreg[SRegSS].sel.raw = Selector(LinuxBootDsSlot,0,0);
  plex86GuestCPU->sreg[SRegSS].des = * (descriptor_t *) &gdt[LinuxBootDsSlot];
  plex86GuestCPU->sreg[SRegSS].valid = 1;
  plex86GuestCPU->genReg[GenRegESP] = 0x60000; // Fixme


  // Leave zeroed:
  //   LDTR
  //   TR
  //   IDTR
  //   data segments
  //   DRx
  //   TRx
  //   CR[1..4]
}

  unsigned
executeLinux(void)
{
  plex86IoctlExecute_t executeMsg;
  int   ret;
  int   ioctlNo;
  void *ioctlMsgPtr;

  if ( plex86State != Plex86StateReady ) {
    fprintf(stderr, "plex86: plex86ExecuteInVM: not in ready state (0x%x)\n",
            plex86State);
    return(0); // Error.
    }

  executeMsg.executeMethod = Plex86ExecuteMethodNative;
  ioctlNo = PLEX86_EXECUTE;
  ioctlMsgPtr = &executeMsg;

executeLoop:

  if ( tunTapEvent ) { // Fixme: make this volatile?
    if ( incrementAtomic(tunTapInService) == 1 ) {
      //fprintf(stderr, "plex86: plex86ExecuteInVM: tunTapEvent==1.\n");
      if ( tuntapReadPacketToGuest(0) ) {
        tunTapEvent = 0; // Reset TUN/TAP event flag.
        }
      tunTapInService = 0; // Reset in-service semaphore.
      }
    }

  ret = ioctl(plex86FD, ioctlNo, ioctlMsgPtr);
  if (ret != 0) {
    fprintf(stderr, "plex86: ioctl(PLEX86_EXECUTE): ");
    switch (ret) {
      case Plex86NoExecute_Method:
        fprintf(stderr, "bad execute method.\n");
        break;
      case Plex86NoExecute_CR0:
        fprintf(stderr, "bad CR0 value.\n");
        break;
      case Plex86NoExecute_CR4:
        fprintf(stderr, "bad CR4 value.\n");
        break;
      case Plex86NoExecute_CS:
        fprintf(stderr, "bad CS value.\n");
        break;
      case Plex86NoExecute_A20:
        fprintf(stderr, "bad A20 enable value.\n");
        break;
      case Plex86NoExecute_Selector:
        fprintf(stderr, "bad selector value.\n");
        break;
      case Plex86NoExecute_DPL:
        fprintf(stderr, "bad descriptor DPL.\n");
        break;
      case Plex86NoExecute_EFlags:
        fprintf(stderr, "bad EFlags (0x%x).\n", plex86GuestCPU->eflags);
        break;
      case Plex86NoExecute_Panic:
        fprintf(stderr, "panic.\n");
        break;
      case Plex86NoExecute_VMState:
        fprintf(stderr, "bad VM state.\n");
        break;
      default:
        fprintf(stderr, "ret = %d\n", ret);
        break;
      }
    }
  else {
    //unsigned pitClocks;
    //unsigned vector;

    switch ( executeMsg.monitorState.request ) {
      case MonReqFlushPrintBuf:
        fprintf(stderr, "::%s", plex86PrintBuffer);
        //tsc += executeMsg.cyclesExecuted;
        goto executeLoop;

      case MonReqRedirect:
        // Monitor had an interrupt redirect and their was work to do,
        // so it returned from the ioctl() call.  Nothing for us to do
        // except return to the monitor.
        goto executeLoop;

      case MonReqHalCall:
        halCall();
        goto executeLoop;

      case MonReqPanic:
        fprintf(stderr, "plex86: MonReqPanic:\n");
        fprintf(stderr, "::%s\n", plex86PrintBuffer);
        break;

      default:
        fprintf(stderr, "plex86: executeMsg.request = %u\n",
                executeMsg.monitorState.request);
        break;
      }
    }

  plex86TearDown();

  return(0);
}


  void
myCtrlCHandler(int signum)
{
  plex86TearDown();
  fprintf(stderr, "myCtrlC:\n");
#if 0
{
static Bit8u rxBuffer[MaxEthernetFrameSize];
unsigned packetLen;
packetLen = read(fdTunTap, rxBuffer, MaxEthernetFrameSize);
fprintf(stderr, "Attempt to read tun/tap device returns %d.\n", packetLen);
}
#endif
  exit(1);
}

  void
doVGADump(void)
{
#if 0
  // VGA text framebuffer dump.
  unsigned col, c, i, lines;
  unsigned char lineBuffer[81];

  unsigned framebuffer_start;

  framebuffer_start = 0xb8000 + 2*((vga.CRTC.reg[12] << 8) + vga.CRTC.reg[13]);
  lines = 204; // 204 Max
  //lines = 160; // 204 Max

  for (i=0; i<2*80*lines; i+=2*80) {
    for (col=0; col<80; col++) {
      c = plex86MemPtr[framebuffer_start + i + col*2];
      if ( isgraph(c) )
        lineBuffer[col] = c;
      else
        lineBuffer[col] = ' ';
      }
    lineBuffer[sizeof(lineBuffer)-1] = 0; // Null terminate string.
    fprintf(stderr, "%s\n", lineBuffer);
    }
#endif
}
