/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2003 Kevin P. Lawton
 *
 *  eflags.h: Bitfields of EFLAGS registers
 */

#ifndef __EFLAGS_H__
#define __EFLAGS_H__


#define FlgMaskID   (1<<21)
#define FlgMaskVIP  (1<<20)
#define FlgMaskVIF  (1<<19)
#define FlgMaskAC   (1<<18)
#define FlgMaskVM   (1<<17)
#define FlgMaskRF   (1<<16)
#define FlgMaskNT   (1<<14)
#define FlgMaskIOPL (3<<12)
#define FlgMaskOF   (1<<11)
#define FlgMaskDF   (1<<10)
#define FlgMaskIF   (1<<9)
#define FlgMaskTF   (1<<8)
#define FlgMaskSF   (1<<7)
#define FlgMaskZF   (1<<6)
#define FlgMaskAF   (1<<4)
#define FlgMaskPF   (1<<2)
#define FlgMaskCF   (1<<0)

typedef union {
  struct {
    Bit8u cf:1;
    Bit8u R1:1;
    Bit8u pf:1;
    Bit8u R3:1;
    Bit8u af:1;
    Bit8u R5:1;
    Bit8u zf:1;
    Bit8u sf:1;
    Bit8u tf:1;
    Bit8u if_:1;
    Bit8u df:1;
    Bit8u of:1;
    Bit8u iopl:2;
    Bit8u nt:1;
    Bit8u R15:1;
    Bit8u rf:1;
    Bit8u vm:1;
    Bit8u ac:1;
    Bit8u vif:1;
    Bit8u vip:1;
    Bit8u id:1;
    Bit16u R31_22:10;
    } __attribute__ ((packed)) fields;
  Bit32u raw;
  } __attribute__ ((packed)) eflags_t;

#endif  /* __EFLAGS_H__ */
