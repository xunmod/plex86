/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton


#define incrementAtomic(v) ({ \
  __asm__ volatile ( \
    "lock incl %0" \
    : "=m" (v) \
    : "0" (v) \
    ); \
  v; \
  })

extern unsigned initHal(char *tunScript);
extern unsigned halCall(void);
extern unsigned tuntapReadPacketToGuest(unsigned deviceNo);

extern volatile unsigned tunTapInService;
extern volatile unsigned tunTapEvent;
extern int fdTunTap;

extern void halDiskCall(unsigned callNo);
extern unsigned halDiskInit(void);

extern void halNetCall(unsigned callNo);
extern unsigned halNetInit(char *tunScript);

extern void halConCall(unsigned callNo);
extern unsigned halConInit(void);
