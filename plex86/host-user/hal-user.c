/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2004 Kevin P. Lawton

#include <stdio.h>

#include "plex86.h"
#include "linuxvm.h"
#include "hal.h"
#include "hal-user.h"


  unsigned
initHal(char *tunScript, diskParams_t *diskParams)
{
  if ( !halNetInit(tunScript) )
    return(0);
  if ( !halDiskInit(diskParams) )
    return(0);
  if ( !halConInit() )
    return(0);

  return(1); // OK
}

  unsigned
halCall(void)
{
  unsigned callNo;

  // fprintf(stderr, "halCall: call=%u, device=%u, packetAddr=0x%x, len=%u.\n",
  //         plex86GuestCPU->genReg[GenRegEAX],
  //         plex86GuestCPU->genReg[GenRegEBX],
  //         plex86GuestCPU->genReg[GenRegECX],
  //         plex86GuestCPU->genReg[GenRegEDX]);

  callNo = plex86GuestCPU->genReg[GenRegEAX];

  switch ( callNo ) {

    case HalCallNetGuestRegDev:
    case HalCallNetGuestTx:
      halNetCall( callNo );
      return(1);

    case HalCallConGuestRegDev:
    case HalCallConGuestWrite:
      halConCall( callNo );
      return(1);

    case HalCallDiskGetInfo:
    case HalCallDiskWrite:
    case HalCallDiskRead:
    case HalCallDiskCleanup:
      halDiskCall( callNo );
      return(1);

    default:
      fprintf(stderr, "halCall(%u) unknown.\n", callNo);
      return(0);
    }
}
