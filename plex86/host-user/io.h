/////////////////////////////////////////////////////////////////////////
//// $Id$
///////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton


#ifndef __IO_H__
#define __IO_H__


// PIT
#define PIT_LATCH_MODE_LSB   10
#define PIT_LATCH_MODE_MSB   11
#define PIT_LATCH_MODE_16BIT 12

#define CPU_CLOCK_FREQ_HZ 500000000
#define PIT_CLOCK_FREQ_HZ   1193182

// CMOS
#define  NUM_CMOS_REGS 64
#define  REG_SEC                     0x00
#define  REG_SEC_ALARM               0x01
#define  REG_MIN                     0x02
#define  REG_MIN_ALARM               0x03
#define  REG_HOUR                    0x04
#define  REG_HOUR_ALARM              0x05
#define  REG_WEEK_DAY                0x06
#define  REG_MONTH_DAY               0x07
#define  REG_MONTH                   0x08
#define  REG_YEAR                    0x09
#define  REG_STAT_A                  0x0a
#define  REG_STAT_B                  0x0b
#define  REG_STAT_C                  0x0c
#define  REG_STAT_D                  0x0d
#define  REG_DIAGNOSTIC_STATUS       0x0e  /* alternatives */
#define  REG_SHUTDOWN_STATUS         0x0f
#define  REG_EQUIPMENT_BYTE          0x14
#define  REG_CSUM_HIGH               0x2e
#define  REG_CSUM_LOW                0x2f
#define  REG_IBM_CENTURY_BYTE        0x32  /* alternatives */
#define  REG_IBM_PS2_CENTURY_BYTE    0x37  /* alternatives */





typedef struct {
  unsigned imr; // Interrupt Mask Register
  unsigned isr; // Interrupt in-Service Register
  unsigned irr; // Interrupt Request Register
  unsigned vectorOffset;
  struct {
    unsigned inInit;
    unsigned requires4;
    unsigned byteExpected;
    } init;
  } pic_t;

typedef struct {
  struct {
    unsigned  mode;
    unsigned  latch_mode;
    unsigned  input_latch_value;
    unsigned  input_latch_toggle;
    unsigned  output_latch_value;
    unsigned  output_latch_toggle;
    unsigned  output_latch_full;
    unsigned  counter_max;
    unsigned  counter;
    unsigned  bcd;
    unsigned  active;
    unsigned  GATE;     // GATE input  pin
    unsigned  OUT;      // OUT  output pin
    } timer[3];
  unsigned  speaker_data_on;
  unsigned  refresh_clock_div2;
  } pit_t;

typedef struct {
  unsigned mem_address;
  Bit8u    reg[NUM_CMOS_REGS];
  } cmos_t;

typedef struct {
  struct {
    unsigned address;
    unsigned reg[0x19];
    } CRTC;
  } vga_t;


// ===================
// Function prototypes
// ===================

extern unsigned  pitInp(unsigned iolen, unsigned port);
extern void      pitOutp(unsigned iolen, unsigned port, unsigned val);
extern unsigned  cmosInp(unsigned iolen, unsigned port);
extern void      cmosOutp(unsigned iolen, unsigned port, unsigned val);
extern unsigned  vgaInp(unsigned iolen, unsigned port);
extern void      vgaOutp(unsigned iolen, unsigned port, unsigned val);
extern void      pitExpireClocks(unsigned pitClocks);
extern void      picIrq(unsigned irqNum,  unsigned val);
extern unsigned  picIAC(void);
extern unsigned  picInp(unsigned iolen, unsigned port);
extern void      picServiceMaster(void);
extern void      picOutp(unsigned iolen, unsigned port, unsigned val);
extern void      doVGADump(void);



// =========
// Variables
// =========

extern pic_t    picMaster;
extern pic_t    picSlave;
extern unsigned port0x80;
extern pit_t    pit;
extern cmos_t   cmos;
extern vga_t    vga;

extern unsigned  cpuToPitRatio;


#endif  // __IO_H__
