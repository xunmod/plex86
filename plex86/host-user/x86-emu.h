/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton


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
//extern void      doInterrupt(unsigned vector, unsigned intFlags,
//                             Bit32u errorCode);


// =========
// Variables
// =========
