/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2003 Kevin P. Lawton
 *
 *  paging.h: defines for x86 paging structures
 *
 */

#ifndef __PAGING_H__
#define __PAGING_H__

#define PgMskD 0x00000040
#define PgMskA 0x00000020

/* Page Directory/Table format */
typedef union {
  Bit32u raw;
  struct {
    Bit32u  P:1;
    Bit32u  RW:1;
    Bit32u  US:1;
    Bit32u  PWT:1;
    Bit32u  PCD:1;
    Bit32u  A:1;
    Bit32u  D:1;
    Bit32u  PS:1;
    Bit32u  G:1;
    Bit32u  avail:3;
    Bit32u  base:20;
    } __attribute__ ((packed)) fields;
  } __attribute__ ((packed)) pageEntry_t;

typedef union {
  Bit8u       bytes[4096];
  pageEntry_t pte[1024];
  } page_t;

#endif  /* __PAGING_H__ */
