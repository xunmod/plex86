/*
 *  $Id$
 *
 *  Copyright (C) 2003-2004 Kevin P. Lawton
 *
 *  halconsole.c: HAL console for a guest Linux.
 */

#include <linux/init.h>
#include <linux/config.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#include "config.h"
#include "hal.h"


MODULE_AUTHOR("Kevin P. Lawton");
MODULE_DESCRIPTION("Plex86 guest console driver (HAL)");
MODULE_LICENSE("GPL");


  static unsigned
halConGuestWrite(unsigned device, unsigned len);

  static unsigned
halConGuestRegDev(unsigned device, unsigned rwAreaPAddr, unsigned rwAreaLen);

int  halcon_mod_init(void);
void halcon_mod_exit(void);


/* Storage area for communicating outside the guest. */
static halConGuestRwArea_t *halConGuestRwArea = 0;

#define HALCON_DUMMY (void *) halcon_dummy
static int halcon_dummy(void);

static const char *halcon_startup(void);
static void halcon_init(struct vc_data *conp, int init);
static void halcon_deinit(struct vc_data *conp);
static void halcon_clear(struct vc_data *conp, int sy, int sx, int height,
                         int width);
static void halcon_putc(struct vc_data *conp, int c, int ypos, int xpos);
static void halcon_putcs(struct vc_data *conp, const unsigned short *s, int count,
                         int ypos, int xpos);
static void halcon_cursor(struct vc_data *conp, int mode);
static int halcon_scroll(struct vc_data *conp, int t, int b, int dir,
                         int count);
static void halcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
                         int height, int width);
static int halcon_switch(struct vc_data *conp);
static int halcon_blank(struct vc_data *conp, int blank);
static int halcon_font_op(struct vc_data *conp, struct console_font_op *op);
static void halcon_invert_region(struct vc_data *conp, u16 *p, int cnt);

static void halcon_print(char *s);

const struct consw halcon = {
  con_startup:       halcon_startup, 
  con_init:          halcon_init,
  con_deinit:        halcon_deinit,
  con_clear:         halcon_clear,
  con_putc:          halcon_putc,
  con_putcs:         halcon_putcs,
  con_cursor:        halcon_cursor,
  con_scroll:        halcon_scroll,
  con_bmove:         halcon_bmove,
  con_switch:        halcon_switch,
  con_blank:         halcon_blank,
  con_font_op:       halcon_font_op,
  con_set_palette:   HALCON_DUMMY,
  con_scrolldelta:   HALCON_DUMMY,
  con_invert_region: halcon_invert_region,
  };


  const char *
halcon_startup(void)
{
  return "<<STARTUP>>";
}

  void
halcon_init(struct vc_data *conp, int init)
{
  halcon_print("<<INIT>>\n");
}

  void
halcon_deinit(struct vc_data *conp)
{
  halcon_print("<<DEINIT>>\n");
}

  void
halcon_clear(struct vc_data *conp, int sy, int sx, int height, int width)
{
  halcon_print("<<CLEAR>>\n");
}

  void
halcon_putc(struct vc_data *conp, int c, int ypos, int xpos)
{
  halcon_print("<<PUTC>>\n");
}

  void
halcon_putcs(struct vc_data *conp, const unsigned short *s, int count,
             int ypos, int xpos)
{
  unsigned i, quantum;

  if ( halConGuestRwArea ) { /* Be paranoid. */
    while ( count ) {
      if ( count > sizeof(halConGuestRwArea->txBuffer) )
        quantum = sizeof(halConGuestRwArea->txBuffer);
      else
        quantum = count;
      for (i = 0; i < quantum; i++) {
        halConGuestRwArea->txBuffer[i] = s[i] & 0xff;
        }
      (void) halConGuestWrite(0, quantum);
      count -= quantum; /* Decrement number of chars handled. */
      s += quantum; /* Advance string. */
      }
    }
  halcon_print("\n");
}

  void
halcon_cursor(struct vc_data *conp, int mode)
{
}


  int
halcon_scroll(struct vc_data *conp, int t, int b, int dir, int count)
{
  return 0;
}

  void
halcon_bmove(struct vc_data *conp, int sy, int sx, int dy, int dx,
                  int height, int width)
{
  halcon_print("<<BMOVE>>\n");
}

  int
halcon_switch(struct vc_data *conp)
{
  halcon_print("<<SWITCH>>\n");
  return 1; /* Redrawing needed. */
}

  int
halcon_blank(struct vc_data *conp, int blank)
{
  if ( blank ) {
    halcon_print("<<BLANK1>>\n");
    return 1; /* Tell console.c to redraw the screen. */
    }
  else {
    halcon_print("<<BLANK0>>\n");
    return 1; /* Tell console.c to redraw the screen. */
    }
}

  int
halcon_font_op(struct vc_data *conp, struct console_font_op *op)
{
  return -ENOSYS;
}

  void
halcon_invert_region(struct vc_data *conp, u16 *p, int cnt)
{
  halcon_print("<<INVERT>>");
}

  int
halcon_dummy(void)
{
  return 0;
}

  void
halcon_print(char *s)
{
  unsigned i, quantum, count;

  count = strlen(s);

  if ( halConGuestRwArea ) { /* Be paranoid. */
    while ( count ) {
      if ( count > sizeof(halConGuestRwArea->txBuffer) )
        quantum = sizeof(halConGuestRwArea->txBuffer);
      else
        quantum = count;
      for (i = 0; i < quantum; i++) {
        halConGuestRwArea->txBuffer[i] = s[i];
        }
      (void) halConGuestWrite(0, quantum);
      count -= quantum; /* Decrement number of chars handled. */
      s += quantum; /* Advance string. */
      }
    }
}

int halcon_mod_init(void)
{
  /* Allocate a page for use as the guest char Rx area. */
  halConGuestRwArea = (halConGuestRwArea_t *)
      get_zeroed_page(GFP_KERNEL | __GFP_DMA);
  if ( !halConGuestRwArea ) {
    return -ENODEV;
    }

  /* Register this char Rx area with the HAL. */
  halConGuestRegDev( 0, ((unsigned) halConGuestRwArea) - PAGE_OFFSET,
                     sizeof(*halConGuestRwArea) );

  take_over_console(&halcon, 0, MAX_NR_CONSOLES-1, 1);

  return 0; /* Ok. */
}

void halcon_mod_exit(void)
{
  /* Fixme: ??? */
}

  unsigned
halConGuestWrite(unsigned device, unsigned len)
{
  unsigned result;

  __asm__ volatile (
    "int $0xff"
    : "=a" (result)
    : "0" (HalCallConGuestWrite),
      "b" (device),
      "d" (len)
    );
  return result;
}

  unsigned
halConGuestRegDev(unsigned device, unsigned rwAreaPAddr, unsigned rwAreaLen)
{
  unsigned result;

  printk("halConGuestRegDev: buffer addr = 0x%x.\n", rwAreaPAddr);
  __asm__ volatile (
    "int $0xff"
    : "=a" (result)
    : "0" (HalCallConGuestRegDev),
      "b" (device),
      "c" (rwAreaPAddr),
      "d" (rwAreaLen)
    );
  return result;
}

module_init(halcon_mod_init);
module_exit(halcon_mod_exit);
