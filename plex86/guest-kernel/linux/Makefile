#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003 Kevin P. Lawton
#
#  This library is free software; you can redistribute it and/or
#  modify it under the terms of the GNU Lesser General Public
#  License as published by the Free Software Foundation; either
#  version 2 of the License, or (at your option) any later version.
#
#  This library is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public
#  License along with this library; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

.PHONY: drivers clean dist-clean

include ../../Rules.make

ifeq ($(GUEST_VERSION), 2.4)
  forceh  := -include $(GUEST_LINUX_SRC)/include/asm/eflags_if.h
  forceh  += -include $(GUEST_LINUX_SRC)/include/linux/modversions.h
  machdef := 
else
ifeq ($(GUEST_VERSION), 2.5)
  forceh  := -include $(GUEST_LINUX_SRC)/include/asm/eflags_if.h
  machdef := -I$(GUEST_LINUX_SRC)/include/asm-i386/mach-default
else
  # Fixme: Error should go here
endif
endif

#defs := -D__KERNEL__ -DMODULE -DMODVERSIONS
defs := -D__KERNEL__ -DMODULE
GUEST_KERNEL_CFLAGS := -I$(GUEST_LINUX_SRC)/include $(defs) \
  $(GUEST_KERNEL_CFLAGS) $(forceh) $(machdef) -iwithprefix include -nostdinc
INC_DIRS := -I../../include -I../include
MODULE_NAME := halnet
OBJS := halnet.o


.c.o:
	$(GUEST_KERNEL_CC) -c $(GUEST_KERNEL_CFLAGS) $(INC_DIRS) $<

drivers: halnet halconsole

halnet: $(OBJS)
	/bin/rm -f $@
	$(LD) -r -o $@ $^

halconsole: halconsole.o
	/bin/rm -f $@
	$(LD) -r -o $@ $^

clean:
	@/bin/rm -f *.o *.s $(MODULE_NAME)

dist-clean: clean
