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
	@for subdir in $(subdirs); \
	  do if ! $(MAKE) -C $$subdir ; then break; fi; done
	@$(MAKE) vmm.o

vmm.o: $(subdirlibs)
	$(LD) -r -o $@ $^

local-clean:
	@/bin/rm -f *.o

clean: local-clean
	@for subdir in $(subdirs); \
	  do if ! $(MAKE) -C $$subdir clean ; then break; fi; done

dist-clean: local-clean
	@for subdir in $(subdirs); \
	  do if ! $(MAKE) -C $$subdir dist-clean ; then break; fi; done
