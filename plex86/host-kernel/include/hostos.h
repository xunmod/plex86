/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2004 Kevin P. Lawton
 *
 *  hostos.h: host OS specific callouts that the host OS component must define.
 *
 */

#ifndef __HOSTOS_H__
#define __HOSTOS_H__

#if defined(__NetBSD__) || defined(__FreeBSD__)
#  include <machine/stdarg.h>
#else
#  include <stdarg.h>
#endif


#ifndef UNUSED
#  define UNUSED(x) ((void)(x))
#endif

#ifndef BytesToPages
#define BytesToPages(b) ( ((b)+4095) >> 12 )
#endif

/*
 * This structure describes the pages containing the code/data
 * of the monitor itself (inside the kernel module)
 */

#define Plex86MaxKernelModulePages 128

typedef struct {
  /* Virtual address space occupied by the kernel module. */
  Bit32u startOffset;
  Bit32u startOffsetPageAligned;
  unsigned nPages; /* Number of pages. */
    
  /* A list of the Physical Page Indeces of the pages comprising the
   * kernel module.  A PPI is just the physical page address >> 12.
   */
  Bit32u ppi[Plex86MaxKernelModulePages];
  } kernelModulePages_t;

extern kernelModulePages_t kernelModulePages;


#if defined(IN_HOST_SPACE) || defined(IN_MONITOR_SPACE)
/*
 * These functions are safe to call from either space.
 */

void  nexusMemZero(void *ptr, int size);
void  nexusMemCpy(void *dst, void *src, int size);
void *nexusMemSet(void *s, unsigned c, unsigned n);
int   nexusVsnprintf(char *str, unsigned size, const char *fmt,
                    va_list args);
#endif


#ifdef IN_HOST_SPACE
/*
 * These features are only safe to be used from host space.
 */

#define Plex86ErrnoEBUSY      1
#define Plex86ErrnoENOMEM     2
#define Plex86ErrnoEFAULT     3
#define Plex86ErrnoEINVAL     4
#define Plex86ErrnoEACCES     5
#define Plex86ErrnoEAGAIN     6


unsigned hostOSIdle(void);
void    *hostOSAllocZeroedMem(unsigned long size);
void     hostOSFreeMem(void *ptr);
void    *hostOSAllocZeroedPage(void);
void     hostOSFreePage(void *ptr);
unsigned hostOSGetAllocedMemPhyPages(Bit32u *page, int max_pages, void *ptr,
                                   unsigned size);
Bit32u   hostOSGetAndPinUserPage(void *vm, Bit32u userAddr, void **osSpecificPtr,
             Bit32u *ppi, Bit32u *kernelAddr);
void     hostOSUnpinUserPage(void *vm, Bit32u userAddr, void *osSpecificPtr,
             Bit32u ppi, Bit32u *kernelAddr, unsigned dirty);
Bit32u   hostOSGetAllocedPagePhyPage(void *ptr);
void     hostOSKernelPrint(char *fmt, ...);
void     hostOSUserPrint(void *vm, char *fmt, ...);
Bit32u   hostOSKernelOffset(void);
int      hostOSConvertPlex86Errno(unsigned ret);
void     hostOSModuleCountReset(void *vm, void *inode, void *filp);
void     hostOSInstrumentIntRedirCount(unsigned interruptVector);
unsigned long hostOSCopyFromUser(void *to, void *from, unsigned long len);
unsigned long hostOSCopyToUser(void *to, void *from, unsigned long len);
unsigned long hostOSCopyFromUserIoctl(void *to, void *from, unsigned long len);
unsigned long hostOSCopyToUserIoctl(void *to, void *from, unsigned long len);

/* These functions are NOT defined by the host OS specific piece, but may
 * be called from it, so the prototypes need to be here.
 */
void     hostUnallocVmPages(void *);
void     hostDeviceOpen(void *);
int      hostIoctlGeneric(void *vm, void *inode, void *filp,
                          unsigned int cmd, unsigned long arg);
unsigned hostModuleInit(void);
unsigned hostGetvm_tSize(void);

#endif  /* IN_HOST_SPACE */

#endif  /* __HOSTOS_H__ */
