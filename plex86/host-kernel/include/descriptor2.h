/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2003 Kevin P. Lawton
 *
 *  descriptor2.h: defines for descriptors and selectors
 *
 */


#ifndef __DESCRIPTOR2_H__
#define __DESCRIPTOR2_H__



#define SET_INT_GATE(d, S,O,P,DPL, D) {\
    d.selector = (S);\
    d.offset_high = (O) >> 16;\
    d.offset_low = (O) & 0xffff;\
    d.RESERVED = 0;\
    d.type = ((D)<<3) | 0x6;\
    d.dpl = (DPL);\
    d.p = (P);\
}

#define SET_TRAP_GATE(d, S,O,P,DPL, D) {\
    d.selector = (S);\
    d.offset_high = (O) >> 16;\
    d.offset_low = (O) & 0xffff;\
    d.RESERVED = 0;\
    d.type = ((D)<<3) | 0x7;\
    d.dpl = (DPL);\
    d.p = (P);\
}

typedef struct 
{
    Bit32u offset;
    Bit16u selector;
} __attribute ((packed)) far_jmp_info_t;

#endif  /* __DESCRIPTOR2_H__ */
