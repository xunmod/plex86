/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton


#include "plex86.h"
#define IN_MONITOR_SPACE
#include "monitor.h"


// NOTES:
//   Updating segment descriptor A bits upon descriptor load.
//   Clean assertRF passed to doGuestInterrupt()
//   Check that all uses of translateLinToPhy() check for LinAddrNotAvailable
//   Make sure that linear accesses modify guest page table A&D bits properly.
//   Change guest_cpu_t and others to guestCpu_t
//   Need checks for 32-bit-span base/limit everywhere segments are reloaded.


// ===================
// Defines / typesdefs
// ===================

// Flags to pass to doGuestInterrupt()
#define IntFlagPushError 1
#define IntFlagAssertRF  2
#define IntFlagSoftInt   4

#define FetchDescUpdateBusyBit 1

#define EIP vm->guest.addr.guestStackContext->eip
#define CPL vm->guestCPL

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

static unsigned  emulateSysInstr(vm_t *);
static void      emulateUserInstr(vm_t *);
//static unsigned  setCRx(vm_t *, unsigned crx, Bit32u val32);
static unsigned  setDRx(vm_t *, unsigned drx, Bit32u val32);
static phyAddr_t translateLinToPhy(vm_t *, Bit32u lAddr);
static unsigned  decodeModRM(vm_t *, Bit8u *opcodePtr, modRM_t *modRMPtr);
static unsigned  loadGuestSegment(vm_t *, unsigned sreg, selector_t selector);
static void      getObjectLAddr(vm_t *vm, Bit8u *obj, Bit32u lAddr0,
                                unsigned len);
static Bit32u    getGuestDWord(vm_t *, Bit32u lAddr);
static Bit16u    getGuestWord(vm_t *, Bit32u lAddr);
static void      writeGuestDWord(vm_t *vm, Bit32u lAddr, Bit32u val);
static descriptor_t *fetchGuestDescBySel(vm_t *, selector_t, descriptor_t *,
                                         unsigned flags);
static descriptor_t *fetchGuestDescByLAddr(vm_t *, Bit32u laddr,
                                           descriptor_t *desc);
static unsigned  inp(vm_t *, unsigned iolen, unsigned port);
static void      outp(vm_t *, unsigned iolen, unsigned port, unsigned val);

//static void      doIRET(void);
static unsigned  doWRMSR(vm_t *vm);

static void guestSelectorUpdated(vm_t *vm, unsigned segno, selector_t selector);

static inline void loadCR4(Bit32u newCR4)
{
  __asm__ volatile (
    "movl %0, %%cr4"
      : /* No outputs. */
      : "r" (newCR4)
    );
}

#define PageSize 4096

typedef struct {
  Bit32u low;
  Bit32u high;
  } __attribute__ ((packed)) gdt_entry_t ;



// =========
// Variables
// =========


static const unsigned dataSReg[4] = { SRegES, SRegDS, SRegFS, SRegGS };
//static descriptor_t nullDesc = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const phyAddr_t LinAddrNotAvailable = -1;

#warning "move guestStackGenRegOffsets and add some sanity checks"
static const int guestStackGenRegOffsets[8] = {
  44, /* EAX */
  40, /* ECX */
  36, /* EDX */
  32, /* EBX */
  68, /* ESP (note ESP is pushed by the int/exception, not PUSHA) */
  24, /* EBP */
  20, /* ESI */
  16  /* EDI */
  };
#define GenReg(vm, regno) \
  * (Bit32u*) \
  (((Bit8u*) vm->guest.addr.guestStackContext)+guestStackGenRegOffsets[regno])


  void
doGuestFault(vm_t *vm, unsigned fault, unsigned errorCode)
{
//monprint(vm, "doGF(%u)\n", fault);

  switch (fault) {

    case ExceptionUD:
    case ExceptionGP:
      if ( CPL==0 ) {
#if 1
// This fault actually should assert RF.
        if ( emulateSysInstr(vm) ) {
          // Could speculatively execute until a non protection-level
          // change system instruction is encounted to eliminate some
          // monitor faults.
          return;
          }
        toHostGuestFault(vm, fault, errorCode);
#else
        // For now, defer back to the user space implementation until
        // the monitor space code is filled out.
        toHostGuestFault(vm, fault, errorCode);
#endif
        }
      else {
        //monprint(vm, "#UD/#GP (CPL!=0).\n");
        emulateUserInstr(vm);
        }
      break;

    case ExceptionNM:
      if ( CPL==0 ) {
// Fixme: This fault actually should assert RF.
        if ( emulateSysInstr(vm) ) {
          // Could speculatively execute until a non protection-level
          // change system instruction is encounted to eliminate some
          // monitor faults.
          return;
          }
        monpanic(vm, "ExceptionNM: emulateSysInstr failed.\n");
        }
      else {
        // #NM from user code - invoke kernel #NM handler.
//monprint(vm, "#NM (CPL!=0).\n"); // Fixme:
        doGuestInterrupt(vm, ExceptionNM, IntFlagAssertRF, 0);
        //emulateUserInstr(vm);
        }
      break;

    case ExceptionPF:
#warning "Fixme: errorCode needs to be corrected for pushed-down priv level."

      // If this Page Fault was from non-user code, we need to fix-up the
      // privilege level bit in the error code.  This really should be moved
      // to the VM monitor.
      if ( CPL!=3 ) {
        errorCode &= ~(1<<2);
        }
      doGuestInterrupt(vm, ExceptionPF,
                       IntFlagAssertRF | IntFlagPushError, errorCode);
      break;

    default:
      monpanic(vm, "doGuestFault: unhandled exception %u.\n", fault);
    }
}

  phyAddr_t
translateLinToPhy(vm_t *vm, Bit32u lAddr)
{
  phyAddr_t pdeAddr, pteAddr, ppf, pAddr;
  Bit32u lpf, pde, pte;
  unsigned pOffset;
  Bit8u   *phyPagePtr;
  guest_cpu_t *guestCpu = vm->guest.addr.guest_cpu;

  if ( ! guestCpu->cr0.fields.pg )
    return(lAddr); // Paging off, just return linear address as physical.

  // CR4.{PAE,PSE} unsupported.
  if ( guestCpu->cr4.raw & 0x00000030 ) {
    return(LinAddrNotAvailable);
    }
  lpf       = lAddr & 0xfffff000; // Linear page frame.
  pOffset   = lAddr & 0x00000fff; // Page offset.

  // 1st level lookup.
  pdeAddr = (guestCpu->cr3 & 0xfffff000) |
            ((lAddr & 0xffc00000) >> 20);
  if (pdeAddr >= vm->pages.guest_n_bytes) {
    return(LinAddrNotAvailable); // Out of physical memory bounds.
    }
  phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pdeAddr >> 12,
                            vm->guest.addr.tmp_phy_page0);
  pde = * (Bit32u *) &phyPagePtr[pdeAddr & 0xfff];
  if ( !(pde & 0x01) ) {
    return(LinAddrNotAvailable); // PDE.P==0 (page table not present)
    }

  // 2nd level lookup.
  pteAddr = (pde & 0xfffff000) | ((lAddr & 0x003ff000) >> 10);
  if (pteAddr >= vm->pages.guest_n_bytes) {
    return(LinAddrNotAvailable); // Out of physical memory bounds.
    }
  phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pteAddr >> 12,
                            vm->guest.addr.tmp_phy_page0);
  pte = * (Bit32u *) &phyPagePtr[pteAddr & 0xfff];
  if ( !(pte & 0x01) ) {
    return(LinAddrNotAvailable); // PTE.P==0 (page not present)
    }

  // Make up the physical page frame address.
  ppf   = pte & 0xfffff000;
  pAddr = ppf | pOffset;
  return(pAddr);
}

  unsigned
emulateSysInstr(vm_t *vm)
{
  unsigned b0, iLen=0;
  Bit8u    opcode[16];
  modRM_t modRM;
  unsigned opsize = 1; // 32-bit opsize by default.
  guest_cpu_t *guestCpu = vm->guest.addr.guest_cpu;
  guestStackContext_t *guestStackContext = vm->guest.addr.guestStackContext;

  getObjectLAddr(vm, opcode, EIP, 16);

decodeOpcode:

  b0 = opcode[iLen++];

//monprint(vm, "b0=0x%x\n", b0);

  // monprint(vm, "instruction @ addr=0x%x is 0x%x\n", pAddr, b0);
  switch (b0) {
    case 0x0f: // 2-byte escape.
      {
      unsigned b1;
      b1 = opcode[iLen++];
      switch (b1) {
        case 0x00: // Group6
          {
          iLen += decodeModRM(vm, &opcode[iLen], &modRM);
          monprint(vm, "emulateSysIntr: G6: nnn=%u\n", modRM.nnn);
          switch (modRM.nnn) {
            case 2: // LLDT_Ew
              {
              selector_t selector;

              if (modRM.mod==3)
                selector.raw = GenReg(vm, modRM.rm);
              else
                selector.raw = getGuestWord(vm, modRM.addr);
              if ( (selector.raw & 0xfffc) == 0 ) {
                guestCpu->ldtr.sel = selector;
                guestCpu->ldtr.valid = 0;
                goto advanceInstruction;
                }
              monprint(vm, "emulateSysIntr: LLDT: forcing selector null.\n");
              guestCpu->ldtr.sel.raw = 0; // Make it null anyways.
              guestCpu->ldtr.valid = 0;
              goto advanceInstruction;
              }

            case 3: // LTR_Ew
              {
              selector_t    tssSelector;
              descriptor_t  tssDesc, *tssDescPtr;
              Bit32u tssLimit;

              if (modRM.mod==3)
                tssSelector.raw = GenReg(vm, modRM.rm);
              else
                tssSelector.raw = getGuestWord(vm, modRM.addr);
              if ( (tssSelector.raw & 0xfffc) == 0 ) {
                guestCpu->tr.sel = tssSelector;
                guestCpu->tr.valid = 0;
                goto advanceInstruction;
                }
              if ( tssSelector.raw & 4 ) {
                monpanic(vm, "LTR_Ew: selector.ti=1.\n");
                return 0;
                }
              tssDescPtr = fetchGuestDescBySel(vm, tssSelector, &tssDesc,
                                               FetchDescUpdateBusyBit);
              if ( !tssDescPtr ) {
                monpanic(vm, "LTR_Ew: descriptor fetch failed.\n");
                return 0;
                }
              tssLimit = (tssDesc.limit_high<<16) |
                          tssDesc.limit_low;
              if (tssDesc.g)
                tssLimit = (tssLimit<<12) | 0xfff;
              if ( (tssDesc.p==0) ||
                   (tssDesc.type!=9) ||
                   (tssLimit<103) ) {
                monpanic(vm, "LTR_Ew: bad descriptor.\n");
                return 0;
                }
              guestCpu->tr.sel = tssSelector;
              guestCpu->tr.des = tssDesc;
              guestCpu->tr.des.type |= 2; // Set busy bit.
              guestCpu->tr.valid = 1;
              goto advanceInstruction;
              }

            default:
            }
          return 0;
          }

        case 0x01: // Group7
          {
          iLen += decodeModRM(vm, &opcode[iLen], &modRM);
// monprint(vm, "emulateSysIntr: G7: nnn=%u\n", modRM.nnn);
          switch (modRM.nnn) {
            case 2: // LGDT_Ms
              {
              Bit32u   base32;
              Bit16u   limit16;

              if (modRM.mod==3) { // Must be a memory reference.
                monpanic(vm, "LGDT_Ms: mod=3.\n");
                }
              limit16 = getGuestWord(vm, modRM.addr);
              base32  = getGuestDWord(vm, modRM.addr+2);
              guestCpu->gdtr.base  = base32;
              guestCpu->gdtr.limit = limit16;
monprint(vm, "GDTR.limit = 0x%x.\n", limit16);
              goto advanceInstruction;
              }

            case 3: // LIDT_Ms
              {
              Bit32u   base32;
              Bit16u   limit16;

              if (modRM.mod==3) { // Must be a memory reference.
                monpanic(vm, "LIDT_Ms: mod=3.\n");
                return 0;
                }
              limit16 = getGuestWord(vm, modRM.addr);
              base32  = getGuestDWord(vm, modRM.addr+2);
              guestCpu->idtr.base  = base32;
              guestCpu->idtr.limit = limit16;
              goto advanceInstruction;
              }

            case 7: // INVLPG
              {
              if (modRM.mod==3) { // Must be a memory reference.
                monprint(vm, "INVLPG: mod=3.\n");
                return 0;
                }
              invlpg_mon_offset( Guest2Monitor(vm, modRM.addr) );
              goto advanceInstruction;
              }
            }
          return 0;
          }

        case 0x06: // CLTS
          {
          // Fixme: when we support FPU ops, we need to sync the monitor
          // Fixme: cr0 value (nexus->mon_cr0) and reload it here.
          guestCpu->cr0.fields.ts = 0;
          goto advanceInstruction;
          }

        case 0x20: // MOV_RdCd
          {
          unsigned modrm, rm, nnn;
          Bit32u   val32;

// Fixme: use decodeModRM()
          modrm = opcode[iLen++];
          rm   = modrm & 7;
          nnn  = (modrm>>3) & 7;
          if ( (modrm & 0xc0) != 0xc0 ) {
            monpanic(vm, "MOV_RdCd: mod field not 11b.\n");
            return 0;
            }
          switch (nnn) {
            case 0: val32 = guestCpu->cr0.raw; break;
            case 2: val32 = guestCpu->cr2;     break;
            case 3: val32 = guestCpu->cr3;     break;
            case 4: val32 = guestCpu->cr4.raw; break;
            default:
              monpanic(vm, "MOV_RdCd: cr%u?\n", nnn);
              return 0;
            }
          GenReg(vm, rm) = val32;
          goto advanceInstruction;
          }

        case 0x22: // MOV_CdRd
          {
return 0; // defer to user space implementation for now.
#if 0
          Bit32u   val32;
          iLen += decodeModRM(vm, &opcode[iLen], &modRM);
          if ( modRM.mod!=3 ) {
            monpanic(vm, "MOV_CdRd: mod field not 11b.\n");
            return 0;
            }
          val32 = GenReg(vm, modRM.rm);
          if ( setCRx(vm, modRM.nnn, val32) )
            goto advanceInstruction;
          return 0;
#endif
          }


        case 0x23: // MOV_DdRd
          {
          Bit32u   val32;
          iLen += decodeModRM(vm, &opcode[iLen], &modRM);
          if ( modRM.mod!=3 ) {
            monpanic(vm, "MOV_DdRd: mod field not 3.\n");
            return 0;
            }
          val32 = GenReg(vm, modRM.rm);
          if ( setDRx(vm, modRM.nnn, val32) )
            goto advanceInstruction;
          return 0;
          }

        case 0x30: // WRMSR
          {
          if ( doWRMSR(vm) )
            goto advanceInstruction;
          return 0;
          }

        case 0x31: // RDTSC
          {
          //monprint(vm, "RDTSC.\n");
          GenReg(vm, GenRegEAX) = (Bit32u) guestCpu->tsc;
          GenReg(vm, GenRegEDX) = guestCpu->tsc>>32;
          guestCpu->tsc += 10; // Fixme: hack for now.
          goto advanceInstruction;
          }

        case 0xb2: // LSS_GvMp
          {
          Bit32u   reg32;
          selector_t selector;

          iLen += decodeModRM(vm, &opcode[iLen], &modRM);
          if (modRM.mod==3) { // Must be a memory reference.
            monpanic(vm, "LSS_GvMp: mod=3.\n");
            return 0;
            }
          reg32        = getGuestDWord(vm, modRM.addr);
          selector.raw = getGuestWord(vm, modRM.addr+4);
          if ( loadGuestSegment(vm, SRegSS, selector) ) {
            GenReg(vm, modRM.nnn) = reg32;
            goto advanceInstruction;
            }
          return 0;
          }

        default:
          monpanic(vm, "emulateSysInstr: default b1=0x%x\n", b1);
          break;
        }
      break;
      }

    case 0x1f: // POP DS
      {
      selector_t selector;

      if ( opsize == 0 ) {
        monpanic(vm, "emulateSysInstr: POP DS, 16-bit opsize.\n");
        return 0;
        }
      selector.raw = getGuestWord(vm, GenReg(vm, GenRegESP));
      if ( loadGuestSegment(vm, SRegDS, selector) ) {
        GenReg(vm, GenRegESP) += 4;
        goto advanceInstruction;
        }
      return 0;
      }

    case 0x66: // Opsize:
      {
      if (iLen == 1) { // Prevent endless string of prefixes doing overrun.
        opsize = 0; // 16-bit opsize.
        goto decodeOpcode;
        }
      return 0;
      }

    case 0x8e: // MOV_SwEw
      {
      unsigned modrm, rm, sreg;
      selector_t selector;

// Fixme: use decodeModRM()
      modrm = opcode[iLen++];
      rm   = modrm & 7;
      sreg = (modrm>>3) & 7;
      if ( (sreg==SRegCS) || (sreg>SRegGS) ) {
        monpanic(vm, "emulateSysInstr: MOV_SwEw bad sreg=%u.\n", sreg);
        return 0; // Fail.
        }
      if ( (modrm & 0xc0) == 0xc0 ) {
        selector.raw = GenReg(vm, rm);
        if (loadGuestSegment(vm, sreg, selector))
          goto advanceInstruction;
        }
      else {
        monpanic(vm, "emulateSysInstr: MOV_SwEw mod!=11b.\n");
        return 0; // Fail.
        }
      break;
      }

    case 0x9b: // FWAIT
      {
      if (iLen == 1) // Prevent endless string of prefixes doing overrun.
        goto decodeOpcode;
      return 0;
      }

    case 0xcd: // int Ib
      {
      unsigned Ib;

      Ib = opcode[iLen++];
      if (Ib == 0xff) {
        // This is the special "int $0xff" call for interfacing with the HAL.
return 0; // defer to user space implementation for now.
#if 0
        EIP += iLen; // Commit instruction length.
        halCall();
        return 1; // OK
#endif
        }
      else {
        EIP += iLen; // Commit instruction length.
        doGuestInterrupt(vm, Ib, IntFlagSoftInt, 0);
        return 1; // OK
        }
      }

    case 0xcf: // IRET
      {
return 0; // defer to user space implementation for now.
#if 0
      doIRET();
      return 1; // OK
#endif
      }

    case 0xdb: // ESC3 (floating point)
      {
      unsigned modrm;
      modrm = opcode[iLen++];
      if (modrm == 0xe3) {
        // FNINIT (Fixme: ignored)
        goto advanceInstruction;
        }
      if (modrm == 0xe4) {
        // FSETPM (essentially a fnop)
        goto advanceInstruction;
        }
      monprint(vm, "ESC3: next=0x%x unsupported.\n", modrm);
      return 0;
      }

    case 0xdf: // ESC7 (floating point)
      {
      unsigned modrm;
      modrm = opcode[iLen++];
      if (modrm == 0xe0) {
        // F(N)STSW AX : (Fixme, faked out to think there is no FPU for now)
        guestStackContext->eax = 0x0000ffff;
        // guestStackContext->eax = 0; // FPU exists.
        goto advanceInstruction;
        }
      monprint(vm, "ESC7: next=0x%x unsupported.\n", modrm);
      return 0;
      }

    case 0xe4: // IN_ALIb
      {
      unsigned port8;
      Bit8u    al;

      port8 = opcode[iLen++];
      al = inp(vm, 1, port8);
      guestStackContext->eax &= ~0xff;
      guestStackContext->eax |= al;
      goto advanceInstruction;
      }

    case 0xe6: // OUT_IbAL
      {
      unsigned port8;

      port8 = opcode[iLen++];
      outp(vm, 1, port8, guestStackContext->eax & 0xff);
      goto advanceInstruction;
      }

    case 0xea: // JMP_Ap (IdIw)
      {
      Bit32u offset;
      selector_t cs;

      offset = * (Bit32u*) &opcode[iLen];
      cs.raw = * (Bit16u*) &opcode[iLen+4];
      iLen += 6;
      if ( loadGuestSegment(vm, SRegCS, cs) ) {
        EIP = offset;
        return 1; // OK.
        }
      monpanic(vm, "JMP_Ap:\n");
      return 0;
      }

    case 0xec: // IN_ALDX
      {
      unsigned dx;
      Bit8u    al;

      dx = guestStackContext->edx & 0xffff;
      al = inp(vm, 1, dx);
      guestStackContext->eax &= ~0xff;
      guestStackContext->eax |= al;
      goto advanceInstruction;
      }

    case 0xee: // OUT_DXAL
      {
      unsigned dx, al;

      dx = guestStackContext->edx & 0xffff;
      al = guestStackContext->eax & 0xff;
      outp(vm, 1, dx, al);
      goto advanceInstruction;
      }

    case 0xef: // OUT_DXeAX
      {
      unsigned dx;
      Bit32u  eax;

      dx  = guestStackContext->edx & 0xffff;
      eax = guestStackContext->eax;
      if ( opsize==0 ) { // 16-bit opsize.
        eax &= 0xffff;
        outp(vm, 2, dx, eax);
        }
      else {
        outp(vm, 4, dx, eax);
        }
      goto advanceInstruction;
      }

    case 0xf0: // LOCK (used to make IRET fault).
      {
      if (iLen == 1) // Prevent endless string of prefixes doing overrun.
        goto decodeOpcode;
      return 0;
      }

    case 0xf4: // HLT
      {
      // Fixme: HLT for non linuxVM mode.
      if ( !guestStackContext->eflags.fields.vif ) {
        monpanic(vm, "HLT with VIF==0.\n");
        return 0;
        }
      while (1) {
        if ( vm->system.INTR )
          break;
        // Fixme: For now I expire 1 pit clock, which is a good number of
        // cpu cycles, but nothing is going on anyways.
        //tsc += cpuToPitRatio;
        pitExpireClocks(vm, 1);
        }
      goto advanceInstruction;
      }

    case 0xfb: // STI (PVI triggers this when VIF is asserted).
      {
      unsigned vector;

      EIP += iLen; // Commit instruction length.
      guestStackContext->eflags.fields.vif = 1;
      if ( !vm->system.INTR ) {
        monpanic(vm, "STI: system.INTR=0?.\n");
        return 0;
        }
      vector = picIAC(vm); // Get interrupt vector from PIC.
      doGuestInterrupt(vm, vector, 0, 0); // Fixme: flags to pass
      return 1;
      }

    default:
      monpanic(vm, "emulateSysInstr: default b0=0x%x\n", b0);
      return 0; // Fail.
    }
  return 0; // Fail.

advanceInstruction:
  EIP += iLen;

  return 1; // OK.
}

  void
emulateUserInstr(vm_t *vm)
{
  unsigned b0, iLen=0;
  Bit8u    opcode[16];
  // modRM_t  modRM;
  // unsigned opsize = 1; // 32-bit opsize by default.

  getObjectLAddr(vm, opcode, EIP, 16);

// decodeOpcode:

  b0 = opcode[iLen++];
  //monprint(vm, "instruction @ eip=0x%x is 0x%x\n", plex86GuestCPU->eip, b0);

//monprint(vm, "U b0=0x%x.\n", b0);

  switch (b0) {

    case 0xcd: // IntIb
      {
      unsigned Ib;

//monprint(vm, "U b0=0x%x >>\n", b0);
      Ib = opcode[iLen++];
      EIP += iLen; // Commit instruction length.
      doGuestInterrupt(vm, Ib, IntFlagSoftInt, 0);
//monprint(vm, "U b0=0x%x <<\n", b0);
      break;
      }

    default:
      monpanic(vm, "emulateUserInstr: b0=0x%x unhandled.\n", b0);
    }
}

#if 0
  unsigned
setCRx(unsigned crx, Bit32u val32)
{
  //monprint(vm, "setCR%u: val=0x%x.\n", crx, val32);

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
        monprint(vm, "setCR0: PE=0.\n");
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
         monprint(vm, "SetCR4: write of 0x%08x unsupported (allowMask=0x%x).",
             val32, allowMask);
         return 0; // Fail.
         }
      val32 &= allowMask; // Screen out unsupported bits for good meassure.
      plex86GuestCPU->cr4.raw = val32;
      // Fixme: have to notify the monitor of paging remap here.
      return 1; // OK.
      }

    default:
      monprint(vm, "setCRx: reg=%u, val=0x%x.\n", crx, val32);
      return 0; // Fail.
    }
  return 0; // Fail.
}
#endif

  unsigned
setDRx(vm_t *vm, unsigned drx, Bit32u val32)
{
  monprint(vm, "setDR%u: ignoring val=0x%x.\n", drx, val32);
  // Fixme: faked out for now.
  return 1; // OK.
}

  unsigned
decodeModRM(vm_t *vm, Bit8u *opcodePtr, modRM_t *modRMPtr)
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
      modRMPtr->addr = GenReg(vm, modRMPtr->rm);
      return(len);
      }
    else if (modRMPtr->mod == 1) {
      monpanic(vm, "decodeModRM: no sib, mod=1.\n");
      }
    else { // (modRMPtr->mod == 2)
      monpanic(vm, "decodeModRM: no sib, mod=2.\n");
      }
    }
  else {
    // s-i-b byte follows.
    unsigned sib, scaledIndex;
    sib = opcodePtr[1];
    len++;
    monprint(vm, "decodeModRM: sib byte follows.\n");
    modRMPtr->base  = (sib & 7);
    modRMPtr->index = (sib >> 3) & 7;
    modRMPtr->scale = (sib >> 6);
    if ( modRMPtr->mod == 0 ) {
      if ( modRMPtr->base == 5 ) {
        // get 32 bit displ + base?
        monpanic(vm, "decodeModRM: sib: mod=%u, base=%u.\n",
                modRMPtr->mod, modRMPtr->base);
        }
      monprint(vm, "decodeModRM: sib: mod=%u, base=%u.\n",
              modRMPtr->mod, modRMPtr->base);
      if ( modRMPtr->index != 4 )
        scaledIndex = GenReg(vm, modRMPtr->index) << modRMPtr->scale;
      else
        scaledIndex = 0;
      modRMPtr->addr = GenReg(vm, modRMPtr->base) + scaledIndex;
      return(len);
      }
    else if ( modRMPtr->mod == 1 ) {
      // get 8 bit displ
      monpanic(vm, "decodeModRM: sib: mod=%u, base=%u.\n",
              modRMPtr->mod, modRMPtr->base);
      }
    else { // ( modRMPtr->mod == 2 )
      // get 32 bit displ
      monpanic(vm, "decodeModRM: sib: mod=%u, base=%u.\n",
              modRMPtr->mod, modRMPtr->base);
      }
    }

  monpanic(vm, "decodeModRM: ???\n");
  return(0);
}

  unsigned
loadGuestSegment(vm_t *vm, unsigned sreg, selector_t selector)
{
  descriptor_t desc, *descPtr;
  guestStackContext_t *guestStackContext = vm->guest.addr.guestStackContext;

  descPtr = fetchGuestDescBySel(vm, selector, &desc, 0);
  if ( descPtr==0 ) {
    monpanic(vm, "loadGuestSegment: fetch failed.\n");
    }

  if (descPtr->dpl != selector.fields.rpl) {
    monpanic(vm, "loadGuestSegment: descriptor.dpl != selector.rpl.\n");
    }

#warning "Fixme: loadGuestSegment: need checks for .type, .p, .base, .limit etc."

  switch ( sreg ) {
    case SRegES:
      guestStackContext->es = selector.raw | 3; // Always push down to ring3.
      break;
    case SRegCS:
      guestStackContext->cs = selector.raw | 3; // Always push down to ring3.
      break;
    case SRegSS:
      guestStackContext->ss = selector.raw | 3; // Always push down to ring3.
      break;
    case SRegDS:
      guestStackContext->ds = selector.raw | 3; // Always push down to ring3.
      break;
    case SRegFS:
      guestStackContext->fs = selector.raw | 3; // Always push down to ring3.
      break;
    case SRegGS:
      guestStackContext->gs = selector.raw | 3; // Always push down to ring3.
      break;
    default:
      monpanic(vm, "loadGuestSegment: sreg = %u.\n", sreg);
    }
  guestSelectorUpdated(vm, sreg, selector);
  return(1); // OK.
}

  void
getObjectLAddr(vm_t *vm, Bit8u *obj, Bit32u lAddr0, unsigned len)
{
  // Get an object of arbitrary size (up to 4K) from linear memory.
  // The object can cross a page boundary, and paging can be disabled
  // or enabled.  This is a general case read operation.

  phyAddr_t pAddr0, pAddr1;
  Bit8u    *phyPagePtr;
  unsigned  pOff;

  pAddr0 = translateLinToPhy(vm, lAddr0);
  if (pAddr0 == LinAddrNotAvailable) {
    monpanic(vm, "getObjectLAddr: lin-->phy translation failed (0x%x).\n",
             lAddr0);
    }
  pOff = pAddr0 & 0xfff;
  if ( pOff <= (0x1000 - len) ) {
    // Object contained within same page.
    if ( pAddr0 > (vm->pages.guest_n_bytes-len) ) {
      monpanic(vm, "getObjectLAddr: pAddr0 OOB (0x%x).\n", pAddr0);
      }
    phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr0>>12,
                              vm->guest.addr.tmp_phy_page0);
    if ( len == 4 ) {
      // Optimize for dwords.
      * ((Bit32u*) obj) = * (Bit32u*) &phyPagePtr[pOff];
      }
    else {
      // Arbitrarily sized objects.
      unsigned i;
      for (i=0; i<len; i++) {
        *obj++ = * (Bit8u*) &phyPagePtr[pOff+i];
        }
      }
    }
  else {
    // object crosses multiple pages.
    unsigned i, page0Len, page1Len;

    page0Len = 0x1000 - pOff;
    page1Len = len - page0Len;
    pAddr1   = translateLinToPhy(vm, lAddr0 + page0Len);
    if (pAddr1 == LinAddrNotAvailable) {
      monpanic(vm, "getObjectLAddr: lin-->phy translation failed (0x%x).\n",
              lAddr0 + page0Len);
      }
    if ( pAddr0 > (vm->pages.guest_n_bytes-page0Len) ) {
      monpanic(vm, "getObjectLAddr: pAddr0 OOB (0x%x).\n", pAddr0);
      }
    if ( pAddr1 > (vm->pages.guest_n_bytes-page1Len) ) {
      monpanic(vm, "getObjectLAddr: pAddr1 OOB (0x%x).\n", pAddr1);
      }

    phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr0>>12,
                              vm->guest.addr.tmp_phy_page0);
    for (i=0; i<page0Len; i++) {
      *obj++ = * (Bit8u*) &phyPagePtr[pOff+i];
      }

    phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr1>>12,
                              vm->guest.addr.tmp_phy_page0);
    for (i=0; i<page1Len; i++) {
      *obj++ = * (Bit8u*) &phyPagePtr[i];
      }
    }
}

  Bit32u
getGuestDWord(vm_t *vm, Bit32u lAddr)
{
  Bit32u    val32;
  unsigned  pOff;

  pOff = lAddr & 0xfff;
  if ( pOff <= (0x1000-4) ) {
    Bit8u    *phyPagePtr;
    phyAddr_t pAddr;

    pAddr = translateLinToPhy(vm, lAddr);
    if (pAddr == LinAddrNotAvailable) {
      monpanic(vm, "getGuestDWord: could not translate address 0x%x.\n",
               lAddr);
      }
    if ( pAddr > (vm->pages.guest_n_bytes-4) ) {
      monpanic(vm, "getGuestDWord: pAddr OOB (0x%x).\n", pAddr);
      }
    // dword contained within same page.
    phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr>>12,
                              vm->guest.addr.tmp_phy_page0);
    val32 = * (Bit32u*) &phyPagePtr[pOff];
    }
  else {
    // Use general case convenience function which can get an object
    // from linear memory anywhere, including crossing page boundaries.
    getObjectLAddr(vm, (Bit8u*) &val32, lAddr, 4);
    }
  return( val32 );
}

  Bit16u
getGuestWord(vm_t *vm, Bit32u lAddr)
{
  Bit16u    val16;
  unsigned  pOff;

  pOff = lAddr & 0xfff;
  if ( pOff <= (0x1000-2) ) {
    Bit8u    *phyPagePtr;
    phyAddr_t pAddr;

    pAddr = translateLinToPhy(vm, lAddr);
    if (pAddr == LinAddrNotAvailable) {
      monpanic(vm, "getGuestWord: could not translate address 0x%x.\n",
               lAddr);
      }
    if ( pAddr > (vm->pages.guest_n_bytes-2) ) {
      monpanic(vm, "getGuestWord: pAddr OOB (0x%x).\n", pAddr);
      }
    // word contained within same page.
    phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr>>12,
                              vm->guest.addr.tmp_phy_page0);
    val16 = * (Bit16u*) &phyPagePtr[pOff];
    }
  else {
    // Use general case convenience function which can get an object
    // from linear memory anywhere, including crossing page boundaries.
    getObjectLAddr(vm, (Bit8u*) &val16, lAddr, 2);
    }

  return( val16 );
}

  void
writeGuestDWord(vm_t *vm, Bit32u lAddr, Bit32u val)
{
  phyAddr_t pAddr;
  Bit8u    *phyPagePtr;

// Fixme: Assumes write priv in page tables.
  pAddr = translateLinToPhy(vm, lAddr);

  if (pAddr == LinAddrNotAvailable) {
    monpanic(vm, "writeGuestDWord: could not translate address.\n");
    }
  if ( pAddr >= (vm->pages.guest_n_bytes-3) ) {
    monpanic(vm, "writeGuestDWord: phy address OOB.\n");
    }
  if ( (pAddr & 0xfff) >= 0xffd ) {
    monpanic(vm, "writeGuestDWord: crosses page boundary.\n");
    }
  phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr>>12,
                            vm->guest.addr.tmp_phy_page0);
  * ((Bit32u*) &phyPagePtr[pAddr & 0xfff]) = val;
}

  descriptor_t *
fetchGuestDescByLAddr(vm_t *vm, Bit32u laddr, descriptor_t *desc)
{
  phyAddr_t descriptorPAddr;
  Bit8u    *phyPagePtr;

  descriptorPAddr = translateLinToPhy(vm, laddr);
  if (descriptorPAddr == LinAddrNotAvailable) {
    monpanic(vm, "fetchGuestDesc: could not translate descriptor addr.\n");
    }
  if (descriptorPAddr & 7) {
    monpanic(vm, "fetchGuestDesc: descriptor not 8-byte aligned.\n");
    }
  if (descriptorPAddr >= (vm->pages.guest_n_bytes-7) ) {
    monpanic(vm, "fetchGuestDesc: descriptor addr OOB.\n");
    }
  phyPagePtr = (Bit8u*) open_guest_phy_page(vm, descriptorPAddr>>12,
                            vm->guest.addr.tmp_phy_page0);
  *desc = * (descriptor_t *) &phyPagePtr[descriptorPAddr & 0xfff];
  return( desc );
}

  descriptor_t *
fetchGuestDescBySel(vm_t *vm, selector_t sel, descriptor_t *desc, unsigned flags)
{
  phyAddr_t gdtOffset, descriptorPAddr;
  Bit8u    *phyPagePtr;
  guest_cpu_t *guestCpu = vm->guest.addr.guest_cpu;

  if (sel.raw & 4) {
    monpanic(vm, "fetchGuestDesc: selector.ti=1.\n");
    }
  if ( (sel.raw & 0xfffc) == 0 ) {
    monpanic(vm, "fetchGuestDesc: selector NULL.\n");
    }

// Fixme: Linux kernel/user seg?
// Fixme: or at least check for gdtIndex==0 (NULL selector)

  gdtOffset = (sel.raw & ~7);
  if ((gdtOffset+7) > guestCpu->gdtr.limit) {
    monpanic(vm, "fetchGuestDesc: selector=0x%x OOB.\n", sel.raw);
    }
  descriptorPAddr = translateLinToPhy(vm, guestCpu->gdtr.base + gdtOffset);
  if (descriptorPAddr == LinAddrNotAvailable) {
    monpanic(vm, "fetchGuestDesc: could not translate descriptor addr.\n");
    }
  if (descriptorPAddr & 7) {
    monpanic(vm, "fetchGuestDesc: descriptor not 8-byte aligned.\n");
    }
  if (descriptorPAddr >= (vm->pages.guest_n_bytes-7) ) {
    monpanic(vm, "fetchGuestDesc: descriptor addr OOB.\n");
    }
  phyPagePtr = (Bit8u*) open_guest_phy_page(vm, descriptorPAddr>>12,
                            vm->guest.addr.tmp_phy_page0);
  *desc = * (descriptor_t *) &phyPagePtr[descriptorPAddr & 0xfff];
  if (desc->dpl != sel.fields.rpl) {
    monpanic(vm, "fetchGuestDesc: descriptor.dpl(%u) != selector.rpl(%u).\n",
             desc->dpl, sel.fields.rpl);
    }
  if ( flags & FetchDescUpdateBusyBit ) {
    ((descriptor_t *) &phyPagePtr[descriptorPAddr & 0xfff])->type |= 2;
    }
  return( desc );
}


  unsigned
inp(vm_t *vm, unsigned iolen, unsigned port)
{
  switch ( port ) {
    case 0x21:
      return( picInp(vm, iolen, port) );
    case 0x40:
      return( pitInp(vm, iolen, port) );
    case 0x61:
      return( pitInp(vm, iolen, port) );

    case 0x71:
      return( cmosInp(vm, iolen, port) );
    case 0x3d5:
    case 0x3da:
      return( vgaInp(vm, iolen, port) );

    case 0x30:
      monprint(vm, "inp(0x30) ???\n");
      goto error;
      return(0xff);

    default:
      monprint(vm, "inp: port=0x%x unsupported.\n", port);
      goto error;
    }

error:
  monpanic(vm, "inp: bailing.\n");
}

  void
outp(vm_t *vm, unsigned iolen, unsigned port, unsigned val)
{
  //monprint(vm, "outp: port=0x%x, val=0x%x\n", port, val);
  switch ( port ) {
    case 0x20:
    case 0x21:
    case 0xa0:
    case 0xa1:
      picOutp(vm, iolen, port, val);
      return;

    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
      pitOutp(vm, 1, port, val);
      return;

    case 0x61:
      pitOutp(vm, 1, port, val);
      return;

    case 0x70:
      cmosOutp(vm, 1, port, val);
      return;

    case 0x80:
      vm->io.port0x80 = val;
      //monprint(vm, "Port0x80 = 0x%02x\n", port0x80);
      return;

    case 0x3c0:
    case 0x3c8:
    case 0x3c9:
    case 0x3d4:
    case 0x3d5:
      vgaOutp(vm, 1, port, val);
      return;

    case 0x3f2:
      monprint(vm, "outp(0x%x)=0x%x unsupported.\n", port, val);
      return;

    default:
      monprint(vm, "outp(0x%x)=0x%x unsupported.\n", port, val);
      goto error;
    }

error:
  monpanic(vm, "outp: bailing.\n");
}


  void      
doGuestInterrupt(vm_t *vm, unsigned vector, unsigned intFlags, Bit32u errorCode)
{
  Bit32u     gateLAddr;
  gate_t    *gatePtr, gateDesc;
  selector_t csSel;
  Bit32u     offset32;
  descriptor_t *csDescPtr, csDesc;
  Bit32u esp;
  Bit32u oldEFlags;
  guest_cpu_t *guestCpu;
  guestStackContext_t *guestStackContext;
  unsigned fromCPL;
  nexus_t *nexus;
  Bit32u newCR4;

  guestCpu          = vm->guest.addr.guest_cpu;
  guestStackContext = vm->guest.addr.guestStackContext;
  nexus             = vm->guest.addr.nexus;
  fromCPL = CPL;

  // monprint(vm, "doGuestInterrupt: vector=%u.\n", vector);
  if ( ((vector<<3)+7) > guestCpu->idtr.limit ) {
    monpanic(vm, "doGuestInterrupt: vector(%u) OOB.\n", vector);
    }

  // If IDTR is 8-byte aligned, we don't have to worry about the
  // descriptor crossing page boundaries.
  if ( guestCpu->idtr.base & 7 ) {
    monpanic(vm, "doGuestInterrupt: idtr.base not 8-byte aligned.\n");
    }

  gateLAddr = guestCpu->idtr.base + (vector<<3);
  gatePtr = (gate_t *) fetchGuestDescByLAddr(vm, gateLAddr,
                           (descriptor_t *) &gateDesc);
  if ( gatePtr->p==0 ) {
    monpanic(vm, "doGuestInterrupt: p=%u\n", gatePtr->p);
    }
  if ( (gatePtr->type!=14) && (gatePtr->type!=15) ) { // 32-bit int/trap gate.
    monpanic(vm, "doGuestInterrupt: desc type(%u)!=14.\n", gatePtr->type);
    }
  if ( (intFlags & IntFlagSoftInt) && (gatePtr->dpl < fromCPL) ) {
    monpanic(vm, "doGuestInterrupt: intIb, dpl(%u)<CPL(%u).\n",
            gatePtr->dpl, fromCPL);
    }

  csSel = gatePtr->selector;
  offset32 = (gatePtr->offset_high<<16) | gatePtr->offset_low;

  if ( (csSel.raw & 0xfffc) == 0 ) {
    monpanic(vm, "doGuestInterrupt: gate selector NULL.\n");
    }
  csDescPtr = fetchGuestDescBySel(vm, csSel, &csDesc, 0);
  if ( (csDescPtr==0) ||
       (csDescPtr->type!=0x1a) ||
       (csDescPtr->dpl!=0) ||
       (csDescPtr->p==0) ) {
    monpanic(vm, "doGuestInterrupt: bad CS descriptor, type=0x%x, "
                 "dpl=%u, p=%u.\n",
            csDescPtr->type, csDescPtr->dpl, csDescPtr->p);
    }

  oldEFlags = guestStackContext->eflags.raw;
  if ( vm->linuxVMMode && (fromCPL==0) ) {
    // In Linux mode, PVI is used to help maintain the interrupt flag for
    // CPL==0.  If this transition was from ring0, we need to correct the eflags
    // image that gets pushed on the guest stack by copying VIF to IF.
    if (oldEFlags & FlgMaskVIF)
      oldEFlags |= FlgMaskIF;
    else
      oldEFlags &= ~FlgMaskIF;
    }

  if ( csDescPtr->dpl < fromCPL ) { // Interrupt to inner (system) ring.
    Bit32u oldESP, oldSS, tssESP, trLimit, trBase, tssStackInfoOffset;
    selector_t ssSel;
    descriptor_t *ssDescPtr, ssDesc;

    // Get inner SS/ESP values from TSS.
    if ( guestCpu->tr.des.type != 11 ) {
      monpanic(vm, "doGuestInterrupt: TR.type(%u)!=11.\n",
              guestCpu->tr.des.type);
      }
    trLimit = ((guestCpu->tr.des.limit_high<<16) |
               (guestCpu->tr.des.limit_low<<0));
    if ( guestCpu->tr.des.g )
      trLimit = (trLimit<<12) | 0xfff;
    trBase  = (guestCpu->tr.des.base_high<<24) |
              (guestCpu->tr.des.base_med<<16) |
              (guestCpu->tr.des.base_low<<0);
    tssStackInfoOffset = 8*csDescPtr->dpl + 4;
    if ( (tssStackInfoOffset+7) > trLimit ) {
      monpanic(vm, "doGuestInterrupt: bad TR.limit.\n");
      }
    tssESP    = getGuestDWord(vm, trBase + tssStackInfoOffset);
    ssSel.raw = getGuestWord(vm, trBase + tssStackInfoOffset + 4);

    if ( (ssSel.raw & 0xfffc) == 0 ) {
      monpanic(vm, "doGuestInterrupt: bad SS value 0x%x.\n", ssSel.raw);
      }
    if ( ssSel.fields.rpl != csDescPtr->dpl ) {
      monpanic(vm, "doGuestInterrupt: ss.rpl != cs.rpl.\n");
      }
    ssDescPtr = fetchGuestDescBySel(vm, ssSel, &ssDesc, 0);
    if ( (ssDescPtr==0) ||
         (ssDescPtr->type!=0x12) ||
         (ssDescPtr->dpl!=0) ||
         (ssDescPtr->p==0) ) {
      monpanic(vm, "doGuestInterrupt: bad SS descriptor, type=0x%x, "
                      "dpl=%u, p=%u.\n",
               ssDescPtr->type, ssDescPtr->dpl, ssDescPtr->p);
      }

    oldESP = guestStackContext->esp;
    oldSS  = guestStackContext->ss;
    // Need to set CPL before calling other functions which depend on it.
    CPL = csSel.fields.rpl;
    guestStackContext->ss  = ssSel.raw | 3; // Always push down to ring3.
    guestStackContext->esp = tssESP;
    guestSelectorUpdated(vm, SRegSS, ssSel);

    guestStackContext->esp -= 20;
    esp = guestStackContext->esp;
    // Push SS
    // Push ESP
    writeGuestDWord(vm, esp+16, oldSS);
    writeGuestDWord(vm, esp+12, oldESP);
    }
  else {
    guestStackContext->esp -= 12;
    esp = guestStackContext->esp;
    }

  // Push eflags
  // Push CS
  // Push EIP
  writeGuestDWord(vm, esp+8,  oldEFlags);
  writeGuestDWord(vm, esp+4,  (guestStackContext->cs & ~3) | fromCPL);
  writeGuestDWord(vm, esp+0,  guestStackContext->eip);

  // If this fault has an associated error code, push that on the stack also.
  if ( intFlags & IntFlagPushError ) {
    guestStackContext->esp -= 4;
    writeGuestDWord(vm, guestStackContext->esp, errorCode);
    }

  guestStackContext->cs  = csSel.raw | 3; // Always push down to ring3.
  guestStackContext->eip = offset32;
  guestSelectorUpdated(vm, SRegCS, csSel);

  // Clear EFLAGS.{VM,RF,NT,TF}.
  oldEFlags &= ~(FlgMaskVM | FlgMaskRF | FlgMaskNT | FlgMaskTF);
  // If interupt-gate, clear IF.
  if ( gatePtr->type==14 )
    oldEFlags &= ~FlgMaskIF;
  if ( vm->linuxVMMode && (CPL==0) ) {
    // Linux VM kernel code.  Use PVI support to manage IF.
    // VIF is set to the requested guest IF value and CR4 must be enabled.
    newCR4 = 0x00000002; /* TSD=0, PVI=1. */
    if ( oldEFlags & FlgMaskIF ) {
      // Guest requests IF=1, so set VIF.
      oldEFlags |= FlgMaskVIF;
      }
    else {
      // Guest requests IF=0, so clear VIF.
      oldEFlags &= ~FlgMaskVIF;
      }
    }
  else {
    newCR4 = 0x00000000; /* TSD=0, PVI=0 */
    if ( !(oldEFlags & FlgMaskIF) ) {
      monpanic(vm, "doGI: IF=0.\n");
      }
    }
  // ALWAYS set the actual value of IF to 1, since the monitor needs it
  // to receive hardware interrupts intended for the host OS!!!
  oldEFlags |= FlgMaskIF;
  guestStackContext->eflags.raw = oldEFlags;

  // If the value of the monitor's CR4 register needs to change to
  // accomodate a change in the guest (like a transition in/out of PVI mode),
  // we must record the change and reload the actual CR4 register now since
  // it is not reloaded in the IRET back to the guest.
  if ( nexus->mon_cr4 != newCR4 ) {
    nexus->mon_cr4 = newCR4;
    loadCR4(newCR4);
    //monprint(vm, "CR4 reloaded to 0x%x.\n", newCR4);
    }
  //monprint(vm, "CPL %u -> %u, eflags=0x%x\n", fromCPL, CPL, oldEFlags);
}


#if 0
  void
doIRET(void)
{
  Bit32u        retEIP, retEFlags, esp, ifFlags;
  selector_t    retCS;
  descriptor_t *csDescPtr, *ssDescPtr;
  unsigned      fromCPL;
  const unsigned fromIOPL=0; // Only allow guest IOPL==0.
  Bit32u        changeMask;

  fromCPL = CPL; // Save the source CPL since we overwrite it.

  if ( plex86GuestCPU->eflags & FlgMaskVIF ) {
    monprint(vm, "doIRET: VIF=1.\n");
    goto error;
    }
  if ( plex86GuestCPU->eflags & FlgMaskNT ) {
    // NT=1 means return from nested task.
    monprint(vm, "doIRET: NT=1.\n");
    goto error;
    }
  esp = plex86GuestCPU->genReg[GenRegESP];
  retEIP     = getGuestDWord(esp+0);
  retCS.raw  = getGuestWord(esp+4);
  retEFlags  = getGuestDWord(esp+8);
  ifFlags    = retEFlags & (FlgMaskVIF | FlgMaskIF);
  if ( ifFlags == (FlgMaskVIF | FlgMaskIF) ) {
    // Both IF/VIF are set.
    retEFlags &= ~FlgMaskVIF;
    }
  else if ( ifFlags!=0 ) {
    monprint(vm, "doIRET: VIF!=IF: 0x%x.\n", retEFlags);
    }

  if ( retEFlags & FlgMaskVM ) {
    // IRET to v86 mode not supported.
    monprint(vm, "doIRET: return EFLAGS.VM=1.\n");
    goto error;
    }
  if ( retEFlags & FlgMaskIOPL ) {
    // IRET eflags value has IOPL non-zero.
    monprint(vm, "doIRET: return EFLAGS.IOPL=%u.\n", (retEFlags>>12)&3);
    goto error;
    }
  if ( (retCS.raw & 0xfffc) == 0 ) {
    monprint(vm, "doIRET: return CS NULL.\n");
    goto error;
    }
  if ( ((retCS.fields.rpl!=0) && (retCS.fields.rpl!=3)) || retCS.fields.ti ) {
    monprint(vm, "doIRET: bad return CS=0x%x.\n", retCS.raw);
    goto error;
    }
  if ( retCS.fields.rpl < fromCPL ) {
    // Can not IRET to an inner ring.
    monprint(vm, "doIRET: to rpl=%u from CPL=%u.\n", retCS.fields.rpl, CPL);
    goto error;
    }
  csDescPtr = fetchGuestDescBySel(vm, retCS);
  if ( (csDescPtr==NULL) ||
       (csDescPtr->type!=0x1a) ||
       (csDescPtr->dpl!=retCS.fields.rpl) ||
       (csDescPtr->p==0) ) {
    monprint(vm, "doIRET: bad CS descriptor, type=0x%x, "
                    "dpl=%u, p=%u.\n",
                    csDescPtr->type, csDescPtr->dpl, csDescPtr->p);
    goto error;
    }

  if ( retCS.fields.rpl > fromCPL ) {
    // IRET to outer (user) privilege level.  We have to also
    // get SS:ESP from the kernel stack, and validate those values.
    Bit32u     retESP;
    selector_t retSS;
    unsigned i, sReg;
//monprint(vm, "doIRET: rpl=%u.\n", retCS.fields.rpl);

    retESP    = getGuestDWord(esp+12);
    retSS.raw = getGuestWord(esp+16);
    if ( (retSS.raw & 0xfffc) == 0 ) {
      monprint(vm, "doIRET: return SS NULL.\n");
      goto error;
      }
    if ( retSS.fields.rpl != retCS.fields.rpl ) {
      monprint(vm, "doIRET: SS.rpl!=CS.rpl.\n");
      goto error;
      }
    ssDescPtr = fetchGuestDescBySel(vm, retSS);
    if ( (ssDescPtr==NULL) ||
         (ssDescPtr->type!=0x12) ||
         (ssDescPtr->dpl!=retCS.fields.rpl) ||
         (ssDescPtr->p==0) ) {
      monprint(vm, "doIRET: bad SS descriptor, type=0x%x, "
                      "dpl=%u, p=%u.\n",
                      ssDescPtr->type, ssDescPtr->dpl, ssDescPtr->p);
      goto error;
      }

    plex86GuestCPU->sreg[SRegSS].sel = retSS;
    plex86GuestCPU->sreg[SRegSS].des = * ssDescPtr;
    plex86GuestCPU->sreg[SRegSS].valid = 1;
    plex86GuestCPU->genReg[GenRegESP] = retESP;

    // For an IRET to an outer ring (user code), if any of the other
    // (non SS,CS) segment registers are loaded for the inner (system)
    // ring, then they are invalidated for use in the new less privileged ring.
    for (i=0; i<4; i++) {
      sReg = dataSReg[i];
      if ( plex86GuestCPU->sreg[sReg].valid ) {
        if ( plex86GuestCPU->sreg[sReg].des.dpl < retCS.fields.rpl ) {
          plex86GuestCPU->sreg[sReg].sel.raw = 0;
          plex86GuestCPU->sreg[sReg].des = nullDesc;
          plex86GuestCPU->sreg[sReg].valid = 0;
          }
        }
      }
    }
  else {
    // If IRET to same level, then ESP did not come off the kernel
    // stack.  We simple increment it to simulate the 3 dwords popped.
    plex86GuestCPU->genReg[GenRegESP] += 12;
    }

  // All values have been fetched from the stack.  Proceed to load
  // CPU registers and descriptor cache values.
  plex86GuestCPU->sreg[SRegCS].sel = retCS;
  plex86GuestCPU->sreg[SRegCS].des = * csDescPtr;
  plex86GuestCPU->sreg[SRegCS].valid = 1;
  plex86GuestCPU->eip = retEIP;

//if (retCS.fields.rpl==3) {
//  monprint(vm, "iret to ring3, cs.slot=%u, eip=0x%x.\n",
//          retCS.fields.index, retEIP);
//  }

  changeMask = FlgMaskID | FlgMaskAC | FlgMaskRF | FlgMaskNT |
               FlgMaskOF | FlgMaskDF | FlgMaskTF | FlgMaskSF |
               FlgMaskZF | FlgMaskAF | FlgMaskPF | FlgMaskCF;
// Fixme: remove
if (changeMask != 0x254dd5) {
  monprint(vm, "changeMask != 0x254dd5.\n");
  goto error;
  }

  // IOPL is changed according to the test below on a processor.  However,
  // I disallow IOPL other than 0 above.
  // if ( fromCPL == 0 )
  //   changeMask |= FlgMaskIOPL;
  if ( fromCPL <= fromIOPL )
    changeMask |= FlgMaskIF;
  plex86GuestCPU->eflags =
    (plex86GuestCPU->eflags & ~changeMask) |
    (retEFlags & changeMask);

  return;

error:
  plex86TearDown(); exit(1);

// don't forget setting CPL
}
#endif


  unsigned
doWRMSR(vm_t *vm)
{
  Bit32u ecx, edx, eax;
  guest_cpu_t *guestCpu = vm->guest.addr.guest_cpu;

  // MSR[ECX] <-- EDX:EAX
  // #GP(0): cpuid.msr==0, reserved or unimplemented MSR addr.
  ecx = GenReg(vm, GenRegECX); // MSR #
  edx = GenReg(vm, GenRegEDX); // High dword
  eax = GenReg(vm, GenRegEAX); // Low  dword

  switch ( ecx ) {
    case MSR_IA32_SYSENTER_CS:
      // monprint(vm, "WRMSR[IA32_SYSENTER_CS]: 0x%x\n", eax);
      guestCpu->sysEnter.cs = eax;
      return 1;
    case MSR_IA32_SYSENTER_ESP:
      // monprint(vm, "WRMSR[IA32_SYSENTER_ESP]: 0x%x\n", eax);
      guestCpu->sysEnter.esp = eax;
      return 1;
    case MSR_IA32_SYSENTER_EIP:
      // monprint(vm, "WRMSR[IA32_SYSENTER_EIP]: 0x%x\n", eax);
      guestCpu->sysEnter.eip = eax;
      return 1;

    default:
      monpanic(vm, "WRMSR[0x%x]: unimplemented MSR.\n", ecx);
      //doGuestInterrupt(vm, ExceptionGP, IntFlagPushError, 0);
      //return 0; // Fault occurred.
      // Fixme: should assert RF in a separate exception routine.
      break;
    }

  monpanic(vm, "doWRMSR.\n");
}

// Fixme: this is fetching a descriptor again.
#warning "guestSelectorUpdated should take a descriptor parameter."

  void
guestSelectorUpdated(vm_t *vm, unsigned segno, selector_t sel)
{
  /* A guest selector has been updated.  The selector value on the
   * monitor stack has already been set.  We need to do some validation
   * of the selector value, the associated guest descriptor, and
   * initialize the GDT slot with a virtualized descriptor.
   */

  /* Only check if non-CS selector is not a NULL selector.  Data selectors
   * can be loaded with a NULL selector.
   */
  if ( (segno==SRegCS) || (sel.raw & 0xfffc) ) {
    unsigned gdtSlot;
    Bit32u   gdtOffset;
    descriptor_t desc, *descPtr;
    guest_cpu_t *guestCpu = vm->guest.addr.guest_cpu;
    Bit32u descLAddr;

    /* We don't support LDT usage. */
    if ( sel.fields.ti || (sel.fields.index==0) ) {
      monpanic(vm, "GuestSU: invalid selector 0x%x.\n", sel.raw);
      }

    gdtOffset = sel.raw & ~7;
    gdtSlot   = sel.fields.index;
    if ( (gdtOffset+7) > guestCpu->gdtr.limit) {
      monpanic(vm, "GuestSU: selector=0x%x OOB.\n", sel.raw);
      }
    descLAddr = guestCpu->gdtr.base + gdtOffset;
    descPtr = fetchGuestDescByLAddr(vm, descLAddr, &desc);
    if ( descPtr==0 ) {
      monpanic(vm, "GuestSU: fetch of guest desc failed; sel=0x%x\n", sel.raw);
      }

    if (vm->linuxVMMode) {
      /* For Linux VM mode, we can also execute the kernel in the VM, but
       * at user privilge using PVI, granted it is configured/compiled for
       * this VM-friendly mode.  In this case, we allow segments of
       * either ring 0 or 3.
       */
      if ( sel.fields.rpl != desc.dpl ) {
        monpanic(vm, "GuestSU: guest desc.dpl(%u) != sel.rpl(%u)\n",
                 desc.dpl, sel.fields.rpl);
        }
      /* Check that DPL is either 0 or 3.  We do not handle 1,2. */
      if ( (desc.dpl!=0) &&
           (desc.dpl!=3) ) {
        monpanic(vm, "GuestSU: guest desc.dpl(%u) strange\n", desc.dpl);
        }
      /* The segments _must_ be in the expected Linux GDT entries, for
       * linuxVMMode, based on the current privilege level.
       */
      if ( vm->guestCPL==0 ) {
        /* Linux kernel. */
        if ( segno == SRegCS ) {
          /* Kernel code segment. */
          if ( (gdtSlot!=LinuxBootCsSlot) &&
               (gdtSlot!=LinuxKernelCsSlot) ) {
            monpanic(vm, "GuestSU: not linux kern CS.\n");
            }
          }
        else {
          /* Kernel data segment. */
          if ( (gdtSlot!=LinuxBootDsSlot) &&
               (gdtSlot!=LinuxKernelDsSlot) &&
               (gdtSlot!=LinuxUserDsSlot) ) {
            monpanic(vm, "GuestSU: not linux kern DS.\n");
            }
          }
        }
      else {
        /* Linux user. */
        if ( segno == SRegCS ) {
          /* User code segment. */
          if ( gdtSlot!=LinuxUserCsSlot ) {
            monpanic(vm, "GuestSU: not linux user cs.\n");
            }
          }
        else {
          /* User data segment. */
          if ( gdtSlot!=LinuxUserDsSlot ) {
            monpanic(vm, "GuestSU: not linux user ds.\n");
            }
          }
        }
      }
    else {
      /* Normal mode.  Only handle user segments. */
      if ( (sel.fields.rpl != 3) ||
           (desc.dpl != 3) ) {
        monpanic(vm, "GuestSU: rpl!=3/dpl!=3.\n");
        }
      /* Descriptor GDT slot must not conflict with the VM monitor slots,
       * and not be zero.
       */
      if ( !OKGuestSlot(gdtSlot) ) {
        monpanic(vm, "GuestSU: !okslot.\n");
        }
      }

    /* Install virtualized guest descriptors in GDT. */
#warning "Check gdtSlot for OOB here?"
    vm->guest.addr.gdt[gdtSlot] = desc;
    /* Descriptors are always virtualized down to ring3. */
    if (desc.dpl != 3)
      vm->guest.addr.gdt[gdtSlot].dpl = 3;
    }

#if 0
handleFailSegReg:
  /* Generate an error code specific to the segment. */
  if (s==SRegCS) {
    retval = Plex86NoExecute_CS; /* Fail. */
    goto handleFail;
    }
  else {
    retval = Plex86NoExecute_Selector; /* Fail. */
    goto handleFail;
    }
#endif

#warning "Have to clear out GDT, especially with ring changes"
}
