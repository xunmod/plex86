#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003 Kevin P. Lawton

.PHONY: clean dist-clean all

include ../../Rules.make

INC_DIRS = -I../../include -I../include
OBJS = null.o


.c.o:
	$(HOST_KERNEL_CC) -c $(HOST_KERNEL_CFLAGS) $(INC_DIRS) $<


all: $(OBJS)

clean:
	@/bin/rm -f *.o *.s

dist-clean: clean
