/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 2003 Kevin P. Lawton
 *
 *  hal.h: Hardware Abstraction Layer interface.
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

#ifndef __HAL_H__
#define __HAL_H__

// Maximum number of HAL Network Devices (channels).
#define HalNetMaxDevices 2

#define HalCallNetGuestTx     1
#define HalCallNetGuestRegDev 2

// 1500 bytes data, 2x 6-byte addresses, 1x 2-byte type field.
#define MaxEthernetFrameSize 1514

typedef struct {
  unsigned  rxBufferFull; // Flag, 1 = guest has not retrieved packet yet.
  unsigned  rxBufferLen;  // Packet size.
  Bit8u     rxBuffer[MaxEthernetFrameSize];
  } halNetGuestRxArea_t;

#endif  /* __HAL_H__ */
