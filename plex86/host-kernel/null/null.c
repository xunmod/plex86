/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2003 Kevin P. Lawton
 *
 *  null.c: host OS specific stubs.  These provide a reference for
 *    ports of plex86 to various host OSes.
 *
 */

#include "plex86.h"
#define IN_HOST_SPACE
#include "monitor.h"

/* Note: for comments on what various functions are expected to do, as
 *   well as a reference implemntation, read the '../linux2.4.c' file.
 *   It's likely the most up-to-date.
 */




#define NULL 0

  int
main(int argc, char *argv[])
{
  vm_t *vm = NULL;

  hostModuleInit();
  hostDeviceOpen(vm);
  hostIoctlGeneric(vm, NULL, NULL, 0, 0);
  return(0);
}

  void
hostOSReservePhyPages(vm_t *vm, Bit32u *hostPhyPages, unsigned nPages)
{
}

  void
hostOSUnreservePhyPages(vm_t *vm, Bit32u *hostPhyPages, unsigned nPages)
{
}



  unsigned
hostOSIdle(void)
{
  return 0;
}

  void *
hostOSAllocZeroedMem(unsigned long size)
{
  return 0;
}

  void
hostOSFreeMem(void *ptr)
{
}

  void *
hostOSAllocZeroedPage(void)
{
  return 0;
}

  void
hostOSFreePage(void *ptr)
{
}


  unsigned
hostOSGetAllocedMemPhyPages(Bit32u *page, int max_pages, void *ptr, unsigned size)
{
  return 0;
}

  Bit32u
hostOSGetAllocedPagePhyPage(void *ptr)
{
  return 0;
}

  void
hostOSPrint(char *fmt, ...)
{
}


  int
hostOSConvertPlex86Errno(unsigned ret)
{
  return 0;
}


  Bit32u
hostOSKernelOffset(void)
{
  return 0;
}

  void
hostOSModuleCountReset(vm_t *vm, void *inode, void *filp)
{
}

  unsigned long
hostOSCopyFromUser(void *to, void *from, unsigned long len)
{
  return 0;
}

  unsigned long
hostOSCopyToUser(void *to, void *from, unsigned long len)
{
  return 0;
}

  unsigned long
hostOSCopyFromUserIoctl(void *to, void *from, unsigned long len)
{
  return 0;
}

  unsigned long
hostOSCopyToUserIoctl(void *to, void *from, unsigned long len)
{
  return 0;
}

  Bit32u
hostOSGetAndPinUserPage(vm_t *vm, Bit32u userAddr, void **osSpecificPtr,
                      Bit32u *ppi, Bit32u *kernelAddr)
{
  return 0;
}

  void
hostOSUnpinUserPage(vm_t *vm, Bit32u userAddr, void *osSpecificPtr,
                          Bit32u ppi, Bit32u *kernelAddr, unsigned dirty)
{
}

  void
hostOSInstrumentIntRedirCount(unsigned interruptVector)
{
}

  void
hostOSKernelPrint(char *fmt, ...)
{
}
