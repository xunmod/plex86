/*
 *  $Id$
 *
 *  plex86: run multiple x86 operating systems concurrently
 *  Copyright (C) 1999-2004 Kevin P. Lawton
 *
 *  util-nexus.c: convenience routines which can be accessed from
 *    either space.
 *
 */


#include "plex86.h"
/* These functions are available from either space. */
#define IN_MONITOR_SPACE
#define IN_HOST_SPACE
#include "monitor.h"


static const unsigned int power_of_ten[] = {

  1,
  10,
  100,
  1000,
  10000,
  100000,
  1000000,
  10000000,
  100000000,
  1000000000,
  };


  void
nexusMemZero(void *ptr, int size)
{
    char *p = ptr;
    while (size--)
        *p++ = 0;
}

  void
nexusMemCpy(void *dst, void *src, int size)
{
    char *d = dst;
    char *s = src;
    while (size--)
        *d++ = *s++;
}

  void *
nexusMemSet(void *dst, unsigned c, unsigned n)
{
  unsigned char *d = dst;
  while (n--) {
    *d++ = c;
    }
  return(dst);
}

/* For now, this is a simple vsnprintf() type of function.  We need
 * to fill this out a little.
 */

  int
nexusVsnprintf(char *str, unsigned size, const char *fmt, va_list args)
{
  int count = 0;
  unsigned format_width;
  unsigned char c;

  while (*fmt) {
    switch (*fmt) {

      case '%':
        format_width = 0;
        fmt++;
        c = *fmt++;
        /* Get optional field width */
        if ( (c>='0') && (c<='9') ) {
          do {
            format_width = (format_width * 10) + (c - '0');
            c = *fmt++;
            } while ( (c>='0') && (c<='9') );
          }
        /* %x: hexadecimal */
        if ( c == 'x' ) {
          unsigned int val, leadin;
          int j;
          unsigned nibble;

          val = va_arg(args, unsigned int);
          leadin = 1;

          for (j=7; j>=0; j--) {
            nibble = (val >> (4 * j)) & 0x0f;
            if (leadin && j && !format_width && !nibble)
              continue;
            if (leadin && j && format_width && ((j+1)>format_width) &&
                !nibble)
              continue;
            leadin = 0;
            if ( (count+2) >= size ) goto error;
            if (nibble <= 9)
              *str++ = nibble + '0';
            else
              *str++ = (nibble-10) + 'A';
            count++;
            }
          break;
          }

        /* %c: character */
        if ( c == 'c' ) {
          unsigned char val;
          val = va_arg(args, unsigned);
          if ( (count+2) >= size ) goto error;
          *str++ = val;
          count++;
          break;
          }

        /* %s: string */
        if ( c == 's' ) {
          unsigned char *s;
          s = va_arg(args, unsigned char *);
          if ( (count+2) >= size ) goto error;
          count++;
          while (*s) {
            if ( (count+2) >= size ) goto error;
            *str++ = *s++; /* Copy char from string to output buffer. */
            count++;
            }
          break;
          }

        /* %u: unsigned int */
        if ( c == 'u' ) {
          unsigned int val, leadin;
          int j;
          unsigned digit;

          val = va_arg(args, unsigned int);
          leadin = 1;

          for (j=9; j>=0; j--) {
            if (leadin && j && !format_width && (val < power_of_ten[j]))
              continue;
            if (leadin && j && format_width && ((j+1)>format_width) &&
                (val < power_of_ten[j]))
              continue;
            leadin = 0;
            digit = (val / power_of_ten[j]);
            if ( (count+2) >= size ) goto error;
            *str++ = digit + '0';
            count++;
            val -= (digit * power_of_ten[j]);
            }
          break;
          }
        /* %b : binary (non-standard but useful) */
        if ( c == 'b' ) {
          unsigned int val, bit, leadin;
          int j;
          val = va_arg(args, unsigned int);
          leadin = 1;
          for (j=31; j>=0; j--) {
            bit = (val >> j) & 1;
            if (leadin && j && !format_width && !bit)
              continue;
            if (leadin && j && format_width && ((j+1)>format_width) && !bit)
              continue;
            leadin = 0;
            if ( (count+2) >= size ) goto error;
            *str++ = bit + '0';
            count++;
            }
          break;
          }

        /* Error, unrecognized format char */
        goto error;
        break;

      default:
        /* pass char through */
        if ( (count+2) >= size ) goto error;
        *str++ = *fmt++;
        count++;
        break;
      }
    }

  *str = 0; /* Complete string with null char */
  return(count);

error:
  return(-1);
}
