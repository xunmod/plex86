/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton
////
////  This library is free software; you can redistribute it and/or
////  modify it under the terms of the GNU Lesser General Public
////  License as published by the Free Software Foundation; either
////  version 2 of the License, or (at your option) any later version.
////
////  This library is distributed in the hope that it will be useful,
////  but WITHOUT ANY WARRANTY; without even the implied warranty of
////  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
////  Lesser General Public License for more details.
////
////  You should have received a copy of the GNU Lesser General Public
////  License along with this library; if not, write to the Free Software
////  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

#ifndef __LINUXVM_H__
#define __LINUXVM_H__


// ========
// Typedefs
// ========
typedef Bit32u phyAddr_t;

// =========
// Functions
// =========

extern unsigned plex86TearDown(void);


// =========
// Variables
// =========

extern Bit8u       *plex86MemPtr;
extern size_t       plex86MemSize;
extern guest_cpu_t *plex86GuestCPU;


#endif  // __LINUXVM_H__
