/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton


#define incrementAtomic(v) ({ \
  asm volatile ( \
    "lock incl %0" \
    : "=m" (v) \
    : "0" (v) \
    ); \
  v; \
  })

extern unsigned initHal(void);
extern void halCall(void);
extern unsigned tuntapReadPacketToGuest(unsigned deviceNo);

extern volatile unsigned tunTapInService;
extern volatile unsigned tunTapEvent;
extern int fdTunTap;
