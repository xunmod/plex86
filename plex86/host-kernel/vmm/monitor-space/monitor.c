/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2003 Kevin P. Lawton
 *
 *  monitor-mon.c: Monitor functions which transfer control back
 *    to the host space.
 *
 */

#include "plex86.h"
#define IN_MONITOR_SPACE
#include "monitor.h"

static inline Bit32u saveEFlagsCLI(void)
{
  Bit32u eflags;

  __asm__ volatile (
    "pushfl \n\t"
    "popl %0 \n\t"
    "cli"
      : "=g" (eflags)
      : // no inputs
      : "memory"
    );
  return(eflags);
}

static inline void restoreIF(Bit32u eflags)
{
  if ( eflags & FlgMaskIF )
    __asm__ volatile (
      "sti"
      : // no outputs
      : // no inputs
      : "memory"
      );
}

  void
toHostFlushPrintBuf(vm_t *vm)
{
  Bit32u eflags = saveEFlagsCLI();

  vm->mon_request = MonReqFlushPrintBuf;
  vm->guest.__mon2host();

  restoreIF(eflags);
}

  void
toHostRemapMonitor(vm_t *vm)
{
  Bit32u eflags = saveEFlagsCLI();

  vm->mon_request = MonReqRemapMonitor;
  vm->guest.__mon2host();

  restoreIF(eflags);
}

  void
toHostGuestFault(vm_t *vm, unsigned fault, unsigned errorCode)
{
  Bit32u eflags = saveEFlagsCLI();

  vm->mon_request = MonReqGuestFault;
  vm->guestFaultNo    = fault;
  vm->guestFaultError = errorCode;
  vm->guest.__mon2host();

  restoreIF(eflags);
}

  void
toHostPinUserPage(vm_t *vm, Bit32u ppi)
{
  Bit32u eflags = saveEFlagsCLI();

  vm->mon_request = MonReqPinUserPage;
  vm->pinReqPPI = ppi;
  vm->guest.__mon2host();

  restoreIF(eflags);
}

  void
toHostHalCall(vm_t *vm)
{
  Bit32u eflags = saveEFlagsCLI();

  vm->mon_request = MonReqHalCall;
  vm->guest.__mon2host();

  restoreIF(eflags);
}
