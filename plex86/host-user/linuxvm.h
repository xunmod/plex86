/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton

#ifndef __LINUXVM_H__
#define __LINUXVM_H__


// ========
// Typedefs
// ========

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
extern cpuid_info_t guestCPUID;


#endif  // __LINUXVM_H__
