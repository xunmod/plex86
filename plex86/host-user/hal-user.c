/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton

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
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/wait.h>

#include "plex86.h"
#include "linuxvm.h"
#include "hal.h"
#include "hal-user.h"

static int halDiskFileNo[HalDiskMaxDisks];

//#define TUNTAP_SPEW 1

#define HalNet0Irq 3


static unsigned initTunTap(char *tunScript);
static void tuntapDev0SigIO(int sig);
static int execute_script(char *name, char* arg1);

//static int bridge_term = 0;
int fdTunTap = -1;
static struct sigaction sa;

volatile unsigned tunTapInService = 0;
volatile unsigned tunTapEvent = 0;


struct {
  unsigned  registered;

  // The guest must register a packet receive buffer in guest physical
  // memory, where we can transfer packets to.
  phyAddr_t            guestRxAreaPAddr;
  unsigned             guestRxAreaLen;

  // A conviences pointer directly into the guest physical memory.
  halNetGuestRxArea_t *guestRxArea;
  } halNetDev[HalNetMaxDevices];

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
initHal(char *tunScript)
{
  memset( &halNetDev[0], 0, sizeof(halNetDev) );
  return( initTunTap( tunScript ) );
}

  void
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

    // The guest requests registereing of this network device.
    case HalCallNetGuestRegDev:
      {
      unsigned deviceNo, rxAreaPAddr, rxAreaLen;

      deviceNo    = plex86GuestCPU->genReg[GenRegEBX];
      rxAreaPAddr = plex86GuestCPU->genReg[GenRegECX];
      rxAreaLen   = plex86GuestCPU->genReg[GenRegEDX];
      if ( deviceNo >= HalNetMaxDevices ) {
        fprintf(stderr, "HalCallNetGuestRegDev: deviceNo=%u.\n", deviceNo);
        goto error;
        }
      if ( halNetDev[deviceNo].registered ) {
        fprintf(stderr, "HalCallNetGuestRegDev: deviceNo %u already "
                        "registered.\n", deviceNo);
        goto error;
        }
      if ( rxAreaLen < sizeof(halNetGuestRxArea_t) ) {
        fprintf(stderr, "HalCallNetGuestRegDev: rxAreaLen(%u) too small.\n",
                        rxAreaLen);
        goto error;
        }
      if ( rxAreaPAddr >= (plex86MemSize - sizeof(halNetGuestRxArea_t)) ) {
        fprintf(stderr, "HalCallNetGuestRegDev: rxAreaPAddr(0x%x) past "
                        "guest physical memory limit.\n", rxAreaPAddr);
        goto error;
        }
      halNetDev[deviceNo].registered = 1;
      halNetDev[deviceNo].guestRxAreaPAddr = rxAreaPAddr;
      halNetDev[deviceNo].guestRxAreaLen   = rxAreaLen;
      halNetDev[deviceNo].guestRxArea =
          (halNetGuestRxArea_t *) &plex86MemPtr[rxAreaPAddr];
      plex86GuestCPU->genReg[GenRegEAX] = 1; // Success.
      return;
      }

    // The guest is transmitting a packet.
    case HalCallNetGuestTx:
      {
      unsigned deviceNo, packetPAddr, packetLen;
      // Bit8u   *rxBuffer;
      Bit8u   *txBuffer;
      int ret;
      // unsigned i;
      // Bit8u   *macHdr, temp8;
      // Bit32u  *ipAddr, temp32;

      deviceNo    = plex86GuestCPU->genReg[GenRegEBX];
      packetPAddr = plex86GuestCPU->genReg[GenRegECX];
      packetLen   = plex86GuestCPU->genReg[GenRegEDX];
      if ( deviceNo >= HalNetMaxDevices ) {
        fprintf(stderr, "halCallNetGuestTx: deviceNo=%u.\n", deviceNo);
        goto error;
        }
      if ( halNetDev[deviceNo].registered==0 ) {
        fprintf(stderr, "halCallNetGuestTx: deviceNo %u no registered.\n",
                deviceNo);
        goto error;
        }
      if ( packetLen > MaxEthernetFrameSize ) {
        fprintf(stderr, "halCallNetGuestTx: packetLen=%u.\n", packetLen);
        goto error;
        }
      if ( packetPAddr >= (plex86MemSize - packetLen) ) {
        fprintf(stderr, "HalCallNetGuestTx: packetPAddr(0x%x) past "
                        "guest physical memory limit.\n", packetPAddr);
        goto error;
        }

#if 0
      // For now, copy the guest Tx packet to the Rx buffer and swap
      // both the IP and ethernet addresses to simulate another host
      // returning ping packets.
      rxBuffer = halNetDev[deviceNo].guestRxArea->rxBuffer;
      memcpy(rxBuffer, &plex86MemPtr[packetPAddr], packetLen);

      // Swap ethernet source/destination addresses.
      macHdr = (Bit8u *) (rxBuffer + 0);
      for (i=0; i<6; i++) {
        temp8  = macHdr[i];
        macHdr[i] = macHdr[6 + i];
        macHdr[6 + i] = temp8;
        }

      // Swap IP source/destination addresses.
      ipAddr = (Bit32u *) (rxBuffer + 14 + 12);
      temp32 = ipAddr[0];
      ipAddr[0] = ipAddr[1];
      ipAddr[1] = temp32;

      // Signal the IRQ to the PIC.
      picIrq(HalNet0Irq, 1);

      // Now mark buffer with the new packet info.
      halNetDev[deviceNo].guestRxArea->rxBufferLen  = packetLen;
      halNetDev[deviceNo].guestRxArea->rxBufferFull = 1;
#endif

      // Write Tx packet to the TUN/TAP interface.
      txBuffer = &plex86MemPtr[packetPAddr];
#ifdef TUNTAP_SPEW
{
Bit8u *macHdr;
unsigned frameType;
macHdr = (Bit8u *) (txBuffer + 0);
frameType = (macHdr[12]<<8) | macHdr[13];
fprintf(stderr, "src: %02x:%02x:%02x:%02x:%02x:%02x -> "
                "dst: %02x:%02x:%02x:%02x:%02x:%02x len=%u frmtype=0x%04x\n",
        macHdr[6+0],
        macHdr[6+1],
        macHdr[6+2],
        macHdr[6+3],
        macHdr[6+4],
        macHdr[6+5],
        macHdr[0+0],
        macHdr[0+1],
        macHdr[0+2],
        macHdr[0+3],
        macHdr[0+4],
        macHdr[0+5],
        packetLen, frameType);
// u8[6] ether dest addr
// u8[6] ether src addr
// u16   frame type (0x0806=ARP)
// -----------------------------
// u16 hw type=1 (ethernet)
// u16 proto type=0x0800 (IP addr)
// u8  hwlen=6
// u8  proto len=4
// u16 op (1=ARP request, 2=ARP reply)
// u8[6] sender hw addr
// u8[4] sender proto addr
// u8[6] target hw addr
// u8[4] target proto addr
}
#endif
      ret = write(fdTunTap, txBuffer, packetLen);
      if ( ((unsigned)ret) != packetLen ) {
        fprintf(stderr, "HalCallNetGuestTx: write(%u) bytes to TUN/TAP "
                        " returned %d.\n", packetLen, ret);
        goto error;
        }

      plex86GuestCPU->genReg[GenRegEAX] = 1; // Success.
      return;
      }

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
      return;
      }

    default:
      fprintf(stderr, "halCall(%u) unknown.\n", callNo);
      goto error;
    }

error:
  plex86GuestCPU->genReg[GenRegEAX] = 0; // Fail.
  plex86TearDown();
}


  void
tuntapDev0SigIO(int sig) 
{
  if ( incrementAtomic(tunTapInService) == 1 ) {
    // We own the in-service semaphore.
    tunTapEvent = 1; // Signal main body that there are packets to read.
    tunTapInService = 0; // Reset in-service semaphore.
    }
  else {
    // The main thread must own the in-service semaphore.  Let it handle
    // reading the TUN/TAP data.
    fprintf(stderr, "tuntapDev0SigIO: did not get semaphore.\n");
    }
}

  int
execute_script( char* scriptname, char* arg1 )
{
  int childPID, waitPID, status;
  int exitReturnCode;

  if ( !(childPID = vfork()) ) {
    execle(scriptname, scriptname, arg1, NULL, NULL);
    // The script process should not get here.
    exit(-1);
    }

  /* Parent process: returned child PID. */
  waitPID = wait( &status );
  if ( waitPID != childPID ) {
    fprintf(stderr, "execute_script: wait() failed.\n");
    return -1;
    }

  if ( !WIFEXITED(status) ) {
    fprintf(stderr, "execute_script: script exited abnormally.\n");
    return -1;
    }
  // Only lower 8 bits of return code available using macro.  Convert
  // back to signed int value.
  exitReturnCode = (int) (char) WEXITSTATUS(status);
  return exitReturnCode;
}
 

  unsigned
initTunTap( char *scriptname )
{
  char * tuntapDevName = "/dev/net/tun";
  struct ifreq ifr;
  int err;

  fdTunTap = open(tuntapDevName, O_RDWR);
  if ( fdTunTap < 0 ) {
    fprintf(stderr, "Error opening tuntap device '%s'.\n", tuntapDevName);
    return(0); // Fail.
    }

  // IFF_TAP is for Ethernet frames.
  // IFF_TUN is for IP.
  // IFF_NO_PI is for not receiving extra meta packet information.
  ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
  strncpy(ifr.ifr_name, "tap%d", IFNAMSIZ);
  err = ioctl(fdTunTap, TUNSETIFF, (void *) &ifr);
  if ( err < 0 ) {
    close(fdTunTap);
    fprintf(stderr, "Error with ioctl(TUNSETIFF).\n");
    return(0); // Fail.
    }

  fprintf(stderr, "tuntap device returns interface name '%s'.\n", ifr.ifr_name);

  // Turn off checksumming, since this is a software-only tunnel; there is
  // no physical transmission media to corrupt the data.
  //ioctl(fdTunTap, TUNSETNOCSUM, 1); // Fixme: need checksumming?

  /* Execute the configuration script */
  if( (scriptname != NULL) &&
      (strcmp(scriptname, "") != 0) &&
      (strcmp(scriptname, "none") != 0) ) {
    if ( execute_script(scriptname, ifr.ifr_name) != 0 ) {
      close(fdTunTap);
      fprintf(stderr,"Error: execute script '%s' on %s failed.\n",
              scriptname, ifr.ifr_name);
      return(0); // Fail.
      }
    fprintf(stderr,"execute script '%s' on %s finished.\n",
            scriptname, ifr.ifr_name);
    }
  else {
    fprintf(stderr, "tuntap device '%s' ready, do commands now and type char.\n",
            tuntapDevName);
    getchar();
    fprintf(stderr, "continuing execution...\n");
    }
      

  fcntl(fdTunTap, F_SETFL, O_NONBLOCK | O_ASYNC);

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = tuntapDev0SigIO;
  sigaction(SIGIO, &sa, NULL); 

  //while( !bridge_term ) {
  //  sleep(1000); 
  //  }
  return(1); // OK
}

  unsigned
tuntapReadPacketToGuest(unsigned deviceNo)
{
  int packetLen;
  Bit8u *rxBuffer;
  static Bit8u rxDrain[MaxEthernetFrameSize];

  if ( halNetDev[deviceNo].registered ) {
    if ( halNetDev[deviceNo].guestRxArea->rxBufferFull==0 ) {
      rxBuffer = halNetDev[deviceNo].guestRxArea->rxBuffer;
      packetLen = read(fdTunTap, rxBuffer, MaxEthernetFrameSize);
      if ( packetLen > 0 ) {
#ifdef TUNTAP_SPEW
        Bit8u *macHdr;
        unsigned frameType;
        macHdr = (Bit8u *) (rxBuffer + 0);
        frameType = (macHdr[12]<<8) | macHdr[13];

fprintf(stderr, "dst: %02x:%02x:%02x:%02x:%02x:%02x <- "
                "src: %02x:%02x:%02x:%02x:%02x:%02x len=%u frmtype=0x%04x\n",
        macHdr[0+0],
        macHdr[0+1],
        macHdr[0+2],
        macHdr[0+3],
        macHdr[0+4],
        macHdr[0+5],
        macHdr[6+0],
        macHdr[6+1],
        macHdr[6+2],
        macHdr[6+3],
        macHdr[6+4],
        macHdr[6+5],
        packetLen, frameType);
#endif
        // If packets are less than the minimum ethernet framesize, then pad.
        // Fixme: I don't think we need to extend this.
        if (packetLen < 60)
          packetLen = 60; // Fixme: clear remaining bytes?

        halNetDev[deviceNo].guestRxArea->rxBufferFull = 1;
        halNetDev[deviceNo].guestRxArea->rxBufferLen  = packetLen;
        // Signal the IRQ to the PIC.
        //picIrq(HalNet0Irq, 1);
// Fixme: was picIrq(HalNet0Irq, 1);
        plex86GuestCPU->halIrq = HalNet0Irq;
        }
      else if ( (packetLen<0) && ((errno!=EAGAIN) && (errno!=EINTR)) ) {
        fprintf(stderr, "tuntapReadPacketToGuest: read error.\n");
        }
      else if ( packetLen==0 ) {
        // If we have exhausted the packets from TUN/TAP, only then
        // do we flag the main body to reset tunTapEvent.
        return 1; // OK.
        }
      }
    else {
#ifdef TUNTAP_SPEW
      fprintf(stderr, "tuntapReadPacketToGuest: buffer full.\n");
#endif
      }
    }
  else {
    // Guest network device is not registered.  Drain packets.
    while ( (packetLen = read(fdTunTap, rxDrain, MaxEthernetFrameSize)) > 0 ) {
      fprintf(stderr, "tuntapReadPacketToGuest: dev%u unregistered, dropping "
                      "packet of %d bytes.\n", deviceNo, packetLen);
      }
    // If we have exhausted the packets from TUN/TAP, only then
    // do we flag the main body to reset tunTapEvent.
    return 1;
    }
  return 0; // Do not reset tunTapEvent yet.

#if 0
  unsigned i, ret;
  Bit8u   *macHdr, temp8;
  Bit32u  *ipAddr, temp32;

  while( (packetLen = read(fdTunTap, rxBuffer, sizeof(rxBuffer))) > 0 ) {
    fprintf(stderr, "sig_io: read %d bytes.\n", packetLen);

    // Swap ethernet source/destination addresses.
    macHdr = (Bit8u *) (rxBuffer + 0);
    for (i=0; i<6; i++) {
      temp8  = macHdr[i];
      macHdr[i] = macHdr[6 + i];
      macHdr[6 + i] = temp8;
      }

    // Swap IP source/destination addresses.
    ipAddr = (Bit32u *) (rxBuffer + 14 + 12);
fprintf(stderr, "SrcIP: %02x.%02x.%02x.%02x --> "
                "DstIP: %02x.%02x.%02x.%02x\n",
                (ipAddr[0]>>0) & 0xff,
                (ipAddr[0]>>8) & 0xff,
                (ipAddr[0]>>16) & 0xff,
                (ipAddr[0]>>24) & 0xff,
                (ipAddr[1]>>0) & 0xff,
                (ipAddr[1]>>8) & 0xff,
                (ipAddr[1]>>16) & 0xff,
                (ipAddr[1]>>24) & 0xff);
    temp32 = ipAddr[0];
    ipAddr[0] = ipAddr[1];
    ipAddr[1] = temp32;

    // Write the modified packet back.
    ret = write(fdTunTap, rxBuffer, packetLen);
    fprintf(stderr, "write(%u bytes) returns %u bytes.\n",
            packetLen, ret);
    }

  if( packetLen < 0 && (errno != EAGAIN && errno != EINTR) ) {
    bridge_term = 1;
    return;
    }
#endif
}
