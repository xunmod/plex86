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


// NOTES:
//   Updating segment descriptor A bits upon descriptor load.
//   Clean assertRF passed to doInterrupt()
//   Check that all uses of translateLinToPhy() check for LinAddrNotAvailable


// ===================
// Defines / typesdefs
// ===================

#define PageSize 4096

typedef struct {
  Bit32u low;
  Bit32u high;
  } __attribute__ ((packed)) gdt_entry_t ;


// ===================
// Function prototypes
// ===================

extern unsigned  doGuestFault(unsigned fault, unsigned errorCode);
extern void      doInterrupt(unsigned vector, unsigned intFlags,
                             Bit32u errorCode);


// =========
// Variables
// =========
