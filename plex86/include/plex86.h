/************************************************************************
 * $Id$
 ************************************************************************
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2003  Kevin P. Lawton
 */

#ifndef __PLEX86_H__
#define __PLEX86_H__

#include "config.h"
#include "descriptor.h"


/* The layout of the VM monitor GDT.  The slots named LinuxXYZ are
 * only used when running a Linux-specific VM (vm->linuxVMMode).
 */
#if 0
// Linux 2.5.59
#define LinuxBootCsSlot     2
#define LinuxBootDsSlot     3
#define LinuxKernelCsSlot  12
#define LinuxKernelDsSlot  13
#define LinuxUserCsSlot    14
#define LinuxUserDsSlot    15
#else
// Linux 2.4.20
#define LinuxBootCsSlot     2
#define LinuxBootDsSlot     3
#define LinuxKernelCsSlot   2
#define LinuxKernelDsSlot   3
#define LinuxUserCsSlot     4
#define LinuxUserDsSlot     5
#endif

#define VMMonCsSlot        16
#define VMMonDsSlot        17
#define VMMonTssSlot       18

/* Under normal (no Linux-specific VM) mode, a macro to determine
 * if a particular guest GDT slot does not conflict with any monitor
 * GDT slots.
 */
#define OKGuestSlot(gdtSlot) \
    ( ((gdtSlot)!=0) && \
      (((gdtSlot)<VMMonCsSlot) || \
        (((gdtSlot)>VMMonTssSlot) && ((gdtSlot)<(MON_GDT_SIZE/8))) ) )

#define IsLinuxVMKernelSlot(gdtSlot) \
    ( (((gdtSlot)>=LinuxBootCsSlot)   && ((gdtSlot)<=LinuxBootDsSlot)) || \
      (((gdtSlot)>=LinuxKernelCsSlot) && ((gdtSlot)<=LinuxKernelDsSlot)) )



typedef struct {
  selector_t   sel;
  descriptor_t des;
  unsigned     valid;
  } __attribute__ ((packed)) guest_sreg_t;

#define SRegES    0
#define SRegCS    1
#define SRegSS    2
#define SRegDS    3
#define SRegFS    4
#define SRegGS    5

#define GenRegEAX 0
#define GenRegECX 1
#define GenRegEDX 2
#define GenRegEBX 3
#define GenRegESP 4
#define GenRegEBP 5
#define GenRegESI 6
#define GenRegEDI 7


#define ExceptionDE   0 /* Divide Error (fault) */
#define ExceptionDB   1 /* Debug (fault/trap) */
#define ExceptionBP   3 /* Breakpoint (trap) */
#define ExceptionOF   4 /* Overflow (trap) */
#define ExceptionBR   5 /* BOUND (fault) */
#define ExceptionUD   6
#define ExceptionNM   7
#define ExceptionDF   8
#define ExceptionTS  10
#define ExceptionNP  11
#define ExceptionSS  12
#define ExceptionGP  13
#define ExceptionPF  14
#define ExceptionMF  16
#define ExceptionAC  17

#define MSR_IA32_SYSENTER_CS    0x174
#define MSR_IA32_SYSENTER_ESP   0x175
#define MSR_IA32_SYSENTER_EIP   0x176

typedef struct {
  Bit32u cs;
  Bit32u eip;
  Bit32u esp;
  } sysEnter_t;

typedef union {
  Bit32u raw;
  struct {
    Bit32u pe:1;
    Bit32u mp:1;
    Bit32u em:1;
    Bit32u ts:1;
    Bit32u et:1;
    Bit32u ne:1;
    Bit32u R15_6:10;
    Bit32u wp:1;
    Bit32u R17:1;
    Bit32u am:1;
    Bit32u R28_19:10;
    Bit32u nw:1;
    Bit32u cd:1;
    Bit32u pg:1;
    } __attribute__ ((packed)) fields;
  } __attribute__ ((packed)) cr0_t;

typedef union {
  Bit32u raw;
  struct {
    Bit32u vme:1;
    Bit32u pvi:1;
    Bit32u tsd:1;
    Bit32u de:1;
    Bit32u pse:1;
    Bit32u pae:1;
    Bit32u mce:1;
    Bit32u pge:1;
    Bit32u pce:1;
    Bit32u reserved:23;
    } __attribute__ ((packed)) fields;
  } __attribute__ ((packed)) cr4_t;

typedef struct {
  Bit32u vendorDWord0;
  Bit32u vendorDWord1;
  Bit32u vendorDWord2;
  union {
    Bit32u raw;
    struct {
      Bit32u stepping:4;
      Bit32u model:4;
      Bit32u family:4;
      Bit32u procType:2;
      Bit32u Reserved31_14:18;
      } __attribute__ ((packed)) fields;
    } __attribute__ ((packed)) procSignature;
  union {
    Bit32u raw;
    struct {
      Bit32u fpu:1;
      Bit32u vme:1;
      Bit32u de:1;
      Bit32u pse:1;
      Bit32u tsc:1;
      Bit32u msr:1;
      Bit32u pae:1;
      Bit32u mce:1;
      Bit32u cx8:1;
      Bit32u apic:1;
      Bit32u Reserved10:1;
      Bit32u sep:1;
      Bit32u mtrr:1;
      Bit32u pge:1;
      Bit32u mca:1;
      Bit32u cmov:1;
      Bit32u pat:1;
      Bit32u pse36:1;
      Bit32u Reserved22_18:5;
      Bit32u mmx:1;
      Bit32u fxsr:1;
      Bit32u Reserved31_25:7;
      } __attribute__ ((packed)) fields;
    } __attribute__ ((packed)) featureFlags;
  } __attribute__ ((packed)) cpuid_info_t;

typedef struct {
  Bit32u genReg[8]; /* EAX, ECX, ... */
  Bit32u eflags;
  Bit32u eip;
  guest_sreg_t sreg[6];
  guest_sreg_t ldtr;
  guest_sreg_t tr;
  gdt_info_t gdtr;
  gdt_info_t idtr;
  Bit32u dr0, dr1, dr2, dr3, dr6, dr7;
  Bit32u tr3, tr4, tr5, tr6, tr7;
  cr0_t cr0;
  Bit32u cr1, cr2, cr3;
  cr4_t cr4;
  unsigned INTR;

  sysEnter_t   sysEnter;
  Bit64u       tsc;
  volatile int halIrq;
  } __attribute__ ((packed)) guest_cpu_t;


typedef Bit32u phyAddr_t;




/*
 *  ioctl() names.
 */

#if defined(__linux__) || defined(__NetBSD__) || defined(__FreeBSD__)
#ifdef __linux__
#include <asm/ioctl.h>
#else
#include <sys/ioccom.h>
#endif
#define PLEX86_RESET         _IO('k', 3)
#define PLEX86_TEARDOWN      _IO('k', 4)
#define PLEX86_EXECUTE       _IOWR('k', 5, plex86IoctlExecute_t)
#define PLEX86_CPUID         _IOW('k', 6, cpuid_info_t)
#define PLEX86_REGISTER_MEMORY _IOW('k', 7, plex86IoctlRegisterMem_t)
#define PLEX86_UNREGISTER_MEMORY _IO('k', 8)
#define PLEX86_LINUX_VM_MODE _IO('k', 9)
#else
#define PLEX86_RESET         0x6b03
#define PLEX86_TEARDOWN      0x6b04
#define PLEX86_EXECUTE       0x6b05
#define PLEX86_CPUID         0x6b06
#define PLEX86_REGISTER_MEMORY 0x6b07
#define PLEX86_UNREGISTER_MEMORY 0x6b08
#define PLEX86_LINUX_VM_MODE 0x6b09
#endif

/* Reasons why plex86 could not execute the guest context in the VM. */
#define Plex86NoExecute_Method    1
#define Plex86NoExecute_CR0       2
#define Plex86NoExecute_CR4       3
#define Plex86NoExecute_CS        4

#define Plex86NoExecute_Selector  6
#define Plex86NoExecute_DPL       7
#define Plex86NoExecute_EFlags    8
#define Plex86NoExecute_Panic     9
#define Plex86NoExecute_VMState  10


/* Requests that the VM monitor makes to host-kernel space or
 * host-user space.
 */
#define MonReqNone              0
#define MonReqFlushPrintBuf     1
#define MonReqRedirect          4 /* Only to host-kernel. */
#define MonReqRemapMonitor      5
#define MonReqGuestFault        6
#define MonReqPinUserPage       7
#define MonReqPanic             8
#define MonReqHalCall           9
#define MonReqCyclesUpdate     10
#define MonReqBogus            11 // Fixme

#define VMStateFDOpened               0x001
#define VMStateMemAllocated           0x002
#define VMStateGuestCPUID             0x004
#define VMStateRegisteredPhyMem       0x008
#define VMStateRegisteredPrintBuffer  0x010
#define VMStateRegisteredGuestCPU     0x020
#define VMStateInitMonitor            0x040
#define VMStateMapMonitor             0x080
#define VMStatePanic                  0x100

  /* State where the VM/monitor is ready to execute. */
#define VMStateReady (VMStateFDOpened | \
                      VMStateMemAllocated | \
                      VMStateGuestCPUID | \
                      VMStateRegisteredPhyMem | \
                      VMStateRegisteredPrintBuffer | \
                      VMStateRegisteredGuestCPU | \
                      VMStateInitMonitor | \
                      VMStateMapMonitor)

  /* State where all user-space memory constructs are registered with
   * the plex86 kernel module.
   */
#define VMStateRegisteredAll \
                     (VMStateRegisteredPhyMem | \
                      VMStateRegisteredPrintBuffer | \
                      VMStateRegisteredGuestCPU)

typedef struct {
  unsigned state;
  unsigned request;
  unsigned guestFaultNo;
  unsigned guestFaultError;
  } plex86MonitorState_t;

typedef struct {
#define Plex86ExecuteMethodNative       10
#define Plex86ExecuteMethodBreakpoint   11
  unsigned executeMethod;

  /* User space --> Monitor space. */
  Bit64u               cyclesRequested;
  unsigned             instructionsRequested;

  /* Monitor space --> User space. */
  Bit64u               cyclesExecuted;
  unsigned             instructionsExecuted;
  plex86MonitorState_t monitorState;
  } plex86IoctlExecute_t;

typedef struct {
  unsigned nMegs;
  Bit32u   guestPhyMemVector;

  Bit32u   logBufferWindow;
  Bit32u   guestCPUWindow;
  } plex86IoctlRegisterMem_t;

#endif  /* #ifndef __PLEX86_H__ */
