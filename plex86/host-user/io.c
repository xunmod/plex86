/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#include "plex86.h"
#include "linuxvm.h"
#include "linux-setup.h"
#include "eflags.h"
#include "io.h"


pic_t    picMaster;
pic_t    picSlave;
unsigned port0x80 = 0;
pit_t    pit;
cmos_t   cmos;
vga_t    vga;

unsigned  cpuToPitRatio = 100;
//unsigned  cpuToPitRatio = CPU_CLOCK_FREQ_HZ / PIT_CLOCK_FREQ_HZ;



  unsigned
pitInp(unsigned iolen, unsigned port)
{
  unsigned pitID;

static unsigned counter = 0; // Fixme.
  switch ( port ) {
    case 0x40:
    // case 0x41:
    // case 0x42:
      pitID = port - 0x40;
      if ( pit.timer[pitID].output_latch_full ) {
        if ( pit.timer[pitID].output_latch_toggle==0 ) {
//fprintf(stderr, "pit: read LSB.\n");
          // LSB 1st.
          pit.timer[pitID].output_latch_toggle = 1;
          return( pit.timer[pitID].output_latch_value & 0xff );
          }
        else {
//fprintf(stderr, "pit: read MSB.\n");
//fprintf(stderr, "pic.isr=0x%x/imr=0x%x, BUS.INTR=%u, EFLAGS.IF=%u.\n",
//        picMaster.isr,
//        picMaster.imr,
//        plex86GuestCPU->INTR, 
//        (plex86GuestCPU->eflags>>9)&1);

          // MSB 2nd.
          pit.timer[pitID].output_latch_full = 0;
          pit.timer[pitID].output_latch_toggle = 0;
          return( (pit.timer[pitID].output_latch_value>>8) & 0xff );
          }
        goto error;
        }
      else {
        fprintf(stderr, "pit[%u] OL empty.\n", pitID);
        //pit.timer[pitID].output_latch_value
        //pit.timer[pitID].counter;
        goto error;
        }
      goto error;

    case 0x61:
      fprintf(stderr, "pit: read of port 0x61 unsupported.\n");
      pit.refresh_clock_div2 = !pit.refresh_clock_div2;
{
counter++;
if (counter > 100) {
  pit.timer[2].OUT = !pit.timer[2].OUT;
  counter = 0;
  }
}
      return( (pit.timer[2].OUT<<5) |
              (pit.refresh_clock_div2<<4) |
              (pit.speaker_data_on<<1) |
              (pit.timer[2].GATE?1:0) );
      goto error;
    default:
      fprintf(stderr, "pit: read of port 0x%x unsupported.\n", port);
      goto error;
    }

error:
  plex86TearDown(); exit(1);
}

  void
pitOutp(unsigned iolen, unsigned port, unsigned val)
{
  unsigned newGATE;

//fprintf(stderr, "pit: outp(0x%x)=0x%x\n", port, val);

  switch (port) {
    case 0x40: // Timer0: write count reg.
    case 0x41: // Timer1: write count reg.
    case 0x42: // Timer2: write count reg.
      {
      unsigned pitID; // {0,1,2}
      unsigned xfer_complete;
      unsigned long periodHz;

      pitID = port - 0x40;
      if ( pit.timer[pitID].latch_mode != PIT_LATCH_MODE_16BIT ) {
        // We only need to support 16-bit latch mode for Linux.
        fprintf(stderr, "pit: latch_mode != 16bit mode.\n");
        goto error;
        }
      if ( pit.timer[pitID].input_latch_toggle==0 ) {
        pit.timer[pitID].input_latch_value = val;
        pit.timer[pitID].input_latch_toggle = 1;
        xfer_complete = 0;
        fprintf(stderr, "pit: pit[%u] write L = %02x.\n", pitID, (unsigned) val);
        }
      else {
        pit.timer[pitID].input_latch_value |= (val << 8);
        pit.timer[pitID].input_latch_toggle = 0;
        xfer_complete = 1;
        fprintf(stderr, "pit: pit[%u] write H = %02x.\n", pitID, (unsigned) val);
        }
      if (xfer_complete) {
        fprintf(stderr, "pit: xfer_complete.\n");
        pit.timer[pitID].counter_max = pit.timer[pitID].input_latch_value;

        // Once data is tranferred from the CPU, the value from the
        // count latch is moved to the counting element and the latch
        // is cleared for further loading.
        pit.timer[pitID].input_latch_value = 0;

        // reprogramming counter clears latch
        pit.timer[pitID].output_latch_full = 0;
        pit.timer[pitID].output_latch_toggle = 0;

        switch ( pit.timer[pitID].mode ) {
          case 0: // Interrupt on Terminal Count (single timeout).
            pit.timer[pitID].counter = pit.timer[pitID].counter_max;
            pit.timer[pitID].active = 1;
            if (pit.timer[pitID].GATE) {
              pit.timer[pitID].OUT = 0; // OUT pin starts low
              if ( pit.timer[pitID].counter_max == 0 )
                periodHz = 1193182 / 65536;
              else
                periodHz = 1193182 / pit.timer[pitID].counter_max;
              fprintf(stderr, "pit[%u] period = %lu Hz.\n", pitID, periodHz);
fprintf(stderr, "pit[%u] period_max = %u.\n", pitID,pit.timer[pitID].counter_max);
              }
            return;

          case 2: // Rate generator.
            if ( pit.timer[pitID].counter_max == 1 ) {
              fprintf(stderr, "pit[%u], mode2, write count of 1 illegal.\n",
                      pitID);
              goto error;
              }
            // Writing a new count while counting does not effect the
            // current counting sequence.  It only updates the counting
            // period for the next cycle.
            if ( pit.timer[pitID].GATE && !pit.timer[pitID].active ) {
              // software triggered
              pit.timer[pitID].counter = pit.timer[pitID].counter_max;
              pit.timer[pitID].active  = 1;
              pit.timer[pitID].OUT     = 1; // initially set high
              if ( pit.timer[pitID].counter_max == 0 )
                periodHz = 1193182 / 65536;
              else
                periodHz = 1193182 / pit.timer[pitID].counter_max;
              fprintf(stderr, "pit[%u] period = %lu Hz.\n", pitID, periodHz);
fprintf(stderr, "pit[%u] period_max = %u.\n", pitID,pit.timer[pitID].counter_max);
              }
            return;

          default:
            fprintf(stderr, "pit: mode=%u unsupported.\n", pit.timer[pitID].mode);
            goto error;
          }
        goto error;
        }
      return;
      }

    case 0x43:
      {
      unsigned pitID, rw, mode, bcd;

      /* | 7 | 6 | 5 | 4 |3 |2 |1 |0  |
       * |SC1|SC0|RW1|RW0|M2|M1|M0|BCD|
       * SC: select counter
       *   00: Counter 0
       *   01: Counter 1
       *   10: Counter 2
       *   11: Read-Back Command
       * RW: Read/Write:
       *   00: Counter Latch Command
       *   01: Read/Write least significant byte only.
       *   10: Read/Write most significant byte only.
       *   11: Read/Write least significant byte 1st, then most significant.
       * M: Mode:
       *   000: Mode 0
       *   001: Mode 1
       *   x10: Mode 2 (x = don't care bits - should be 0 for forward compat.)
       *   x11: Mode 3
       *   100: Mode 4
       *   101: Mode 5
       * BCD:
       *   0: Binary Counter 16-bits
       *   1: Binary Coded Decimal Counter (4 decades)
       */
      pitID = (val >> 6) & 3;
      rw    = (val >> 4) & 3;
      mode  = (val >> 1) & 7;
      bcd   = (val >> 0) & 1;

      if ( pitID == 3 ) {
        // Read Back Command not supported.
        fprintf(stderr, "pit: outp(0x43)=0x%02x: Read Back Command.\n", val);
        goto error;
        }
      if ( mode>5 ) {
        // mode==6 is mode=2 because of don't care bit.
        // mode==7 is mode=3 because of don't care bit.
        fprintf(stderr, "pit: outp(0x43)=0x%02x, mode>5.\n", val);
        goto error;
        }
      if ( bcd ) {
        // Only deal with binary counting.
        fprintf(stderr, "pit: outp(0x43)=0x%02x: bcd mode.\n", val);
        goto error;
        }
      if ( rw == 0 ) {
        // Counter Latch Command.
        // fprintf(stderr, "pit[%u]: counter latch command.\n", pitID);
        if ( pit.timer[pitID].output_latch_full ) {
          fprintf(stderr, "pit[%u]: counter latch command, latch full.\n", pitID);
          goto error;
          }
        pit.timer[pitID].output_latch_full = 1;
        pit.timer[pitID].output_latch_value = pit.timer[pitID].counter;
        pit.timer[pitID].output_latch_toggle = 0;
        return;
        }
      else if ( rw != 3 ) {
        // Only support 16-bit counters.
        fprintf(stderr, "pit: outp(0x43)=0x%02x: rw=%u.\n", val, rw);
        goto error;
        }
      if ( (mode!=0) && (mode!=2) ) {
        // Only support modes {0,2}.
        fprintf(stderr, "pit: outp(0x43)=0x%02x: mode=%u.\n", val, mode);
        goto error;
        }

      pit.timer[pitID].mode               = mode;
      pit.timer[pitID].latch_mode         = PIT_LATCH_MODE_16BIT;
      pit.timer[pitID].input_latch_value  = 0;
      pit.timer[pitID].input_latch_toggle = 0;
      pit.timer[pitID].bcd                = bcd;
      // Note: when the Control Word is written to a counter, all control
      // logic is immediately reset and OUT goes to a known initial state.
      if ( mode==0 ) { // Interrupt on terminal count.
        // Mode0 starts with OUT=0.  It goes high after the initial count
        // is written and expires.
        pit.timer[pitID].OUT  = 0;
        }
      else if ( mode==2 ) { // Rate generator.
        pit.timer[pitID].OUT  = 1;
        }
      fprintf(stderr, "pit: CW written, mode=%u.\n", mode);
      return;
      }

    case 0x61:
      {
      pit.speaker_data_on = (val >> 1) & 0x01;
      newGATE = val & 1;
      if ( newGATE == pit.timer[2].GATE ) {
        // No change in GATE, done.
        return;
        }
      pit.timer[2].GATE = newGATE;
      if ( newGATE ) {
        // PIT2: transition of GATE from 0 to 1.
        switch ( pit.timer[2].mode ) {
          default:
            fprintf(stderr, "pit: port 0x61 W, timer2 GATE0->1 mode=%u.\n",
                    pit.timer[2].mode);
            goto error;
          }
        goto error;
        }
      else {
        // PIT2: transition of GATE from 1 to 0, deactivate.
        switch ( pit.timer[2].mode ) {
          default:
            fprintf(stderr, "pit: port 0x61 W, timer2 GATE1->0 mode=%u.\n",
                    pit.timer[2].mode);
            goto error;
          }
        goto error;
        }
      return;
      }

    default:
      goto error;
    }

error:
  plex86TearDown(); exit(1);
}

  void
pitExpireClocks( unsigned pitClocks )
{
  unsigned pitID;

  for (pitID=0; pitID<=2; pitID++) {
    if ( pit.timer[pitID].active ) {
      if ( pit.timer[pitID].counter > pitClocks ) {
        // Expired ticks are still less than number of counts left.
        pit.timer[pitID].counter -= pitClocks;
        }
      else {
        if ( pit.timer[pitID].mode==0 ) {
          // Interrupt on Terminal Count (single timeout).
          pit.timer[pitID].counter = 0;
          pit.timer[pitID].active = 0;
          if ( pitID==0 )
            picIrq(/*irq0*/ 0,  /*value*/ 1);
          }
        else if ( pit.timer[pitID].mode==2 ) {
          // Rate generator (periodic).
          // Reload counter.
          pit.timer[pitID].counter = pit.timer[pitID].counter_max;
          if ( pitID==0 )
            picIrq(/*irq0*/ 0,  /*value*/ 1);
          }
        else {
          fprintf(stderr, "pitExpireClocks: mode=%u.\n", pit.timer[pitID].mode);
          plex86TearDown(); exit(1);
          }
        }
      }
    }
}


  unsigned
cmosInp(unsigned iolen, unsigned port)
{
  unsigned val;

  switch (port) {
    case 0x71:
      fprintf(stderr, "cmosInp(0x%x), reg=%u.\n", port, cmos.mem_address);
      if (cmos.mem_address >= NUM_CMOS_REGS) {
        fprintf(stderr, "cmos: mem_address(%u) OOB.\n", cmos.mem_address);
        goto error;
        }
if (cmos.mem_address == REG_STAT_A) {
  // Fixme: hack to toggle the update-in-progress bit.  Linux will read
  // this until it changes, to know when it's safe to read.
  cmos.reg[REG_STAT_A] ^= 0x80;
  fprintf(stderr, "cmos[REG_STAT_A] now 0x%02x\n", cmos.reg[REG_STAT_A]);
  }
      val = cmos.reg[cmos.mem_address];
      if ( cmos.mem_address == REG_STAT_C ) {
        cmos.reg[REG_STAT_C] = 0;
        fprintf(stderr, "cmos: read of REG_STAT_C.\n");
        // Fixme: pic_lower_irq(8);
        }
      return( val );

    default:
      fprintf(stderr, "cmosInp(0x%x).\n", port);
      goto error;
    }

error:
  plex86TearDown(); exit(1);
}

  void
cmosOutp(unsigned iolen, unsigned port, unsigned val)
{
  switch (port) {
    case 0x70:
#if (NUM_CMOS_REGS == 64)
      cmos.mem_address = val & 0x3f;
#else // 128 registers
      cmos.mem_address = val & 0x7f;
#endif
      return;
    default:
      goto error;
    }

error:
  plex86TearDown(); exit(1);
}

  unsigned
vgaInp(unsigned iolen, unsigned port)
{
  //fprintf(stderr, "vgaInp(0x%x).\n", port);
  switch ( port ) {
    case 0x3d5:
      if ( vga.CRTC.address > 0x18 ) {
        return(0);
        }
      fprintf(stderr, "vga: reading CRTC[%u].\n", vga.CRTC.address);
      return( vga.CRTC.reg[vga.CRTC.address] );
    case 0x3da:
      return(0); // Fixme:
    }
  goto error;
error:
  plex86TearDown(); exit(1);
}

  void
vgaOutp(unsigned iolen, unsigned port, unsigned val)
{
  if ( iolen == 2 ) {
    vgaOutp(1, port, val&0xff);
    vgaOutp(1, port+1, (val>>8) & 0xff);
    return;
    }

  switch ( port ) {
    case 0x03d4: /* CRTC Index Register (color emulation modes) */
      vga.CRTC.address = val & 0x7f;
      if (vga.CRTC.address > 0x18) {
        fprintf(stderr, "vga W: invalid CRTC register 0x%02x selected.\n",
                vga.CRTC.address);
        goto error;
        }
      return;

    case 0x03d5: /* CRTC Index Register (color emulation modes) */
      if (vga.CRTC.address > 0x18) {
        fprintf(stderr, "vga W: invalid CRTC register 0x%02x selected.\n",
                vga.CRTC.address);
        goto error;
        }
      vga.CRTC.reg[vga.CRTC.address] = val & 0xff;
      return;

    default:
      fprintf(stderr, "vgaOutp(0x%x, 0x%x).\n", port, val);
      return; // Fixme:
    }

error:
  plex86TearDown(); exit(1);
}

  unsigned
picInp(unsigned iolen, unsigned port)
{
  switch (port) {
    case 0x21:
      // In polled mode, this is an int acknowledge.
      return( picMaster.imr );


    default:
      fprintf(stderr, "picInp: port=0x%x\n", port);
      goto error;
    }

error:
  plex86TearDown(); exit(1);
}

  void
picOutp(unsigned iolen, unsigned port, unsigned val)
{
  unsigned bitmask;

  switch ( port ) {
    case 0x20:
      if ( val & 0x10 ) { // Init command 1.
        picMaster.init.inInit = 1;
        picMaster.init.requires4 = (val & 1);
        picMaster.init.byteExpected = 2; // Next is command 2.
        picMaster.imr = 0;
        picMaster.isr = 0;
        picMaster.irr = 0;
        plex86GuestCPU->INTR = 0;
        if ( val & 0x0a ) {
          fprintf(stderr, "PIC(m): unsupported init command 0x%x\n", val);
          goto error;
          }
        return;
        }
      if ( (val & 0x18) == 0x08 ) { /* OCW3 */
        fprintf(stderr, "PIC(m): OCW3 unfinished.\n");
        goto error;
        }
      switch ( val ) {
        case 0x60: // Specific EOI 0
        case 0x63: // Specific EOI 3
          bitmask = 1 << (val-0x60);
          if ( !(picMaster.isr & bitmask) ) {
            fprintf(stderr, "PIC(m): EOI %u, isr=0x%x.\n",
                    val-0x60, picMaster.isr);
            goto error;
            }
          // Remove interrupt from ISR.
          picMaster.isr &= ~bitmask;
          plex86GuestCPU->INTR = 0;
          // Give PIC a chance to propogate another interrupt.
          picServiceMaster();
          return;

        default:
          fprintf(stderr, "PIC(m): OCW2 0x%x.\n", val);
        }
      goto error;

    case 0x21:
      picMaster.imr = val;
      picServiceMaster();
      return;

    case 0xa0:
      if ( val & 0x10 ) { // Init command 1.
        picSlave.init.inInit = 1;
        picSlave.init.requires4 = (val & 1);
        picSlave.init.byteExpected = 2; // Next is command 2.
        picSlave.imr = 0;
        picSlave.isr = 0;
        picSlave.irr = 0;
        if ( val & 0x0a ) {
          fprintf(stderr, "PIC(s): unsupported init command 0x%x\n", val);
          goto error;
          }
        return;
        }
      if ( (val & 0x18) == 0x08 ) { /* OCW3 */
        fprintf(stderr, "PIC(s): OCW3 unfinished.\n");
        goto error;
        }
      fprintf(stderr, "PIC(s): OCW2 unfinished.\n");
      goto error;

    case 0xa1:
      picSlave.imr = val;
      fprintf(stderr, "PIC(s) imr=0x%x\n", val);
      return;
    }

error:
  plex86TearDown(); exit(1);
}

  void
picIrq(unsigned irqNum,  unsigned val)
{
  unsigned bitMask;
  pic_t   *picPtr;

  // Assuming val==1

  if ( irqNum <= 7 ) {
    picPtr = &picMaster;
    bitMask = 1<<irqNum;
    }
  else {
    picPtr = &picSlave;
    bitMask = 1<<(irqNum-8);
    }

if ( picPtr->irr & bitMask )
  return; // Int already requested, nothing useful to do.

  picPtr->irr |= bitMask;
  if ( picPtr->imr & bitMask )
    return; // Int masked out currently.  Nothing to do.
  if ( picPtr->isr )
    return; // An int is in-service.  Wait until it is EOI'd.
  // No interrupts in-service, irqNum is not masked out.  We can
  // signal the interrupt to the CPU.
  if ( irqNum <= 7 ) {
    plex86GuestCPU->INTR = 1;
    }
  else { /* 8..15 */
    picIrq(2, 1); // Slave output is irq#2 input on Master.
    }
}

  void
picServiceMaster(void)
{
  unsigned irr;

  if ( picMaster.isr ) {
    return; // An interrupt is already in-service.
    }

  // Find the highest priority requested interrupt which is not masked.
  irr = (picMaster.irr & ~picMaster.imr);
  if ( !irr )
    return; // No unmasked pending interrupts.

  // There are pending unmaked irqs; signal INTR to CPU.
  plex86GuestCPU->INTR = 1;
  return;
}


  unsigned  
picIAC(void)
{
// Note: ignores slave PIC for now.
  unsigned vector, bit, irr;

  //fprintf(stderr, "picIAC:\n");
  if ( !plex86GuestCPU->INTR || picMaster.isr ) {
    fprintf(stderr, "picIAC: INT=%u, isr=0x%x.\n",
            plex86GuestCPU->INTR, picMaster.isr);
    goto error;
    }

  irr = (picMaster.irr & ~picMaster.imr);
  if ( !irr ) {
    fprintf(stderr, "picIAC: no unmasked irqs.\n");
    goto error;
    }
  // Find the highest priority unmasked irq.
  for (bit=0; bit<=7; bit++) {
    if ( irr & (1<<bit) )
      break;
    }
  // Promote irq from the IRR to ISR register.
  picMaster.irr &= ~(1<<bit);
  picMaster.isr |=  (1<<bit);

  plex86GuestCPU->INTR = 0;

  // Return a vector, adjusted by the offset that was configured.
  vector = picMaster.vectorOffset + bit;

  return( vector );

error:
  plex86TearDown(); exit(1);
}

  void
doVGADump(void)
{
  // VGA text framebuffer dump.
  unsigned col, c, i, lines;
  unsigned char lineBuffer[81];

  unsigned framebuffer_start;

  framebuffer_start = 0xb8000 + 2*((vga.CRTC.reg[12] << 8) + vga.CRTC.reg[13]);
  lines = 204; // 204 Max
  //lines = 160; // 204 Max

  for (i=0; i<2*80*lines; i+=2*80) {
    for (col=0; col<80; col++) {
      c = plex86MemPtr[framebuffer_start + i + col*2];
      if ( isgraph(c) )
        lineBuffer[col] = c;
      else
        lineBuffer[col] = ' ';
      }
    lineBuffer[sizeof(lineBuffer)-1] = 0; // Null terminate string.
    fprintf(stderr, "%s\n", lineBuffer);
    }
}
