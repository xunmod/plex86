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

.PHONY: clean dist-clean

include ../../Rules.make

ifeq ($(HOST_VERSION), 2.4)
  forceh  := -include $(HOST_LINUX_SRC)/include/linux/modversions.h
  machdef := 
else
ifeq ($(HOST_VERSION), 2.5)
  forceh  :=
  machdef := "-I$(HOST_LINUX_SRC)/include/asm-i386/mach-default"
else
  # Fixme: Error should go here
endif
endif

#defs := -D__KERNEL__ -DMODULE -DMODVERSIONS
defs := -D__KERNEL__ -DMODULE
HOST_KERNEL_CFLAGS := -I$(HOST_LINUX_SRC)/include $(defs) $(HOST_KERNEL_CFLAGS) \
  $(forceh) $(machdef) -iwithprefix include -nostdinc
INC_DIRS := -I../../include -I../include
OBJS := linux2.4.o


.c.o:
	$(HOST_KERNEL_CC) -c $(HOST_KERNEL_CFLAGS) $(INC_DIRS) $<


linux.o: $(OBJS)
	/bin/rm -f $@
	$(LD) -r -o $@ $^

clean:
	@/bin/rm -f *.o *.s $(MODULE_NAME)

dist-clean: clean
