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

#define KernelSetupAddr     0x00090000
#define KernelCLAddr        0x00090800
#define BootloaderGDTAddr   0x00091000
#define BootloaderCLAddr    0x00091800
#define BootloaderStackAddr 0x00092000
#define BootloaderCodeAddr  0x00093000

#define PageSize 4096

typedef struct {
  Bit32u low;
  Bit32u high;
  } __attribute__ ((packed)) gdt_entry_t ;


// ===================
// Function prototypes
// ===================

static int      openFD(void);
static unsigned plex86CpuInfo(void);
static phyAddr_t loadImage(unsigned char *path, phyAddr_t imagePhyAddr,
                           phyAddr_t sizeMax);
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

#define BLCommandLineMax 2048 // Fixme: should get from include file.
#define KernelCommandLineMax 2048 // Fixme: should get from include file.
static unsigned char blImagePathname[NAME_MAX];
static unsigned char blCommandLine[BLCommandLineMax];

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
  blImagePathname[0] = 0;
  blCommandLine[0] = 0;
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
    else if ( !strcmp(argv[argi], "-bootloader-image") ) {
      CheckArgsForParameter(1);
      strncpy(blImagePathname, argv[argi+1], NAME_MAX-1);
      blImagePathname[NAME_MAX-1] = 0; /* Make damn sure string ends. */
      argi += 2;
      }
    else if ( !strcmp(argv[argi], "-linux-commandline") ) {
      CheckArgsForParameter(1);
      strncpy(kernelCommandLine, argv[argi+1], KernelCommandLineMax);
      /* Make damn sure string ends. */
      kernelCommandLine[KernelCommandLineMax-1] = 0;
      argi += 2;
      }
    else if ( !strcmp(argv[argi], "-bootloader-commandline") ) {
      CheckArgsForParameter(1);
      strncpy(blCommandLine, argv[argi+1], BLCommandLineMax);
      /* Make damn sure string ends. */
      blCommandLine[BLCommandLineMax-1] = 0;
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
  if ( blImagePathname[0] == 0 ) {
    fprintf(stderr, "You must provide a '-bootloader-image' option.\n");
    goto errorUsage;
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

  // Load linux kernel image, optionally the initrd image into the
  // guest's physical memory.  Then load the 32-bit mini-bootloader.
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
fprintf(stderr, "INITRD: loaded @ 0x%x, size = %u\n",
        initrdImageLoadAddr, initrdImageSize);
    }

  // Load mini 32-bit bootloader @ 0x90000.  It needs to fit before the
  // VGA memory area at 0xA0000, so it has a max size of 0x10000.
  if ( loadImage(blImagePathname, 0x90000, 0x10000) == 0 ) {
    (void) plex86TearDown();
    return(1); // Error.
    }
  }


  // Copy kernel command line to Linux setup area, if one was given.
  if ( kernelCommandLine[0] )
    strcpy( &plex86MemPtr[KernelCLAddr], kernelCommandLine );

  // Pass required args to the bootloader, based on requested parameters.
  sprintf( &plex86MemPtr[BootloaderCLAddr],
           "-megs %u -initrd-image 0x%x 0x%x",
           nMegs, initrdImageLoadAddr, initrdImageSize);
  // Fixme: we should allow *append* to bootloader args, from blCommandLine[]
  //if ( blCommandLine[0] )
  //  strcpy( &plex86MemPtr[BootloaderCLAddr], blCommandLine );

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

  // Get pointer to bootloader GDT.
  gdt = (gdt_entry_t *) &plex86MemPtr[BootloaderGDTAddr];

  // Fixme: remove these checks.
  if (gdt[LinuxBootCsSlot].high != 0x00cf9a00) goto Error;
  if (gdt[LinuxBootCsSlot].low  != 0x0000ffff) goto Error;
  if (gdt[LinuxBootDsSlot].high != 0x00cf9200) goto Error;
  if (gdt[LinuxBootDsSlot].low  != 0x0000ffff) goto Error;

  if (gdt[LinuxUserCsSlot].high != 0x00cffa00) goto Error;
  if (gdt[LinuxUserCsSlot].low  != 0x0000ffff) goto Error;
  if (gdt[LinuxUserDsSlot].high != 0x00cff200) goto Error;
  if (gdt[LinuxUserDsSlot].low  != 0x0000ffff) goto Error;

  // ===
  // CPU
  // ===

  plex86GuestCPU->cr0.raw = 0x00000033; // CR0.PE=1
  plex86GuestCPU->eflags  = 0x00000002; // EFLAGS.IF=0 (reserved bit=1)
  plex86GuestCPU->gdtr.base  = BootloaderGDTAddr;
  plex86GuestCPU->gdtr.limit = 0x400;

  // CS:EIP = 0x10: linuxImageLoadAddr
  plex86GuestCPU->sreg[SRegCS].sel.raw = Selector(LinuxBootCsSlot,0,0);
  plex86GuestCPU->sreg[SRegCS].des = * (descriptor_t *) &gdt[LinuxBootCsSlot];
  plex86GuestCPU->sreg[SRegCS].valid = 1;
  plex86GuestCPU->eip = BootloaderCodeAddr;

  // Plex86 requires a valid SS to iret to the guest.  Linux will reload
  // this immediately, so it's really not used.
  plex86GuestCPU->sreg[SRegSS].sel.raw = Selector(LinuxBootDsSlot,0,0);
  plex86GuestCPU->sreg[SRegSS].des = * (descriptor_t *) &gdt[LinuxBootDsSlot];
  plex86GuestCPU->sreg[SRegSS].valid = 1;
  plex86GuestCPU->genReg[GenRegESP] = BootloaderStackAddr + 4096; // Fixme:


  // Leave zeroed:
  //   LDTR
  //   TR
  //   IDTR
  //   data segments
  //   DRx
  //   TRx
  //   CR[1..4]
return;

Error: // Fixme:
  (void) plex86TearDown();
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

      case MonReqBogus:
        // I put this message in here as a way for the monitor to
        // give this loop an opportunity to execute so it can service
        // async HAL events.
        goto executeLoop;
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
#if 1
  // VGA text framebuffer dump.
  unsigned col, c, i, lines;
  unsigned char lineBuffer[81];

  unsigned framebuffer_start;
  unsigned isVisible;

//framebuffer_start = 0xb8000 + 2*((vga.CRTC.reg[12] << 8) + vga.CRTC.reg[13]);
  framebuffer_start = 0xb8000;
  lines = 204; // 204 Max
  //lines = 160; // 204 Max

  for (i=0; i<2*80*lines; i+=2*80) {
    isVisible = 0;
    for (col=0; col<80; col++) {
      c = plex86MemPtr[framebuffer_start + i + col*2];
      if ( isgraph(c) ) {
        lineBuffer[col] = c;
        isVisible = 1;
        }
      else
        lineBuffer[col] = ' ';
      }
    lineBuffer[sizeof(lineBuffer)-1] = 0; // Null terminate string.
    if (isVisible)
      fprintf(stderr, "%s\n", lineBuffer);
    }
#endif
}
