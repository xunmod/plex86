/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2004 Kevin P. Lawton

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "plex86.h"
#include "linuxvm.h"
#include "hal.h"
#include "hal-user.h"

static int halDiskFileNo[HalDiskMaxDisks];


  unsigned
halDiskInit(void)
{
  return(1); // OK
}

  void
halDiskCall(unsigned callNo)
{
  switch ( callNo ) {

    case HalCallDiskGetInfo:
      {
      unsigned infoPAddr, unit;
      halDiskInfo_t *halDiskInfo;
char pathName[HalDiskMaxDisks][64];

      infoPAddr   = plex86GuestCPU->genReg[GenRegEBX];
      if ( infoPAddr >= (plex86MemSize - sizeof(halDiskInfo_t)) ) {
        fprintf(stderr, "HalCallDiskGetInfo: infoPAddr(0x%x) past "
                        "guest physical memory limit.\n", infoPAddr);
        goto error;
        }
      halDiskInfo = (halDiskInfo_t *) &plex86MemPtr[infoPAddr];
      for (unit=0; unit<HalDiskMaxDisks; unit++) {
        if (unit <= 1) {
          unsigned sectors = (2048 * 1024 / 512);
          halDiskInfo[unit].exists = 1;
          halDiskInfo[unit].geom.cylinders = (sectors & ~0x3f) >> 6;
          halDiskInfo[unit].geom.heads = 4;
          halDiskInfo[unit].geom.spt = 16;
          halDiskInfo[unit].geom.numSectors = sectors;
sprintf(pathName[unit], "%s%u", "/tmp/hald", unit);
halDiskFileNo[unit] = open(pathName[unit], O_RDWR, 0);
if (halDiskFileNo[unit] < 0) {
  fprintf(stderr, "HalCallDiskGetInfo: could not open file '%s'.\n",
          pathName[unit]);
  goto error;
  }
          }
        else {
          memset(&halDiskInfo[unit], 0, sizeof(halDiskInfo_t));
halDiskFileNo[unit] = -1;
          }
        }
      return;
      }

    case HalCallDiskWrite:
      {
      unsigned bufferPAddr = plex86GuestCPU->genReg[GenRegEBX];
      unsigned sector      = plex86GuestCPU->genReg[GenRegECX];
      unsigned unit        = plex86GuestCPU->genReg[GenRegEDX];
      Bit8u *rwArea;

      if ( bufferPAddr >= (plex86MemSize - 512) ) {
        fprintf(stderr, "HalCallDiskWrite: bufferPAddr(0x%x) past "
                        "guest physical memory limit.\n", bufferPAddr);
        goto error;
        }
      if ( unit >= HalDiskMaxDisks ) {
        fprintf(stderr, "HalCallDiskWrite: unit(%u) OOB.\n", unit);
        goto error;
        }
// Fixme: other checks here.
      rwArea = (Bit8u *) &plex86MemPtr[bufferPAddr];
if ( lseek(halDiskFileNo[unit], sector<<9, SEEK_SET) < 0 ) {
  fprintf(stderr, "HalCallDiskWrite: lseek() failed.\n");
  goto error;
  }
if ( write(halDiskFileNo[unit], rwArea, 512) != 512 ) {
  fprintf(stderr, "HalCallDiskWrite: write() failed.\n");
  goto error;
  }

      // Result goes in EAX
      plex86GuestCPU->genReg[GenRegEAX] = 1; // OK.
      return;
      }

    case HalCallDiskRead:
      {
      unsigned bufferPAddr = plex86GuestCPU->genReg[GenRegEBX];
      unsigned sector      = plex86GuestCPU->genReg[GenRegECX];
      unsigned unit        = plex86GuestCPU->genReg[GenRegEDX];
      Bit8u *rwArea;

      if ( bufferPAddr >= (plex86MemSize - 512) ) {
        fprintf(stderr, "HalCallDiskRead: bufferPAddr(0x%x) past "
                        "guest physical memory limit.\n", bufferPAddr);
        goto error;
        }
      if ( unit >= HalDiskMaxDisks ) {
        fprintf(stderr, "HalCallDiskRead: unit(%u) OOB.\n", unit);
        goto error;
        }
// Fixme: other checks here.
      rwArea = (Bit8u *) &plex86MemPtr[bufferPAddr];
if ( lseek(halDiskFileNo[unit], sector<<9, SEEK_SET) < 0 ) {
  fprintf(stderr, "HalCallDiskRead: lseek() failed.\n");
  goto error;
  }
if ( read(halDiskFileNo[unit], rwArea, 512) != 512 ) {
  fprintf(stderr, "HalCallDiskRead: read() failed.\n");
  goto error;
  }

      // Result goes in EAX
      plex86GuestCPU->genReg[GenRegEAX] = 1; // OK.
      return;
      }

    case HalCallDiskCleanup:
      {
      unsigned unit;

      for (unit=0; unit<HalDiskMaxDisks; unit++) {
if ( halDiskFileNo[unit] >= 0 )
  close( halDiskFileNo[unit] );
        }

      // Result goes in EAX
      plex86GuestCPU->genReg[GenRegEAX] = 1; // OK.
      return;
      }

    default:
      fprintf(stderr, "halDiskCall(%u) unknown.\n", callNo);
      goto error;
    }

error:
  plex86GuestCPU->genReg[GenRegEAX] = 0; // Fail.
  plex86TearDown();
  // Fixme: exit() here?
}
