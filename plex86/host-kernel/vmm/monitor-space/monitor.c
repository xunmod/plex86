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

  void
toHostFlushPrintBuf(vm_t *vm)
{
  CLI();
  vm->mon_request = MonReqFlushPrintBuf;
  vm->guest.__mon2host();
  STI();
}

  void
toHostRemapMonitor(vm_t *vm)
{
  CLI();
  vm->mon_request = MonReqRemapMonitor;
  vm->guest.__mon2host();
  STI();
}

  void
toHostGuestFault(vm_t *vm, unsigned fault, unsigned errorCode)
{
  CLI();
  vm->mon_request = MonReqGuestFault;
  vm->guestFaultNo    = fault;
  vm->guestFaultError = errorCode;
  vm->guest.__mon2host();
  STI();
}

  void
toHostPinUserPage(vm_t *vm, Bit32u ppi)
{
  CLI();
  vm->mon_request = MonReqPinUserPage;
  vm->pinReqPPI = ppi;
  vm->guest.__mon2host();
  STI();
}
