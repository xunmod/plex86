/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton

#include "plex86.h"
#define IN_MONITOR_SPACE
#include "monitor.h"



  unsigned
pitInp(vm_t *vm, unsigned iolen, unsigned port)
{
  unsigned pitID;

  switch ( port ) {
    case 0x40:
    // case 0x41:
    // case 0x42:
      pitID = port - 0x40;
      if ( vm->io.pit.timer[pitID].output_latch_full ) {
        if ( vm->io.pit.timer[pitID].output_latch_toggle==0 ) {
//monprint(vm, "pit: read LSB.\n");
          // LSB 1st.
          vm->io.pit.timer[pitID].output_latch_toggle = 1;
          return( vm->io.pit.timer[pitID].output_latch_value & 0xff );
          }
        else {
//monprint(vm, "pit: read MSB.\n");
//monprint(vm, "pic.isr=0x%x/imr=0x%x, BUS.INTR=%u, EFLAGS.IF=%u.\n",
//        vm->io.picMaster.isr,
//        vm->io.picMaster.imr,
//        vm->system.INTR, 
//        (guestCpu->eflags>>9)&1);

          // MSB 2nd.
          vm->io.pit.timer[pitID].output_latch_full = 0;
          vm->io.pit.timer[pitID].output_latch_toggle = 0;
          return( (vm->io.pit.timer[pitID].output_latch_value>>8) & 0xff );
          }
        goto error;
        }
      else {
        monprint(vm, "pit[%u] OL empty.\n", pitID);
        //vm->io.pit.timer[pitID].output_latch_value
        //vm->io.pit.timer[pitID].counter;
        goto error;
        }
      goto error;

    case 0x61:
      monprint(vm, "pit: read of port 0x61 unsupported.\n");
      vm->io.pit.refresh_clock_div2 = !vm->io.pit.refresh_clock_div2;
{
vm->io.pit_bogus_counter++;
if (vm->io.pit_bogus_counter > 100) {
  vm->io.pit.timer[2].OUT = !vm->io.pit.timer[2].OUT;
  vm->io.pit_bogus_counter = 0;
  }
}
      return( (vm->io.pit.timer[2].OUT<<5) |
              (vm->io.pit.refresh_clock_div2<<4) |
              (vm->io.pit.speaker_data_on<<1) |
              (vm->io.pit.timer[2].GATE?1:0) );
      goto error;
    default:
      monprint(vm, "pit: read of port 0x%x unsupported.\n", port);
      goto error;
    }

error:
  monpanic(vm, "pitInp: bailing.\n");
}

  void
pitOutp(vm_t *vm, unsigned iolen, unsigned port, unsigned val)
{
  unsigned newGATE;

//monprint(vm, "pit: outp(0x%x)=0x%x\n", port, val);

  switch (port) {
    case 0x40: // Timer0: write count reg.
    case 0x41: // Timer1: write count reg.
    case 0x42: // Timer2: write count reg.
      {
      unsigned pitID; // {0,1,2}
      unsigned xfer_complete;
      unsigned long periodHz;

      pitID = port - 0x40;
      if ( vm->io.pit.timer[pitID].latch_mode != PIT_LATCH_MODE_16BIT ) {
        // We only need to support 16-bit latch mode for Linux.
        monprint(vm, "pit: latch_mode != 16bit mode.\n");
        goto error;
        }
      if ( vm->io.pit.timer[pitID].input_latch_toggle==0 ) {
        vm->io.pit.timer[pitID].input_latch_value = val;
        vm->io.pit.timer[pitID].input_latch_toggle = 1;
        xfer_complete = 0;
        monprint(vm, "pit: pit[%u] write L = %02x.\n", pitID, (unsigned) val);
        }
      else {
        vm->io.pit.timer[pitID].input_latch_value |= (val << 8);
        vm->io.pit.timer[pitID].input_latch_toggle = 0;
        xfer_complete = 1;
        monprint(vm, "pit: pit[%u] write H = %02x.\n", pitID, (unsigned) val);
        }
      if (xfer_complete) {
        monprint(vm, "pit: xfer_complete.\n");
        vm->io.pit.timer[pitID].counter_max = vm->io.pit.timer[pitID].input_latch_value;

        // Once data is tranferred from the CPU, the value from the
        // count latch is moved to the counting element and the latch
        // is cleared for further loading.
        vm->io.pit.timer[pitID].input_latch_value = 0;

        // reprogramming counter clears latch
        vm->io.pit.timer[pitID].output_latch_full = 0;
        vm->io.pit.timer[pitID].output_latch_toggle = 0;

        switch ( vm->io.pit.timer[pitID].mode ) {
          case 0: // Interrupt on Terminal Count (single timeout).
            vm->io.pit.timer[pitID].counter = vm->io.pit.timer[pitID].counter_max;
            vm->io.pit.timer[pitID].active = 1;
            if (vm->io.pit.timer[pitID].GATE) {
              vm->io.pit.timer[pitID].OUT = 0; // OUT pin starts low
              if ( vm->io.pit.timer[pitID].counter_max == 0 )
                periodHz = 1193182 / 65536;
              else
                periodHz = 1193182 / vm->io.pit.timer[pitID].counter_max;
              monprint(vm, "pit[%u] period = %u Hz.\n", pitID, (unsigned)periodHz);
              monprint(vm, "pit[%u] period_max = %u.\n", pitID,
                       vm->io.pit.timer[pitID].counter_max);
              }
            return;

          case 2: // Rate generator.
            if ( vm->io.pit.timer[pitID].counter_max == 1 ) {
              monprint(vm, "pit[%u], mode2, write count of 1 illegal.\n",
                      pitID);
              goto error;
              }
            // Writing a new count while counting does not effect the
            // current counting sequence.  It only updates the counting
            // period for the next cycle.
            if ( vm->io.pit.timer[pitID].GATE && !vm->io.pit.timer[pitID].active ) {
              // software triggered
              vm->io.pit.timer[pitID].counter = vm->io.pit.timer[pitID].counter_max;
              vm->io.pit.timer[pitID].active  = 1;
              vm->io.pit.timer[pitID].OUT     = 1; // initially set high
              if ( vm->io.pit.timer[pitID].counter_max == 0 )
                periodHz = 1193182 / 65536;
              else
                periodHz = 1193182 / vm->io.pit.timer[pitID].counter_max;
              monprint(vm, "pit[%u] period = %u Hz.\n", pitID, (unsigned)periodHz);
              monprint(vm, "pit[%u] period_max = %u.\n", pitID,
                       vm->io.pit.timer[pitID].counter_max);
              }
            return;

          default:
            monprint(vm, "pit: mode=%u unsupported.\n",
                     vm->io.pit.timer[pitID].mode);
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
        monprint(vm, "pit: outp(0x43)=0x%02x: Read Back Command.\n", val);
        goto error;
        }
      if ( mode>5 ) {
        // mode==6 is mode=2 because of don't care bit.
        // mode==7 is mode=3 because of don't care bit.
        monprint(vm, "pit: outp(0x43)=0x%02x, mode>5.\n", val);
        goto error;
        }
      if ( bcd ) {
        // Only deal with binary counting.
        monprint(vm, "pit: outp(0x43)=0x%02x: bcd mode.\n", val);
        goto error;
        }
      if ( rw == 0 ) {
        // Counter Latch Command.
        // monprint(vm, "pit[%u]: counter latch command.\n", pitID);
        if ( vm->io.pit.timer[pitID].output_latch_full ) {
          monprint(vm, "pit[%u]: counter latch command, latch full.\n", pitID);
          goto error;
          }
        vm->io.pit.timer[pitID].output_latch_full = 1;
        vm->io.pit.timer[pitID].output_latch_value = vm->io.pit.timer[pitID].counter;
        vm->io.pit.timer[pitID].output_latch_toggle = 0;
        return;
        }
      else if ( rw != 3 ) {
        // Only support 16-bit counters.
        monprint(vm, "pit: outp(0x43)=0x%02x: rw=%u.\n", val, rw);
        goto error;
        }
      if ( (mode!=0) && (mode!=2) ) {
        // Only support modes {0,2}.
        monprint(vm, "pit: outp(0x43)=0x%02x: mode=%u.\n", val, mode);
        goto error;
        }

      vm->io.pit.timer[pitID].mode               = mode;
      vm->io.pit.timer[pitID].latch_mode         = PIT_LATCH_MODE_16BIT;
      vm->io.pit.timer[pitID].input_latch_value  = 0;
      vm->io.pit.timer[pitID].input_latch_toggle = 0;
      vm->io.pit.timer[pitID].bcd                = bcd;
      // Note: when the Control Word is written to a counter, all control
      // logic is immediately reset and OUT goes to a known initial state.
      if ( mode==0 ) { // Interrupt on terminal count.
        // Mode0 starts with OUT=0.  It goes high after the initial count
        // is written and expires.
        vm->io.pit.timer[pitID].OUT  = 0;
        }
      else if ( mode==2 ) { // Rate generator.
        vm->io.pit.timer[pitID].OUT  = 1;
        }
      monprint(vm, "pit: CW written, mode=%u.\n", mode);
      return;
      }

    case 0x61:
      {
      vm->io.pit.speaker_data_on = (val >> 1) & 0x01;
      newGATE = val & 1;
      if ( newGATE == vm->io.pit.timer[2].GATE ) {
        // No change in GATE, done.
        return;
        }
      vm->io.pit.timer[2].GATE = newGATE;
      if ( newGATE ) {
        // PIT2: transition of GATE from 0 to 1.
        switch ( vm->io.pit.timer[2].mode ) {
          default:
            monprint(vm, "pit: port 0x61 W, timer2 GATE0->1 mode=%u.\n",
                    vm->io.pit.timer[2].mode);
            goto error;
          }
        goto error;
        }
      else {
        // PIT2: transition of GATE from 1 to 0, deactivate.
        switch ( vm->io.pit.timer[2].mode ) {
          default:
            monprint(vm, "pit: port 0x61 W, timer2 GATE1->0 mode=%u.\n",
                    vm->io.pit.timer[2].mode);
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
  monpanic(vm, "pitOutp: bailing.\n");
}

  void
pitExpireClocks(vm_t *vm,  unsigned pitClocks)
{
  unsigned pitID;

  for (pitID=0; pitID<=2; pitID++) {
    if ( vm->io.pit.timer[pitID].active ) {
      if ( vm->io.pit.timer[pitID].counter > pitClocks ) {
        // Expired ticks are still less than number of counts left.
        vm->io.pit.timer[pitID].counter -= pitClocks;
        }
      else {
        if ( vm->io.pit.timer[pitID].mode==0 ) {
          // Interrupt on Terminal Count (single timeout).
          vm->io.pit.timer[pitID].counter = 0;
          vm->io.pit.timer[pitID].active = 0;
          if ( pitID==0 )
            picIrq(vm, /*irq0*/ 0,  /*value*/ 1);
          }
        else if ( vm->io.pit.timer[pitID].mode==2 ) {
          // Rate generator (periodic).
          // Reload counter.
          vm->io.pit.timer[pitID].counter = vm->io.pit.timer[pitID].counter_max;
          if ( pitID==0 )
            picIrq(vm, /*irq0*/ 0,  /*value*/ 1);
          }
        else {
          monpanic(vm, "pitExpireClocks: mode=%u.\n",
                   vm->io.pit.timer[pitID].mode);
          }
        }
      }
    }
}


  unsigned
cmosInp(vm_t *vm, unsigned iolen, unsigned port)
{
  unsigned val;

  switch (port) {
    case 0x71:
      monprint(vm, "cmosInp(0x%x), reg=%u.\n", port, vm->io.cmos.mem_address);
      if (vm->io.cmos.mem_address >= NUM_CMOS_REGS) {
        monprint(vm, "cmos: mem_address(%u) OOB.\n", vm->io.cmos.mem_address);
        goto error;
        }
if (vm->io.cmos.mem_address == REG_STAT_A) {
  // Fixme: hack to toggle the update-in-progress bit.  Linux will read
  // this until it changes, to know when it's safe to read.
  vm->io.cmos.reg[REG_STAT_A] ^= 0x80;
  monprint(vm, "cmos[REG_STAT_A] now 0x%02x\n", vm->io.cmos.reg[REG_STAT_A]);
  }
      val = vm->io.cmos.reg[vm->io.cmos.mem_address];
      if ( vm->io.cmos.mem_address == REG_STAT_C ) {
        vm->io.cmos.reg[REG_STAT_C] = 0;
        monprint(vm, "cmos: read of REG_STAT_C.\n");
        // Fixme: pic_lower_irq(8);
        }
      return( val );

    default:
      monprint(vm, "cmosInp(0x%x).\n", port);
      goto error;
    }

error:
  monpanic(vm, "cmosInp: bailing.\n");
}

  void
cmosOutp(vm_t *vm, unsigned iolen, unsigned port, unsigned val)
{
  switch (port) {
    case 0x70:
#if (NUM_CMOS_REGS == 64)
      vm->io.cmos.mem_address = val & 0x3f;
#else // 128 registers
      vm->io.cmos.mem_address = val & 0x7f;
#endif
      return;
    default:
      goto error;
    }

error:
  monpanic(vm, "cmosOutp: bailing.\n");
}

  unsigned
vgaInp(vm_t *vm, unsigned iolen, unsigned port)
{
  //monprint(vm, "vgaInp(0x%x).\n", port);
  switch ( port ) {
    case 0x3d5:
      if ( vm->io.vga.CRTC.address > 0x18 ) {
        return(0);
        }
      monprint(vm, "vga: reading CRTC[%u].\n", vm->io.vga.CRTC.address);
      return( vm->io.vga.CRTC.reg[vm->io.vga.CRTC.address] );
    case 0x3da:
      return(0); // Fixme:
    }
  goto error;

error:
  monpanic(vm, "vgaInp: bailing.\n");
}

  void
vgaOutp(vm_t *vm, unsigned iolen, unsigned port, unsigned val)
{
  if ( iolen == 2 ) {
    vgaOutp(vm, 1, port, val&0xff);
    vgaOutp(vm, 1, port+1, (val>>8) & 0xff);
    return;
    }

  switch ( port ) {
    case 0x03d4: /* CRTC Index Register (color emulation modes) */
      vm->io.vga.CRTC.address = val & 0x7f;
      if (vm->io.vga.CRTC.address > 0x18) {
        monprint(vm, "vga W: invalid CRTC register 0x%02x selected.\n",
                vm->io.vga.CRTC.address);
        goto error;
        }
      return;

    case 0x03d5: /* CRTC Index Register (color emulation modes) */
      if (vm->io.vga.CRTC.address > 0x18) {
        monprint(vm, "vga W: invalid CRTC register 0x%02x selected.\n",
                vm->io.vga.CRTC.address);
        goto error;
        }
      vm->io.vga.CRTC.reg[vm->io.vga.CRTC.address] = val & 0xff;
      return;

    default:
      monprint(vm, "vgaOutp(0x%x, 0x%x).\n", port, val);
      return; // Fixme:
    }

error:
  monpanic(vm, "vgaOutp: bailing.\n");
}

  unsigned
picInp(vm_t *vm, unsigned iolen, unsigned port)
{
  switch (port) {
    case 0x21:
      // In polled mode, this is an int acknowledge.
      return( vm->io.picMaster.imr );


    default:
      monprint(vm, "picInp: port=0x%x\n", port);
      goto error;
    }

error:
  monpanic(vm, "vgaInp: bailing.\n");
}

  void
picOutp(vm_t *vm, unsigned iolen, unsigned port, unsigned val)
{
  unsigned bitmask;

  switch ( port ) {
    case 0x20:
      if ( val & 0x10 ) { // Init command 1.
        vm->io.picMaster.init.inInit = 1;
        vm->io.picMaster.init.requires4 = (val & 1);
        vm->io.picMaster.init.byteExpected = 2; // Next is command 2.
        vm->io.picMaster.imr = 0;
        vm->io.picMaster.isr = 0;
        vm->io.picMaster.irr = 0;
        vm->system.INTR = 0;
        if ( val & 0x0a ) {
          monprint(vm, "PIC(m): unsupported init command 0x%x\n", val);
          goto error;
          }
        return;
        }
      if ( (val & 0x18) == 0x08 ) { /* OCW3 */
        monprint(vm, "PIC(m): OCW3 unfinished.\n");
        goto error;
        }
      switch ( val ) {
        case 0x60: // Specific EOI 0
        case 0x63: // Specific EOI 3
          bitmask = 1 << (val-0x60);
          if ( !(vm->io.picMaster.isr & bitmask) ) {
            monprint(vm, "PIC(m): EOI %u, isr=0x%x.\n",
                    val-0x60, vm->io.picMaster.isr);
            goto error;
            }
          // Remove interrupt from ISR.
          vm->io.picMaster.isr &= ~bitmask;
          vm->system.INTR = 0;
          // Give PIC a chance to propogate another interrupt.
          picServiceMaster(vm);
          return;

        default:
          monprint(vm, "PIC(m): OCW2 0x%x.\n", val);
        }
      goto error;

    case 0x21:
      vm->io.picMaster.imr = val;
      picServiceMaster(vm);
      return;

    case 0xa0:
      if ( val & 0x10 ) { // Init command 1.
        vm->io.picSlave.init.inInit = 1;
        vm->io.picSlave.init.requires4 = (val & 1);
        vm->io.picSlave.init.byteExpected = 2; // Next is command 2.
        vm->io.picSlave.imr = 0;
        vm->io.picSlave.isr = 0;
        vm->io.picSlave.irr = 0;
        if ( val & 0x0a ) {
          monprint(vm, "PIC(s): unsupported init command 0x%x\n", val);
          goto error;
          }
        return;
        }
      if ( (val & 0x18) == 0x08 ) { /* OCW3 */
        monprint(vm, "PIC(s): OCW3 unfinished.\n");
        goto error;
        }
      monprint(vm, "PIC(s): OCW2 unfinished.\n");
      goto error;

    case 0xa1:
      vm->io.picSlave.imr = val;
      monprint(vm, "PIC(s) imr=0x%x\n", val);
      return;
    }

error:
  monpanic(vm, "picOutp: bailing.\n");
}

  void
picIrq(vm_t *vm, unsigned irqNum,  unsigned val)
{
  unsigned bitMask;
  pic_t   *picPtr;

  if ( irqNum > 15 )
    monpanic(vm, "picIrq: irq=%u\n", irqNum);

  // Assuming val==1

  if ( irqNum <= 7 ) {
    picPtr = &vm->io.picMaster;
    bitMask = 1<<irqNum;
    }
  else {
    picPtr = &vm->io.picSlave;
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
    vm->system.INTR = 1;
    }
  else { /* 8..15 */
    picIrq(vm, 2, 1); // Slave output is irq#2 input on Master.
    }
}

  void
picServiceMaster(vm_t *vm)
{
  unsigned irr;

  if ( vm->io.picMaster.isr ) {
    return; // An interrupt is already in-service.
    }

  // Find the highest priority requested interrupt which is not masked.
  irr = (vm->io.picMaster.irr & ~vm->io.picMaster.imr);
  if ( !irr )
    return; // No unmasked pending interrupts.

  // There are pending unmaked irqs; signal INTR to CPU.
  vm->system.INTR = 1;
  return;
}


  unsigned  
picIAC(vm_t *vm)
{
// Note: ignores slave PIC for now.
  unsigned vector, bit, irr;

  //monprint(vm, "picIAC:\n");
  if ( (!vm->system.INTR) || vm->io.picMaster.isr ) {
    monpanic(vm, "picIAC: INTR=%u, isr=0x%x.\n",
            vm->system.INTR, vm->io.picMaster.isr);
    }

  irr = (vm->io.picMaster.irr & ~vm->io.picMaster.imr);
  if ( !irr ) {
    monpanic(vm, "picIAC: no unmasked irqs.\n");
    }
  // Find the highest priority unmasked irq.
  for (bit=0; bit<=7; bit++) {
    if ( irr & (1<<bit) )
      break;
    }
  // Promote irq from the IRR to ISR register.
  vm->io.picMaster.irr &= ~(1<<bit);
  vm->io.picMaster.isr |=  (1<<bit);

  vm->system.INTR = 0;

  // Return a vector, adjusted by the offset that was configured.
  vector = vm->io.picMaster.vectorOffset + bit;

  return( vector );
}
