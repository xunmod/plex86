/*
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2003 Kevin P. Lawton
 *
 *  monitor-mon.c: Monitor functions which transfer control back
 *    to the host space.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
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
