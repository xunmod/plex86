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
#include "x86-emu.h"


// NOTES:
//   Updating segment descriptor A bits upon descriptor load.
//   Clean assertRF passed to doInterrupt()
//   Check that all uses of translateLinToPhy() check for LinAddrNotAvailable


// ===================
// Defines / typesdefs
// ===================

// Flags to pass to doInterrupt()
#define IntFlagPushError 1
#define IntFlagAssertRF  2
#define IntFlagSoftInt   4

#define CPL plex86GuestCPU->sreg[SRegCS].des.dpl

typedef struct {
  Bit32u   addr; // The resolved segment offset after calculation.

  unsigned mod;
  unsigned nnn;
  unsigned rm;

  unsigned base;
  unsigned index;
  unsigned scale;
  } modRM_t;


// ===================
// Function prototypes
// ===================

static unsigned  emulateSysInstr(void);
static unsigned  setCRx(unsigned crx, Bit32u val32);
static phyAddr_t translateLinToPhy(Bit32u lAddr);
static unsigned  decodeModRM(Bit8u *opcodePtr, modRM_t *modRMPtr);


// =========
// Variables
// =========

static const unsigned dataSReg[4] = { SRegES, SRegDS, SRegFS, SRegGS };
//static descriptor_t nullDesc = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const phyAddr_t LinAddrNotAvailable = -1;



  unsigned
doGuestFault(unsigned fault, unsigned errorCode)
{
  switch (fault) {

    case ExceptionUD:
    case ExceptionGP:
      if (plex86GuestCPU->sreg[SRegCS].sel.fields.rpl==0) {
// This fault actually should assert RF.
        if (emulateSysInstr()) {
          // Could speculatively execute until a non protection-level
          // change system instruction is encounted to eliminate some
          // monitor faults.
          return 1;
          }
        fprintf(stderr, "#UD/#GP (CPL==0).\n");
        return(0);
        }
      else {
        fprintf(stderr, "#UD/#GP (CPL!=0).\n");
        //emulateUserInstr();
        //return 1;
        return 0;
        }

    case ExceptionNM:
      if (plex86GuestCPU->sreg[SRegCS].sel.fields.rpl==0) {
// This fault actually should assert RF.
        if (emulateSysInstr()) {
          // Could speculatively execute until a non protection-level
          // change system instruction is encounted to eliminate some
          // monitor faults.
          return 1;
          }
        }
      else {
        // #NM from user code - invoke kernel #NM handler.
fprintf(stderr, "#NM (CPL!=0).\n");
//doInterrupt(ExceptionNM, IntFlagAssertRF, 0);
//emulateUserInstr();
        //return 1;
        return 0;
        }
      fprintf(stderr, "#NM (CPL!=0).\n");
      return(0);

    default:
      fprintf(stderr, "doGuestFault: unhandled exception %u.\n", fault);
      return(0);
    }
}

  phyAddr_t
translateLinToPhy(Bit32u lAddr)
{
  phyAddr_t pdeAddr, pteAddr, ppf, pAddr;
  Bit32u lpf, pde, pte;
  unsigned pOffset;

  if ( !plex86GuestCPU->cr0.fields.pg )
    return(lAddr); // Paging off, just return linear address as physical.

  // CR4.{PAE,PSE} unsupported.
  if ( plex86GuestCPU->cr4.raw & 0x00000030 ) {
    return(LinAddrNotAvailable);
    }
  lpf       = lAddr & 0xfffff000; // Linear page frame.
  pOffset   = lAddr & 0x00000fff; // Page offset.

  // 1st level lookup.
  pdeAddr = (plex86GuestCPU->cr3 & 0xfffff000) | ((lAddr & 0xffc00000) >> 20);
  if (pdeAddr >= plex86MemSize) {
    return(LinAddrNotAvailable); // Out of physical memory bounds.
    }
  pde = * (Bit32u *) &plex86MemPtr[pdeAddr];
  if ( !(pde & 0x01) ) {
    return(LinAddrNotAvailable); // PDE.P==0 (page table not present)
    }

  // 2nd level lookup.
  pteAddr = (pde & 0xfffff000) | ((lAddr & 0x003ff000) >> 10);
  if (pteAddr >= plex86MemSize) {
    return(LinAddrNotAvailable); // Out of physical memory bounds.
    }
  pte = * (Bit32u *) &plex86MemPtr[pteAddr];
  if ( !(pte & 0x01) ) {
    return(LinAddrNotAvailable); // PTE.P==0 (page not present)
    }

  // Make up the physical page frame address.
  ppf   = pte & 0xfffff000;
  pAddr = ppf | pOffset;
  return(pAddr);
}

  unsigned
emulateSysInstr(void)
{
  phyAddr_t pAddr;
  unsigned b0, iLen=0;
  Bit8u    opcode[16];
  Bit32u  *opcodeDWords;
  modRM_t modRM;
  unsigned opsize = 1; // 32-bit opsize by default.

  if (plex86GuestCPU->cr0.fields.pg) {
    Bit32u    lAddr;
    unsigned  pageOffset;

    lAddr = plex86GuestCPU->eip; // Forget segmentation base for Linux.
    pAddr = translateLinToPhy(lAddr);
    if (pAddr == -1) {
      fprintf(stderr, "emulateSysInstr: lin-->phy translation failed (0x%x).\n",
              lAddr);
      return 0; // Fail.
      }
    if ( pAddr >= (plex86MemSize-16) ) {
      fprintf(stderr, "emulateSysInstr: physical address of 0x%x "
              "beyond memory.\n", pAddr);
      return 0; // Fail.
      }
    pageOffset = pAddr & 0xfff;
    if ( pageOffset <= 0xff0 ) {
      // Draw in 16 bytes from guest memory.  No boundary checks are
      // necessary since paging is off and we can't be overrunning
      // the physical memory bounds.
      opcodeDWords = (Bit32u*) &opcode[0];
      opcodeDWords[0] = * (Bit32u*) &plex86MemPtr[pAddr+ 0];
      opcodeDWords[1] = * (Bit32u*) &plex86MemPtr[pAddr+ 4];
      opcodeDWords[2] = * (Bit32u*) &plex86MemPtr[pAddr+ 8];
      opcodeDWords[3] = * (Bit32u*) &plex86MemPtr[pAddr+12];
      }
    else {
      phyAddr_t page1PAddr;
      unsigned i, page0Count, page1Count;

      page0Count = 0x1000 - pageOffset;
      page1Count = 16 - page0Count;
      // Fetch remaining bytes from this page.
      for (i=0; i<page0Count; i++) {
        opcode[i] = plex86MemPtr[pAddr + i];
        }
      // Translate physical address of following page.
      page1PAddr = translateLinToPhy((lAddr | 0xfff) + 1);
      if ( page1PAddr == LinAddrNotAvailable ) {
        fprintf(stderr, "emulateSysInstr: lin-->phy translation failed (0x%x).\n",
                (lAddr | 0xfff) + 1);
        return 0; // Fail.
        }
      // Fetch residual bytes from following page.
      for (i=0; i<page1Count; i++) {
        opcode[page0Count + i] = plex86MemPtr[page1PAddr + i];
        }
      }
    }
  else {
    pAddr = plex86GuestCPU->eip; // Forget segmentation base for Linux.
    if ( pAddr >= (plex86MemSize-16) ) {
      fprintf(stderr, "emulateSysInstr: physical address of 0x%x "
              "beyond memory.\n", pAddr);
      return 0; // Fail.
      }
    // Draw in 16 bytes from guest memory.  No boundary checks are
    // necessary since paging is off and the check above makes sure
    // we don't overrun the physical memory bounds.
    opcodeDWords = (Bit32u*) &opcode[0];
    opcodeDWords[0] = * (Bit32u*) &plex86MemPtr[pAddr+ 0];
    opcodeDWords[1] = * (Bit32u*) &plex86MemPtr[pAddr+ 4];
    opcodeDWords[2] = * (Bit32u*) &plex86MemPtr[pAddr+ 8];
    opcodeDWords[3] = * (Bit32u*) &plex86MemPtr[pAddr+12];
    }

decodeOpcode:

  b0 = opcode[iLen++];
  // fprintf(stderr, "instruction @ addr=0x%x is 0x%x\n", pAddr, b0);
  switch (b0) {
    case 0x0f: // 2-byte escape.
      {
      unsigned b1;
      b1 = opcode[iLen++];
      switch (b1) {

        case 0x22: // MOV_CdRd
          {
          Bit32u   val32;
          iLen += decodeModRM(&opcode[iLen], &modRM);
          if ( modRM.mod!=3 ) {
            fprintf(stderr, "MOV_CdRd: mod field not 11b.\n");
            return 0;
            }
          val32 = plex86GuestCPU->genReg[modRM.rm];
          if ( setCRx(modRM.nnn, val32) )
            goto advanceInstruction;
          return 0;
          }

        default:
          fprintf(stderr, "emulateSysInstr: default b1=0x%x\n", b1);
          break;
        }
      break;
      }

    case 0x66: // Opsize:
      {
      if (iLen == 1) { // Prevent endless string of prefixes doing overrun.
        opsize = 0; // 16-bit opsize.
        goto decodeOpcode;
        }
      return 0;
      }

    case 0xcd: // int Ib
      {
      unsigned Ib;

      Ib = opcode[iLen++];
      plex86GuestCPU->eip += iLen; // Commit instruction length.
      if (Ib == 0xff) {
        // This is the special "int $0xff" call for interfacing with the HAL.
        halCall();
        return 1; // OK
        }
      return 0; // Fail. (normal int handled in monitor space now)
      }

    case 0xf0: // LOCK (used to make IRET fault).
      {
      if (iLen == 1) // Prevent endless string of prefixes doing overrun.
        goto decodeOpcode;
      return 0;
      }

    default:
      fprintf(stderr, "emulateSysInstr: default b0=0x%x\n", b0);
      return 0;
    }
  return 0; // Fail.

advanceInstruction:
  plex86GuestCPU->eip += iLen;

  return 1; // OK.
}


  unsigned
setCRx(unsigned crx, Bit32u val32)
{
  //fprintf(stderr, "setCR%u: val=0x%x.\n", crx, val32);

  switch ( crx ) {
#if 0
  // From bochs code.
  switch (i->nnn()) {
    case 0: // CR0 (MSW)
      // BX_INFO(("MOV_CdRd:CR0: R32 = %08x @CS:EIP %04x:%04x ",
      //   (unsigned) val_32,
      //   (unsigned) BX_CPU_THIS_PTR sregs[BX_SEG_REG_CS].selector.value,
      //   (unsigned) EIP));
      SetCR0(val_32);
      break;

    case 1: /* CR1 */
      BX_PANIC(("MOV_CdRd: CR1 not implemented yet"));
      break;
    case 2: /* CR2 */
      BX_DEBUG(("MOV_CdRd: CR2 not implemented yet"));
      BX_DEBUG(("MOV_CdRd: CR2 = reg"));
      BX_CPU_THIS_PTR cr2 = val_32;
      break;
    case 3: // CR3
      if (bx_dbg.creg)
        BX_INFO(("MOV_CdRd:CR3 = %08x", (unsigned) val_32));
      // Reserved bits take on value of MOV instruction
      CR3_change(val_32);
      BX_INSTR_TLB_CNTRL(CPU_ID, BX_INSTR_MOV_CR3, val_32);
      // Reload of CR3 always serializes.
      // invalidate_prefetch_q(); // Already done.
      break;
    }
#endif
    case 0:
      {
      plex86GuestCPU->cr0.raw = val32;
      if ( !plex86GuestCPU->cr0.fields.pe ) {
        fprintf(stderr, "setCR0: PE=0.\n");
        return 0; // Fail.
        }
      // Fixme: have to notify the monitor of changes.
      return 1; // OK.
      }

    // case 1:
    // case 2:

    case 3:
      {
      plex86GuestCPU->cr3 = val32;
      // Fixme: have to notify the monitor of paging remap here.
      return 1; // OK.
      }

    case 4:
      {
      //   [31-11] Reserved, Must be Zero
      //   [10]    OSXMMEXCPT: Operating System Unmasked Exception Support R/W
      //   [9]     OSFXSR: Operating System FXSAVE/FXRSTOR Support R/W
      //   [8]     PCE: Performance-Monitoring Counter Enable R/W
      //   [7]     PGE: Page-Global Enable R/W
      //   [6]     MCE: Machine Check Enable R/W
      //   [5]     PAE: Physical-Address Extension R/W
      //   [4]     PSE: Page Size Extensions R/W
      //   [3]     DE: Debugging Extensions R/W
      //   [2]     TSD: Time Stamp Disable R/W
      //   [1]     PVI: Protected-Mode Virtual Interrupts R/W
      //   [0]     VME: Virtual-8086 Mode Extensions R/W

      Bit32u allowMask = 0;
      //Bit32u oldCR4 = plex86GuestCPU->cr4.raw;


      //if (guestCPUID.featureFlags.fields.vme)
      //  allowMask |= ((1<<1) | (1<<0)); // PVI
      if (guestCPUID.featureFlags.fields.pge)
        allowMask |= (1<<7);

      // X86-64 has some behaviour with msr.lme / cr4.pae.

      if (val32 & ~allowMask) {
         fprintf(stderr, "SetCR4: write of 0x%08x unsupported (allowMask=0x%x).",
             val32, allowMask);
         return 0; // Fail.
         }
      val32 &= allowMask; // Screen out unsupported bits for good meassure.
      plex86GuestCPU->cr4.raw = val32;
      // Fixme: have to notify the monitor of paging remap here.
      return 1; // OK.
      }

    default:
      fprintf(stderr, "setCRx: reg=%u, val=0x%x.\n", crx, val32);
      return 0; // Fail.
    }
  return 0; // Fail.
}

  unsigned
decodeModRM(Bit8u *opcodePtr, modRM_t *modRMPtr)
{
  unsigned modrm = opcodePtr[0];
  unsigned len = 1; // Length of entire modrm sequence

  modRMPtr->mod = modrm >> 6;
  modRMPtr->nnn = (modrm>>3) & 7;
  modRMPtr->rm  = modrm & 7;

  if (modRMPtr->mod == 3)
    return(len); // Done, 1 byte of decode info processed.

  if (modRMPtr->rm != 4) {
    // No s-i-b byte follows.
    if (modRMPtr->mod == 0) {
      if (modRMPtr->rm == 5) {
        // A dword immediate address follows.  No registers are used
        // in the address calculation.
        modRMPtr->addr = * (Bit32u*) &opcodePtr[1];
        len += 4;
        return(len);
        }
      modRMPtr->addr = plex86GuestCPU->genReg[modRMPtr->rm];
      return(len);
      }
    else if (modRMPtr->mod == 1) {
      fprintf(stderr, "decodeModRM: no sib, mod=1.\n");
      plex86TearDown(); exit(1);
      }
    else { // (modRMPtr->mod == 2)
      fprintf(stderr, "decodeModRM: no sib, mod=2.\n");
      plex86TearDown(); exit(1);
      }
    }
  else {
    // s-i-b byte follows.
    unsigned sib, scaledIndex;
    sib = opcodePtr[1];
    len++;
    fprintf(stderr, "decodeModRM: sib byte follows.\n");
    modRMPtr->base  = (sib & 7);
    modRMPtr->index = (sib >> 3) & 7;
    modRMPtr->scale = (sib >> 6);
    if ( modRMPtr->mod == 0 ) {
      if ( modRMPtr->base == 5 ) {
        // get 32 bit displ + base?
        fprintf(stderr, "decodeModRM: sib: mod=%u, base=%u.\n",
                modRMPtr->mod, modRMPtr->base);
        plex86TearDown(); exit(1);
        }
      fprintf(stderr, "decodeModRM: sib: mod=%u, base=%u.\n",
              modRMPtr->mod, modRMPtr->base);
      if ( modRMPtr->index != 4 )
        scaledIndex = plex86GuestCPU->genReg[modRMPtr->index] << modRMPtr->scale;
      else
        scaledIndex = 0;
      modRMPtr->addr = plex86GuestCPU->genReg[modRMPtr->base] + scaledIndex;
      return(len);
      }
    else if ( modRMPtr->mod == 1 ) {
      // get 8 bit displ
      fprintf(stderr, "decodeModRM: sib: mod=%u, base=%u.\n",
              modRMPtr->mod, modRMPtr->base);
      plex86TearDown(); exit(1);
      }
    else { // ( modRMPtr->mod == 2 )
      // get 32 bit displ
      fprintf(stderr, "decodeModRM: sib: mod=%u, base=%u.\n",
              modRMPtr->mod, modRMPtr->base);
      plex86TearDown(); exit(1);
      }
    plex86TearDown(); exit(1);
    }

  return(0);
}
