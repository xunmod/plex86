/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 2003 Kevin P. Lawton
 *
 *  hal.h: Hardware Abstraction Layer interface.
 */

#ifndef __HAL_H__
#define __HAL_H__

// Maximum number of HAL Network Devices (channels).
#define HalNetMaxDevices 2
#define HalConMaxDevices 1

#define HalCallNetGuestTx     1
#define HalCallNetGuestRegDev 2
#define HalCallConGuestWrite  3
#define HalCallConGuestRegDev 4

// 1500 bytes data, 2x 6-byte addresses, 1x 2-byte type field.
#define MaxEthernetFrameSize 1514

  /* Note that this whole structure should not exceed 4096-bytes in size. */
typedef struct {
  unsigned  rxBufferFull; // Flag, 1 = guest has not retrieved packet yet.
  unsigned  rxBufferLen;  // Packet size.
  Bit8u     rxBuffer[MaxEthernetFrameSize];
  } halNetGuestRxArea_t;


  /* Note that this whole structure should not exceed 4096-bytes in size. */
typedef struct {
#define MaxConsoleFrameSize 1024
  unsigned  rxBufferFull; // Flag, 1 = guest has not retrieved packet yet.
  unsigned  rxBufferLen;  // Packet size.
  Bit8u     rxBuffer[MaxConsoleFrameSize];
  Bit8u     txBuffer[MaxConsoleFrameSize];
  } halConGuestRwArea_t;

#endif  /* __HAL_H__ */
