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


// ===================
// Defines / typesdefs
// ===================

// Flags to pass to doGuestInterrupt()
#define IntFlagPushError 1
#define IntFlagAssertRF  2
#define IntFlagSoftInt   4

#define MSR_IA32_SYSENTER_CS    0x174
#define MSR_IA32_SYSENTER_ESP   0x175
#define MSR_IA32_SYSENTER_EIP   0x176

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

typedef struct {
  Bit32u cs;
  Bit32u eip;
  Bit32u esp;
  } sysEnter_t;


// ===================
// Function prototypes
// ===================

static unsigned  emulateSysInstr(vm_t *);
static void      emulateUserInstr(vm_t *);
//static unsigned  setCRx(vm_t *, unsigned crx, Bit32u val32);
//static unsigned  setDRx(unsigned drx, Bit32u val32);
static phyAddr_t translateLinToPhy(vm_t *, Bit32u lAddr);
static unsigned  decodeModRM(vm_t *, Bit8u *opcodePtr, modRM_t *modRMPtr);
static unsigned  loadGuestSegment(vm_t *, unsigned sreg, selector_t selector);
static Bit32u    getGuestDWord(vm_t *, Bit32u lAddr);
static Bit16u    getGuestWord(vm_t *, Bit32u lAddr);
static void      writeGuestDWord(vm_t *vm, Bit32u lAddr, Bit32u val);
static descriptor_t *fetchGuestDescBySel(vm_t *, selector_t, descriptor_t *);
static descriptor_t *fetchGuestDescByLAddr(vm_t *, Bit32u laddr,
                                           descriptor_t *desc);
//static unsigned  inp(unsigned iolen, unsigned port);
//static void      outp(unsigned iolen, unsigned port, unsigned val);

//static void      doIRET(void);
//static unsigned  doWRMSR(void);
static void      doGuestInterrupt(vm_t *vm, unsigned vector, unsigned intFlags,
                                  Bit32u errorCode);

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

//static sysEnter_t sysEnter;

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
monprint(vm, "#NM (CPL!=0).\n"); // Fixme:
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
  phyAddr_t pAddr0, pAddr1;
  unsigned b0, iLen=0;
  Bit8u    opcode[16];
  Bit32u  *opcodeDWords;
  modRM_t modRM;
  unsigned  pOff;
  Bit32u page0Len, page1Len;
  unsigned opsize = 1; // 32-bit opsize by default.
  guest_cpu_t *guestCpu = vm->guest.addr.guest_cpu;
  guestStackContext_t *guestStackContext = vm->guest.addr.guestStackContext;

  if (guestCpu->cr0.fields.pg) {
    Bit32u lAddr0;
    Bit8u *phyPagePtr;

    lAddr0 = EIP; // Forget segmentation base for Linux.
    pAddr0 = translateLinToPhy(vm, lAddr0);
    if (pAddr0 == LinAddrNotAvailable) {
      monpanic(vm, "emulateSysInstr: lin-->phy translation failed (0x%x).\n",
              lAddr0);
      }
    pOff = pAddr0 & 0xfff;
    if ( pOff <= 0xff0 ) {
      page0Len = 16;
      page1Len = 0;
      pAddr1   = pAddr0; // Keep compiler quiet.
      }
    else {
      page0Len = 0x1000 - pOff;
      page1Len = 16 - page0Len;
      pAddr1    = translateLinToPhy(vm, lAddr0 + page0Len);
      if (pAddr1 == LinAddrNotAvailable) {
        monpanic(vm, "emulateSysInstr: lin-->phy translation failed (0x%x).\n",
                lAddr0 + page0Len);
        }
      }

fetch16:

    if ( pAddr0 >= (vm->pages.guest_n_bytes-16) ) {
      monpanic(vm, "emulateSysInstr: physical address of 0x%x "
              "beyond memory.\n", pAddr0);
      }
    if ( page0Len == 16 ) {
      // Draw in 16 bytes from guest memory into a local array so we don't
      // have to worry about edge conditions when decoding.  All accesses are
      // within a single physical page.
      // Fixme: we could probably use the linear address (corrected for
      // Fixme: monitor CS.base) to access the page without creating a window.
      /* Open a window into guest physical memory. */
      phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr0>>12,
                                vm->guest.addr.tmp_phy_page0);
      opcodeDWords = (Bit32u*) &opcode[0];
      opcodeDWords[0] = * (Bit32u*) &phyPagePtr[pOff+ 0];
      opcodeDWords[1] = * (Bit32u*) &phyPagePtr[pOff+ 4];
      opcodeDWords[2] = * (Bit32u*) &phyPagePtr[pOff+ 8];
      opcodeDWords[3] = * (Bit32u*) &phyPagePtr[pOff+12];
      }
    else {
      unsigned i;

      phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr0>>12,
                                vm->guest.addr.tmp_phy_page0);
      // Fetch initial bytes from page0.
      for (i=0; i<page0Len; i++) {
        opcode[i] = * (Bit8u*) &phyPagePtr[pOff + i];
        }

      phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr1>>12,
                                vm->guest.addr.tmp_phy_page0);
      // Fetch remaining bytes from page1.
      for (i=0; i<page1Len; i++) {
        opcode[page0Len + i] = * (Bit8u*) &phyPagePtr[i];
        }
      }
    }
  else {
    pAddr0 = EIP; // Forget segmentation base for Linux.
    pOff = pAddr0 & 0xfff;
    if ( pOff <= 0xff0 ) {
      page0Len = 16;
      page1Len = 0;
      pAddr1   = pAddr0; // Keep compiler quiet.
      }
    else {
      page0Len = 0x1000 - pOff;
      page1Len = 16 - page0Len;
      pAddr1    = pAddr0 + page0Len;
      }
    goto fetch16;
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
        case 0x00: // Group6
          {
          iLen += decodeModRM(vm, &opcode[iLen], &modRM);
          monprint(vm, "emulateSysIntr: G6: nnn=%u\n", modRM.nnn);
          switch (modRM.nnn) {
            case 2: // LLDT_Ew
              {
return 0; // defer to user space implementation for now.
#if 0
              selector_t selector;

              if (modRM.mod==3)
                selector.raw = plex86GuestCPU->genReg[modRM.rm];
              else
                selector.raw = getGuestWord(modRM.addr);
              if ( (selector.raw & 0xfffc) == 0 ) {
                plex86GuestCPU->ldtr.sel = selector;
                plex86GuestCPU->ldtr.valid = 0;
                goto advanceInstruction;
                }
              fprintf(stderr, "emulateSysIntr: LLDT: forcing selector null.\n");
              plex86GuestCPU->ldtr.sel.raw = 0; // Make it null anyways.
              plex86GuestCPU->ldtr.valid = 0;
              goto advanceInstruction;
#endif
              }

            case 3: // LTR_Ew
              {
return 0; // defer to user space implementation for now.
#if 0
              selector_t    tssSelector;
              descriptor_t *tssDescriptorPtr;
              Bit32u tssLimit;

              if (modRM.mod==3)
                tssSelector.raw = plex86GuestCPU->genReg[modRM.rm];
              else
                tssSelector.raw = getGuestWord(modRM.addr);
              if ( (tssSelector.raw & 0xfffc) == 0 ) {
                plex86GuestCPU->tr.sel = tssSelector;
                plex86GuestCPU->tr.valid = 0;
                goto advanceInstruction;
                }
              if ( tssSelector.raw & 4 ) {
                fprintf(stderr, "LTR_Ew: selector.ti=1.\n");
                return 0;
                }
              tssDescriptorPtr = fetchGuestDescBySel(vm, tssSelector);
              if ( !tssDescriptorPtr ) {
                fprintf(stderr, "LTR_Ew: bad descriptor.\n");
                return 0;
                }
              tssLimit = (tssDescriptorPtr->limit_high<<16) |
                          tssDescriptorPtr->limit_low;
              if (tssDescriptorPtr->g)
                tssLimit = (tssLimit<<12) | 0xfff;
              if ( (tssDescriptorPtr->p==0) ||
                   (tssDescriptorPtr->type!=9) ||
                   (tssLimit<103) ) {
                fprintf(stderr, "LTR_Ew: bad descriptor.\n");
                return 0;
                }
              plex86GuestCPU->tr.sel = tssSelector;
              plex86GuestCPU->tr.des = *tssDescriptorPtr;
              plex86GuestCPU->tr.des.type |= 2; // Set busy bit.
              plex86GuestCPU->tr.valid = 1;

              // Write descriptor back to memory to update busy bit.
              tssDescriptorPtr->type |= 2;
              goto advanceInstruction;
#endif
              }

            default:
            }
          return 0;
          }

        case 0x01: // Group7
          {
          iLen += decodeModRM(vm, &opcode[iLen], &modRM);
// fprintf(stderr, "emulateSysIntr: G7: nnn=%u\n", modRM.nnn);
          switch (modRM.nnn) {
            case 2: // LGDT_Ms
              {
return 0; // defer to user space implementation for now.
#if 0
              Bit32u   base32;
              Bit16u   limit16;

              if (modRM.mod==3) { // Must be a memory reference.
                fprintf(stderr, "LGDT_Ms: mod=3.\n");
                return 0;
                }
              limit16 = getGuestWord(modRM.addr);
              base32  = getGuestDWord(modRM.addr+2);
              plex86GuestCPU->gdtr.base  = base32;
              plex86GuestCPU->gdtr.limit = limit16;
fprintf(stderr, "GDTR.limit = 0x%x\n", plex86GuestCPU->gdtr.limit);
              goto advanceInstruction;
#endif
              }
            case 3: // LIDT_Ms
              {
return 0; // defer to user space implementation for now.
#if 0
              Bit32u   base32;
              Bit16u   limit16;

              if (modRM.mod==3) { // Must be a memory reference.
                fprintf(stderr, "LIDT_Ms: mod=3.\n");
                return 0;
                }
              limit16 = getGuestWord(modRM.addr);
              base32  = getGuestDWord(modRM.addr+2);
              plex86GuestCPU->idtr.base  = base32;
              plex86GuestCPU->idtr.limit = limit16;
              goto advanceInstruction;
#endif
              }
            case 7: // INVLPG
              {
return 0; // defer to user space implementation for now.
#if 0
              if (modRM.mod==3) { // Must be a memory reference.
                fprintf(stderr, "INVLPG: mod=3.\n");
                return 0;
                }
              // Fixme: must act on this when this code goes in the VMM.
              // For now, who cares since the page tables are rebuilt anyways.
              goto advanceInstruction;
#endif
              }

            case 0: // SGDT_Ms
            case 1: // SIDT_Ms
            case 4:
            case 5:
            case 6:
            }
          return 0;
          }

        case 0x06: // CLTS
          {
return 0; // defer to user space implementation for now.
#if 0
          guestCpu->cr0.fields.ts = 0;
          goto advanceInstruction;
#endif
          }

        case 0x20: // MOV_RdCd
          {
return 0; // defer to user space implementation for now.
#if 0
          unsigned modrm, rm, nnn;
          Bit32u   val32;
// Fix this fetch.
          modrm = opcode[iLen++];
          rm   = modrm & 7;
          nnn  = (modrm>>3) & 7;
          if ( (modrm & 0xc0) != 0xc0 ) {
            fprintf(stderr, "MOV_RdCd: mod field not 11b.\n");
            return 0;
            }
          switch (nnn) {
            case 0: val32 = plex86GuestCPU->cr0.raw; break;
            case 2: val32 = plex86GuestCPU->cr2;     break;
            case 3: val32 = plex86GuestCPU->cr3;     break;
            case 4: val32 = plex86GuestCPU->cr4.raw; break;
            default:
              fprintf(stderr, "MOV_RdCd: cr%u?\n", nnn);
              return 0;
            }
          plex86GuestCPU->genReg[rm] = val32;
          goto advanceInstruction;
#endif
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
return 0; // defer to user space implementation for now.
#if 0
          Bit32u   val32;
          iLen += decodeModRM(&opcode[iLen], &modRM);
          if ( modRM.mod!=3 ) {
            fprintf(stderr, "MOV_DdRd: mod field not 3.\n");
            return 0;
            }
          val32 = plex86GuestCPU->genReg[modRM.rm];
          if ( setDRx(modRM.nnn, val32) )
            goto advanceInstruction;
          return 0;
#endif
          }

        case 0x30: // WRMSR
          {
return 0; // defer to user space implementation for now.
#if 0
          if ( doWRMSR() )
            goto advanceInstruction;
          return 0;
#endif
          }

        case 0x31: // RDTSC
          {
return 0; // defer to user space implementation for now.
#if 0
          fprintf(stderr, "RDTSC.\n");
          plex86GuestCPU->genReg[GenRegEAX] = (Bit32u) tsc;
          plex86GuestCPU->genReg[GenRegEDX] = tsc>>32;
          tsc += 10; // Fixme: hack for now.
          goto advanceInstruction;
#endif
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
return 0; // defer to user space implementation for now.
#if 0
      unsigned selector;

      if ( opsize == 0 ) {
        fprintf(stderr, "emulateSysInstr: POP DS, 16-bit opsize.\n");
        return 0;
        }
      selector = getGuestWord( plex86GuestCPU->genReg[GenRegESP] );
      if ( loadGuestSegment(SRegDS, selector) ) {
        plex86GuestCPU->genReg[GenRegESP] += 4;
        goto advanceInstruction;
        }
      return 0;
#endif
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
return 0; // defer to user space implementation for now.
#if 0
      unsigned modrm, rm, sreg, selector;

// Fix this fetch.
      modrm = opcode[iLen++];
      rm   = modrm & 7;
      sreg = (modrm>>3) & 7;
      if ( (sreg==SRegCS) || (sreg>SRegGS) ) {
        fprintf(stderr, "emulateSysInstr: MOV_SwEw bad sreg=%u.\n", sreg);
        return 0; // Fail.
        }
      if ( (modrm & 0xc0) == 0xc0 ) {
        selector = plex86GuestCPU->genReg[rm];
        if (loadGuestSegment(sreg, selector))
          goto advanceInstruction;
        }
      else {
        fprintf(stderr, "emulateSysInstr: MOV_SwEw mod!=11b.\n");
        return 0; // Fail.
        }
      break;
#endif
      }

    case 0x9b: // FWAIT
      {
      if (iLen == 1) // Prevent endless string of prefixes doing overrun.
        goto decodeOpcode;
      return 0;
      }

    case 0xcd: // int Ib
      {
return 0; // defer to user space implementation for now.
#if 0
      unsigned Ib;

      Ib = opcode[iLen++];
      plex86GuestCPU->eip += iLen; // Commit instruction length.
      if (Ib == 0xff) {
        // This is the special "int $0xff" call for interfacing with the HAL.
        halCall();
        return 1; // OK
        }
      else {
        doGuestInterrupt(vm, Ib, IntFlagSoftInt, 0);
        return 1; // OK
        }
#endif
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
return 0; // defer to user space implementation for now.
#if 0
      unsigned port8;
      Bit8u    al;

      port8 = opcode[iLen++];
      al = inp(1, port8);
      plex86GuestCPU->genReg[GenRegEAX] &= ~0xff;
      plex86GuestCPU->genReg[GenRegEAX] |= al;
      goto advanceInstruction;
#endif
      }

    case 0xe6: // OUT_IbAL
      {
return 0; // defer to user space implementation for now.
#if 0
      unsigned port8;

      port8 = opcode[iLen++];
      outp(1, port8, plex86GuestCPU->genReg[GenRegEAX] & 0xff);
      goto advanceInstruction;
#endif
      }

    case 0xea: // JMP_Ap (IdIw)
      {
return 0; // defer to user space implementation for now.
#if 0
      Bit32u offset;
      unsigned cs;
      offset = * (Bit32u*) &opcode[iLen];
      cs     = * (Bit16u*) &opcode[iLen+4];
      iLen += 6;
      if ( loadGuestSegment(SRegCS, cs) ) {
        plex86GuestCPU->eip = offset;
        return 1; // OK.
        }
      return 0;
#endif
      }

    case 0xec: // IN_ALDX
      {
return 0; // defer to user space implementation for now.
#if 0
      unsigned dx, al;

      dx = plex86GuestCPU->genReg[GenRegEDX] & 0xffff;
      al = inp(1, dx);
      plex86GuestCPU->genReg[GenRegEAX] &= ~0xff;
      plex86GuestCPU->genReg[GenRegEAX] |= al;
      goto advanceInstruction;
#endif
      }

    case 0xee: // OUT_DXAL
      {
return 0; // defer to user space implementation for now.
#if 0
      unsigned dx, al;

      dx = plex86GuestCPU->genReg[GenRegEDX] & 0xffff;
      al = plex86GuestCPU->genReg[GenRegEAX] & 0xff;
      outp(1, dx, al);
      goto advanceInstruction;
#endif
      }

    case 0xef: // OUT_DXeAX
      {
return 0; // defer to user space implementation for now.
#if 0
      unsigned dx;
      Bit32u  eax;

      dx  = plex86GuestCPU->genReg[GenRegEDX] & 0xffff;
      eax = plex86GuestCPU->genReg[GenRegEAX];
      if ( opsize==0 ) { // 16-bit opsize.
        eax &= 0xffff;
        outp(2, dx, eax);
        }
      else {
        outp(4, dx, eax);
        }
      goto advanceInstruction;
#endif
      }

    case 0xf0: // LOCK (used to make IRET fault).
      {
      if (iLen == 1) // Prevent endless string of prefixes doing overrun.
        goto decodeOpcode;
      return 0;
      }

    case 0xf4: // HLT
      {
return 0; // defer to user space implementation for now.
#if 0
      if ( !(plex86GuestCPU->eflags & FlgMaskIF) ) {
        fprintf(stderr, "HLT with IF==0.\n");
        return 0;
        }
      while (1) {
        if ( plex86GuestCPU->INTR )
          break;
        // Fixme: For now I expire 1 pit clock, which is a good number of
        // cpu cycles, but nothing is going on anyways.
        //tsc += cpuToPitRatio;
        pitExpireClocks( 1 );
        }
      goto advanceInstruction;
#endif
      }

    case 0xfb: // STI (PVI triggers this when VIF is asserted.
      {
return 0; // defer to user space implementation for now.
#if 0
      unsigned vector;

      plex86GuestCPU->eip += iLen; // Commit instruction length.
      plex86GuestCPU->eflags |= FlgMaskIF;
      if ( !plex86GuestCPU->INTR ) {
        fprintf(stderr, "STI: INTR=0?.\n");
        return 0;
        }
      vector = picIAC(); // Get interrupt vector from PIC.
      doGuestInterrupt(vm, vector, 0, 0);
      return 1;
#endif
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
  phyAddr_t pAddr, pOff;
  unsigned b0, iLen=0;
  Bit8u    opcode[16];
  Bit32u  *opcodeDWords;
  // modRM_t  modRM;
  // unsigned opsize = 1; // 32-bit opsize by default.
  Bit32u   lAddr;
  guest_cpu_t *guestCpu = vm->guest.addr.guest_cpu;

  if ( guestCpu->cr0.fields.pg == 0 ) {
    monpanic(vm, "emulateUserInstr: cr0.pg==0.\n");
    }
  lAddr = EIP; // Forget segmentation base for Linux.
  pAddr = translateLinToPhy(vm, lAddr);
  if (pAddr == LinAddrNotAvailable) {
    monpanic(vm, "emulateUserInstr: lin-->phy translation failed (0x%x).\n",
             lAddr);
    }
  if ( pAddr >= (vm->pages.guest_n_bytes-16) ) {
    monpanic(vm, "emulateUserInstr: physical address of 0x%x "
             "beyond memory.\n", pAddr);
    }
  pOff = pAddr & 0xfff; // Physical address page offset.
  if ( pOff <= 0xff0 ) {
    Bit8u *phyPagePtr;

    // Draw in 16 bytes from guest memory into a local array so we don't
    // have to worry about edge conditions when decoding.  All accesses are
    // within a single physical page.
    // Fixme: we could probably use the linear address (corrected for
    // Fixme: monitor CS.base) to access the page without creating a window.
    /* Open a window into guest physical memory. */
    phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr>>12,
                              vm->guest.addr.tmp_phy_page0);
    opcodeDWords = (Bit32u*) &opcode[0];
    opcodeDWords[0] = * (Bit32u*) &phyPagePtr[pOff+ 0];
    opcodeDWords[1] = * (Bit32u*) &phyPagePtr[pOff+ 4];
    opcodeDWords[2] = * (Bit32u*) &phyPagePtr[pOff+ 8];
    opcodeDWords[3] = * (Bit32u*) &phyPagePtr[pOff+12];
    }
  else {
    monpanic(vm, "emulateUserInstr: possible page crossing.\n");
    }

// decodeOpcode:

  b0 = opcode[iLen++];
  //fprintf(stderr, "instruction @ eip=0x%x is 0x%x\n", plex86GuestCPU->eip, b0);
  switch (b0) {

    case 0xcd: // IntIb
      {
      unsigned Ib;

      Ib = opcode[iLen++];
      EIP += iLen; // Commit instruction length.
      doGuestInterrupt(vm, Ib, IntFlagSoftInt, 0);
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
#endif

#if 0
  unsigned
setDRx(unsigned drx, Bit32u val32)
{
  fprintf(stderr, "setDR%u: ignoring val=0x%x.\n", drx, val32);
  // Fixme: faked out for now.
  return 1; // OK.
}
#endif

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

  descPtr = fetchGuestDescBySel(vm, selector, &desc);
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
      monpanic(vm, "loadGuestSegment: sreg = CS.\n", sreg);
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

  Bit32u
getGuestDWord(vm_t *vm, Bit32u lAddr)
{
  phyAddr_t pAddr;
  Bit8u    *phyPagePtr;

  pAddr = translateLinToPhy(vm, lAddr);

  if (pAddr == LinAddrNotAvailable) {
    monpanic(vm, "getGuestDWord: could not translate address.\n");
    }
  if ( pAddr >= (vm->pages.guest_n_bytes-3) ) {
    monpanic(vm, "getGuestDWord: phy address OOB.\n");
    }
  if ( (pAddr & 0xfff) >= 0xffd ) {
    monpanic(vm, "getGuestDWord: crosses page boundary.\n");
    }
  phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr>>12,
                            vm->guest.addr.tmp_phy_page0);
  return( * (Bit32u*) &phyPagePtr[pAddr & 0xfff] );
}

  Bit16u
getGuestWord(vm_t *vm, Bit32u lAddr)
{
  phyAddr_t pAddr;
  Bit8u    *phyPagePtr;

  pAddr = translateLinToPhy(vm, lAddr);

  if (pAddr == LinAddrNotAvailable) {
    monpanic(vm, "getGuestWord: could not translate address.\n");
    }
  if ( pAddr >= (vm->pages.guest_n_bytes-1) ) {
    monpanic(vm, "getGuestWord: phy address OOB.\n");
    }
  if ( (pAddr & 0xfff) >= 0xfff ) {
    monpanic(vm, "getGuestWord: crosses page boundary.\n");
    }
  phyPagePtr = (Bit8u*) open_guest_phy_page(vm, pAddr>>12,
                            vm->guest.addr.tmp_phy_page0);
  return( * (Bit16u*) &phyPagePtr[pAddr & 0xfff] );
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
fetchGuestDescBySel(vm_t *vm, selector_t sel, descriptor_t *desc)
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
  return( desc );
}


#if 0
  unsigned
inp(unsigned iolen, unsigned port)
{
  switch ( port ) {
    case 0x21:
      return( picInp(iolen, port) );
    case 0x40:
      return( pitInp(iolen, port) );
    case 0x61:
      return( pitInp(iolen, port) );

    case 0x71:
      return( cmosInp(iolen, port) );
    case 0x3d5:
    case 0x3da:
      return( vgaInp(iolen, port) );

    case 0x30:
      fprintf(stderr, "inp(0x30) ???\n");
      goto error;
      return(0xff);

    default:
      fprintf(stderr, "inp: port=0x%x unsupported.\n", port);
      goto error;
    }

error:
  plex86TearDown(); exit(1);
}
#endif

#if 0
  void
outp(unsigned iolen, unsigned port, unsigned val)
{
  //fprintf(stderr, "outp: port=0x%x, val=0x%x\n", port, val);
  switch ( port ) {
    case 0x20:
    case 0x21:
    case 0xa0:
    case 0xa1:
      picOutp( iolen, port, val );
      return;

    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
      pitOutp(1, port, val);
      return;

    case 0x61:
      pitOutp(1, port, val);
      return;

    case 0x70:
      cmosOutp(1, port, val);
      return;

    case 0x80:
      port0x80 = val;
      //fprintf(stderr, "Port0x80 = 0x%02x\n", port0x80);
      return;

    case 0x3c0:
    case 0x3c8:
    case 0x3c9:
    case 0x3d4:
    case 0x3d5:
      vgaOutp(1, port, val);
      return;

    case 0x3f2:
      fprintf(stderr, "outp(0x%x)=0x%x unsupported.\n", port, val);
      return;

    default:
      fprintf(stderr, "outp(0x%x)=0x%x unsupported.\n", port, val);
      goto error;
    }

error:
  plex86TearDown(); exit(1);
}
#endif


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

  // fprintf(stderr, "doGuestInterrupt: vector=%u.\n", vector);
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
  csDescPtr = fetchGuestDescBySel(vm, csSel, &csDesc);
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
    ssDescPtr = fetchGuestDescBySel(vm, ssSel, &ssDesc);
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
    fprintf(stderr, "doIRET: VIF=1.\n");
    goto error;
    }
  if ( plex86GuestCPU->eflags & FlgMaskNT ) {
    // NT=1 means return from nested task.
    fprintf(stderr, "doIRET: NT=1.\n");
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
    fprintf(stderr, "doIRET: VIF!=IF: 0x%x.\n", retEFlags);
    }

  if ( retEFlags & FlgMaskVM ) {
    // IRET to v86 mode not supported.
    fprintf(stderr, "doIRET: return EFLAGS.VM=1.\n");
    goto error;
    }
  if ( retEFlags & FlgMaskIOPL ) {
    // IRET eflags value has IOPL non-zero.
    fprintf(stderr, "doIRET: return EFLAGS.IOPL=%u.\n", (retEFlags>>12)&3);
    goto error;
    }
  if ( (retCS.raw & 0xfffc) == 0 ) {
    fprintf(stderr, "doIRET: return CS NULL.\n");
    goto error;
    }
  if ( ((retCS.fields.rpl!=0) && (retCS.fields.rpl!=3)) || retCS.fields.ti ) {
    fprintf(stderr, "doIRET: bad return CS=0x%x.\n", retCS.raw);
    goto error;
    }
  if ( retCS.fields.rpl < fromCPL ) {
    // Can not IRET to an inner ring.
    fprintf(stderr, "doIRET: to rpl=%u from CPL=%u.\n", retCS.fields.rpl, CPL);
    goto error;
    }
  csDescPtr = fetchGuestDescBySel(vm, retCS);
  if ( (csDescPtr==NULL) ||
       (csDescPtr->type!=0x1a) ||
       (csDescPtr->dpl!=retCS.fields.rpl) ||
       (csDescPtr->p==0) ) {
    fprintf(stderr, "doIRET: bad CS descriptor, type=0x%x, "
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
//fprintf(stderr, "doIRET: rpl=%u.\n", retCS.fields.rpl);

    retESP    = getGuestDWord(esp+12);
    retSS.raw = getGuestWord(esp+16);
    if ( (retSS.raw & 0xfffc) == 0 ) {
      fprintf(stderr, "doIRET: return SS NULL.\n");
      goto error;
      }
    if ( retSS.fields.rpl != retCS.fields.rpl ) {
      fprintf(stderr, "doIRET: SS.rpl!=CS.rpl.\n");
      goto error;
      }
    ssDescPtr = fetchGuestDescBySel(vm, retSS);
    if ( (ssDescPtr==NULL) ||
         (ssDescPtr->type!=0x12) ||
         (ssDescPtr->dpl!=retCS.fields.rpl) ||
         (ssDescPtr->p==0) ) {
      fprintf(stderr, "doIRET: bad SS descriptor, type=0x%x, "
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
//  fprintf(stderr, "iret to ring3, cs.slot=%u, eip=0x%x.\n",
//          retCS.fields.index, retEIP);
//  }

  changeMask = FlgMaskID | FlgMaskAC | FlgMaskRF | FlgMaskNT |
               FlgMaskOF | FlgMaskDF | FlgMaskTF | FlgMaskSF |
               FlgMaskZF | FlgMaskAF | FlgMaskPF | FlgMaskCF;
// Fixme: remove
if (changeMask != 0x254dd5) {
  fprintf(stderr, "changeMask != 0x254dd5.\n");
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


#if 0
  unsigned
doWRMSR(void)
{
  Bit32u ecx, edx, eax;

  // MSR[ECX] <-- EDX:EAX
  // #GP(0): cpuid.msr==0, reserved or unimplemented MSR addr.
  ecx = plex86GuestCPU->genReg[GenRegECX]; // MSR #
  edx = plex86GuestCPU->genReg[GenRegEDX]; // High dword
  eax = plex86GuestCPU->genReg[GenRegEAX]; // Low  dword

  switch ( ecx ) {
    case MSR_IA32_SYSENTER_CS:
      // fprintf(stderr, "WRMSR[IA32_SYSENTER_CS]: 0x%x\n", eax);
      sysEnter.cs = eax;
      return 1;
    case MSR_IA32_SYSENTER_ESP:
      // fprintf(stderr, "WRMSR[IA32_SYSENTER_ESP]: 0x%x\n", eax);
      sysEnter.esp = eax;
      return 1;
    case MSR_IA32_SYSENTER_EIP:
      // fprintf(stderr, "WRMSR[IA32_SYSENTER_EIP]: 0x%x\n", eax);
      sysEnter.eip = eax;
      return 1;

    default:
      fprintf(stderr, "WRMSR[0x%x]: unimplemented MSR.\n",
              plex86GuestCPU->genReg[GenRegECX]);
      //doGuestInterrupt(vm, ExceptionGP, IntFlagPushError, 0);
      //return 0; // Fault occurred.
      // Fixme: should assert RF in a separate exception routine.
      break;
    }

//error:
  plex86TearDown(); exit(1);
}
#endif

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
