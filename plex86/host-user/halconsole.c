/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2004 Kevin P. Lawton

#include <stdio.h>
#include <sys/time.h>
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
#include <errno.h>
#include <sys/wait.h>

#include "plex86.h"
#include "linuxvm.h"
#include "hal.h"
#include "hal-user.h"


struct {
  unsigned  registered;
  phyAddr_t guestRwAreaPAddr;
  unsigned  guestRwAreaLen;
  halConGuestRwArea_t *guestRwArea;
  } halConDev[HalConMaxDevices];

// Handled up to the following # of chars per line.  One extra since
// Linux is spewing 80 chars, then a newline in a separate request.  If
// we print when we get to 80, we'll end up printing an extra newline.
#define LineLen 81

/* Allow for preceding "> " and following newline + NULL char. */
static unsigned char halConLine[LineLen+2];
static unsigned      halConLineI = 0;

  unsigned
halConInit(void)
{
  return(1); // OK
}

  void
halConCall( unsigned callNo )
{
  switch ( callNo ) {

    case HalCallConGuestRegDev:
      {
      unsigned deviceNo, rwAreaPAddr, rwAreaLen;

      deviceNo    = plex86GuestCPU->genReg[GenRegEBX];
      rwAreaPAddr = plex86GuestCPU->genReg[GenRegECX];
      rwAreaLen   = plex86GuestCPU->genReg[GenRegEDX];
      if ( deviceNo >= HalConMaxDevices ) {
        fprintf(stderr, "HalCallConGuestRegDev: deviceNo=%u.\n", deviceNo);
        goto error;
        }
      if ( halConDev[deviceNo].registered ) {
        fprintf(stderr, "HalCallConGuestRegDev: deviceNo %u already "
                        "registered.\n", deviceNo);
        goto error;
        }
      if ( rwAreaLen < sizeof(halConGuestRwArea_t) ) {
        fprintf(stderr, "HalCallConGuestRegDev: rwAreaLen(%u) too small.\n",
                        rwAreaLen);
        goto error;
        }
      if ( rwAreaPAddr >= (plex86MemSize - sizeof(halConGuestRwArea_t)) ) {
        fprintf(stderr, "HalCallConGuestRegDev: rwAreaPAddr(0x%x) past "
                        "guest physical memory limit.\n", rwAreaPAddr);
        goto error;
        }
      halConDev[deviceNo].registered = 1;
      halConDev[deviceNo].guestRwAreaPAddr = rwAreaPAddr;
      halConDev[deviceNo].guestRwAreaLen   = rwAreaLen;
      halConDev[deviceNo].guestRwArea =
          (halConGuestRwArea_t *) &plex86MemPtr[rwAreaPAddr];
      plex86GuestCPU->genReg[GenRegEAX] = 1; // Success.
      return;
      }

    case HalCallConGuestWrite:
      {
      unsigned bufferI;
      unsigned deviceNo, writeLen;
      unsigned char c;

      deviceNo   = plex86GuestCPU->genReg[GenRegEBX];
      writeLen   = plex86GuestCPU->genReg[GenRegEDX];
      if ( deviceNo >= HalConMaxDevices ) {
        fprintf(stderr, "HalCallConGuestWrite: deviceNo=%u.\n", deviceNo);
        goto error;
        }
      if ( writeLen > sizeof(halConDev[0].guestRwArea->txBuffer) ) {
        fprintf(stderr, "HalCallConGuestWrite: writeLen(%u) OOB.\n", writeLen);
        goto error;
        }

      bufferI = 0;
      while ( writeLen ) {
        if ( halConLineI >= LineLen ) {
          // If previous characters filled the buffer, terminate them
          // with a newline+NULL and print them out first, before processing
          // the rest of the request.
          halConLine[halConLineI++] = '\n'; // A newline.
          halConLine[halConLineI]   = '\0'; // Terminate string.
          fputs("> ", stderr);
          fputs(halConLine, stderr);
          halConLineI = 0; // Reset index.
          }
        c = halConDev[0].guestRwArea->txBuffer[bufferI++];
        writeLen --;
        if ( c == '\n' ) {
          halConLine[halConLineI++] = c;    // The newline.
          halConLine[halConLineI++] = '\0'; // Terminate string.
          fputs("> ", stderr);
          fputs(halConLine, stderr);
          halConLineI = 0; // Reset index.
          }
        else if ( isgraph(c) )
          halConLine[halConLineI++] = c; // Normal chars get passed through.
        else
          halConLine[halConLineI++] = ' '; // Transformed others to a space.
        }

      plex86GuestCPU->genReg[GenRegEAX] = 1; // Success.
      return;
      }

    default:
      fprintf(stderr, "halCall(%u) unknown.\n", callNo);
      goto error;
    }

error:
  plex86GuestCPU->genReg[GenRegEAX] = 0; // Fail.
  plex86TearDown();
  exit(1); // Fixme: should have plex86Exit which calls plex86Teardown
}
