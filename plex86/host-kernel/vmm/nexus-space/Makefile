#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003 Kevin P. Lawton

.PHONY: clean dist-clean

include ../../../Rules.make

inc_dirs := -I../../../include -I../../include
objs := nexus.o util-nexus.o

.c.o:
	$(HOST_KERNEL_CC) -c $(HOST_KERNEL_CFLAGS) $(inc_dirs) $<
.S.o:
	$(HOST_KERNEL_CC) -c $(HOST_KERNEL_CFLAGS) $(inc_dirs) -D__ASSEMBLY__ $<

nexus-space.o: $(objs)
	$(LD) -o $@ -r $^

clean: 
	@/bin/rm -f *.o *.s

dist-clean: clean
