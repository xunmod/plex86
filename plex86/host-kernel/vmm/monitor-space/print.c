/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2004 Kevin P. Lawton
 *
 *  print-nexus.c:  Monitor debug print facility
 *
 */


#include "plex86.h"
#define IN_MONITOR_SPACE
#include "monitor.h"

int mon_vprint(vm_t *vm, char *fmt, va_list args);

  int
monprint(vm_t *vm, char *fmt, ...)
{
  va_list args;
  int ret;

  va_start(args, fmt);
  ret = mon_vprint(vm, fmt, args);
  va_end(args);
  return(ret);
}

  int
mon_vprint(vm_t *vm, char *fmt, va_list args)
{
  unsigned offset, remaining;
  unsigned char *log_buff_p;
  int ret;

  if (vm->monLogBufferInfo.locked)
    return 0;

  vm->monLogBufferInfo.locked = 1;
  offset = vm->monLogBufferInfo.offset;

  /* Sanity check */
  if (offset >= LOG_BUFF_SIZE) {
    resetPrintBuf(vm);
    vm->monLogBufferInfo.locked = 0;
    return(0);
    }

  remaining = LOG_BUFF_SIZE - offset;
  log_buff_p = &vm->guest.addr.monLogBuffer[offset];

  ret = nexusVsnprintf(log_buff_p, remaining, fmt, args);

  if (ret == -1) {
    /* Terminate current contents since new print request did not work. */
    *log_buff_p = 0;
    /* Request that the current buffer contents be printed. */
    toHostFlushPrintBuf(vm);
    resetPrintBuf(vm);

    /* Print request did not fit.  dump buffer contents and try again
     * using whole buffer.
     */
    remaining = LOG_BUFF_SIZE;
    log_buff_p = &vm->guest.addr.monLogBuffer[0];
    ret = nexusVsnprintf(log_buff_p, remaining, fmt, args);
    if (ret == -1) {
      /* We have serious problems.  This print request will not even
       * fit in the whole buffer.
       */
      resetPrintBuf(vm);
      /* xxx Put error in buffer here. */
      return(0);
      }
    }
  vm->monLogBufferInfo.offset += ret;
toHostFlushPrintBuf(vm);
resetPrintBuf(vm);
  vm->monLogBufferInfo.locked = 0;
  return(ret);
}

  void
resetPrintBuf(vm_t *vm)
{
  vm->monLogBufferInfo.offset = 0;
  vm->monLogBufferInfo.error = 0;
  vm->guest.addr.monLogBuffer[0] = 0;
}
