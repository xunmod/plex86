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
#include <limits.h>

#include "plex86.h"
#include "linuxvm.h"
#include "hal.h"
#include "hal-user.h"

static struct {
  unsigned enabled;
  int      fileNo;
  unsigned char pathName[NAME_MAX];
  unsigned numSectors;
  unsigned cylinders;
  unsigned heads;
  unsigned spt; // Sectors per track.
  } halDisk[HalDiskMaxDisks];


  unsigned
halDiskInit(diskParams_t *diskParams)
{
  unsigned unit;
  int n;

// "file=/tmp/hald0,sectors=4096,CHS=64/4/16"

  for (unit=0; unit<HalDiskMaxDisks; unit++) {
    if ( (*diskParams)[unit][0] ) {
      // User provided settings for this unit.
      unsigned char *ptr;

      ptr = (*diskParams)[unit];

      // Parameter line must start with "file=/path".
      if ( !strncmp(ptr, "file=", 5) ) {
        unsigned char *start;
        unsigned pathNameLen;

        ptr += 5; // Point to pathname.
        start = ptr; // Save start of pathname.
        while ( *ptr && (*ptr!=',') ) {
          ptr++;
          }
        pathNameLen = (ptr - start);
        if (pathNameLen==0) {
          goto usage;
          }
        if (pathNameLen >= sizeof(halDisk[unit].pathName)) {
          goto usage;
          }
        strncpy(halDisk[unit].pathName, start, pathNameLen);
        halDisk[unit].pathName[sizeof(halDisk[unit].pathName)-1] = 0;
        if ( *ptr ) // Advance if was a ','.
          ptr++;
        }
      else {
        goto usage;
        }
      n = sscanf(ptr, "sectors=%u,chs=%u/%u/%u",
                 &halDisk[unit].numSectors,
                 &halDisk[unit].cylinders,
                 &halDisk[unit].heads,
                 &halDisk[unit].spt);
      if (n!=4) {
        goto usage;
        }
      if ( (halDisk[unit].numSectors==0) ||
           (halDisk[unit].cylinders==0) ||
           (halDisk[unit].heads==0) ||
           (halDisk[unit].spt==0) ) {
        goto usage;
        }
      if ( halDisk[unit].numSectors !=
           (halDisk[unit].cylinders *
            halDisk[unit].heads *
            halDisk[unit].spt) ) {
        // Sanity check.
        goto usage;
        }
      // User parameters OK.
      halDisk[unit].enabled = 1;
      }
    else {
      // User did not provide settings for this unit, clear all fields.
      memset( &((*diskParams)[unit]), 0, sizeof((*diskParams)[unit]) );
      }
    }
  return(1); // OK

usage:
  fprintf(stderr, "halDisk: option malformed.\n");
plex86TearDown();
// Fixme: exit() here?
  return(0); // Error.
}

  void
halDiskCall(unsigned callNo)
{
  switch ( callNo ) {

    case HalCallDiskGetInfo:
      {
      unsigned infoPAddr, unit;
      halDiskInfo_t *halDiskInfo;

      infoPAddr   = plex86GuestCPU->genReg[GenRegEBX];
      if ( infoPAddr >= (plex86MemSize - sizeof(halDiskInfo_t)) ) {
        fprintf(stderr, "HalCallDiskGetInfo: infoPAddr(0x%x) past "
                        "guest physical memory limit.\n", infoPAddr);
        goto error;
        }
      halDiskInfo = (halDiskInfo_t *) &plex86MemPtr[infoPAddr];
      for (unit=0; unit<HalDiskMaxDisks; unit++) {
        if (halDisk[unit].enabled) {
          halDiskInfo[unit].enabled         = halDisk[unit].enabled;
          halDiskInfo[unit].geom.cylinders  = halDisk[unit].cylinders;
          halDiskInfo[unit].geom.heads      = halDisk[unit].heads;
          halDiskInfo[unit].geom.spt        = halDisk[unit].spt;
          halDiskInfo[unit].geom.numSectors = halDisk[unit].numSectors;
          halDisk[unit].fileNo = open(halDisk[unit].pathName, O_RDWR, 0);
          if (halDisk[unit].fileNo < 0) {
            fprintf(stderr, "HalCallDiskGetInfo: could not open file '%s'.\n",
                    halDisk[unit].pathName);
            goto error;
            }
          }
        else {
          memset(&halDiskInfo[unit], 0, sizeof(halDiskInfo_t));
          halDisk[unit].fileNo = -1;
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
      if ( lseek(halDisk[unit].fileNo, sector<<9, SEEK_SET) < 0 ) {
        fprintf(stderr, "HalCallDiskWrite: lseek() failed.\n");
        goto error;
        }
      if ( write(halDisk[unit].fileNo, rwArea, 512) != 512 ) {
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
      if ( lseek(halDisk[unit].fileNo, sector<<9, SEEK_SET) < 0 ) {
        fprintf(stderr, "HalCallDiskRead: lseek() failed.\n");
        goto error;
        }
      if ( read(halDisk[unit].fileNo, rwArea, 512) != 512 ) {
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
        if ( halDisk[unit].fileNo >= 0 )
          close( halDisk[unit].fileNo );
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
