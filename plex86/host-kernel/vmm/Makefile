#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003 Kevin P. Lawton

.PHONY: all local-clean clean dist-clean

include ../../Rules.make

subdirs := host-space monitor-space nexus-space
subdirlibs := \
    host-space/host-space.o \
    monitor-space/monitor-space.o \
    nexus-space/nexus-space.o

all:
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir ; done
	@$(MAKE) vmm.o

vmm.o: $(subdirlibs)
	$(LD) -r -o $@ $^

local-clean:
	@/bin/rm -f *.o

clean: local-clean
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir clean ; done

dist-clean: local-clean
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir dist-clean ; done
