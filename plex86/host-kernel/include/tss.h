/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2004 Kevin P. Lawton
 *
 *  tss.h: defines for x86 hardware tasking structures
 *
 */

#ifndef __TSS_H__
#define __TSS_H__

typedef struct 
{
    Bit16u back, RESERVED0;      /* Backlink */
    Bit32u esp0;                 /* The CK stack pointer */
    Bit16u ss0,  RESERVED1;      /* The CK stack selector */
    Bit32u esp1;                 /* The parent KL stack pointer */
    Bit16u ss1,  RESERVED2;      /* The parent KL stack selector */
    Bit32u esp2;                 /* Unused */
    Bit16u ss2,  RESERVED3;      /* Unused */
    Bit32u cr3;                  /* The page directory pointer */
    Bit32u eip;                  /* The instruction pointer */
    Bit32u eflags;               /* The flags */
    Bit32u eax, ecx, edx, ebx;   /* The general purpose registers */
    Bit32u esp, ebp, esi, edi;   /* The special purpose registers */
    Bit16u es,   RESERVED4;      /* The extra selector */
    Bit16u cs,   RESERVED5;      /* The code selector */
    Bit16u ss,   RESERVED6;      /* The application stack selector */
    Bit16u ds,   RESERVED7;      /* The data selector */
    Bit16u fs,   RESERVED8;      /* And another extra selector */
    Bit16u gs,   RESERVED9;      /* ... and another one */
    Bit16u ldt,  RESERVED10;     /* The local descriptor table */
    Bit16u trap;                 /* The trap flag (for debugging) */
    Bit16u io;                   /* The I/O Map base address */
} __attribute__ ((packed)) tss_t;

#endif  /* __TSS_H__ */
