#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003 Kevin P. Lawton

.PHONY: clean dist-clean top

include ../../../Rules.make

inc_dirs := -I../../../include -I../../include
objs := monitor.o


.c.o:
	$(HOST_KERNEL_CC) -c $(HOST_KERNEL_CFLAGS) $(inc_dirs) $<
.S.o:
	$(HOST_KERNEL_CC) -c $(HOST_KERNEL_CFLAGS) $(inc_dirs) -D__ASSEMBLY__ $<

host-space.o: $(objs)
	$(LD) -o $@ -r $^

clean: 
	@/bin/rm -f *.o *.s

dist-clean: clean

top:
	$(MAKE) -C ../../..
