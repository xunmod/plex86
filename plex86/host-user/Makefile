#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003  Kevin P. Lawton

.PHONY: all clean dist-clean top

include ../Rules.make

EXE = linuxvm
INC_DIRS = -I../include

.c.o:
	$(HOST_USER_CC) -c $(HOST_USER_CFLAGS) $(INC_DIRS) $<

ifeq ($(HOST_OS), null)
# For a null host, we are only compiling kernel stuff.  NOP.
all:

endif

$(EXE): linuxvm.o hal-user.o x86-emu.o
	$(HOST_USER_CC) -o $@ $^

clean: 
	@/bin/rm -f *.o $(EXE)

dist-clean: clean

top:
	$(MAKE) -C ..
