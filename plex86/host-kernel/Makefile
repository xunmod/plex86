#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003 Kevin P. Lawton

.PHONY: all local-clean clean dist-clean

include ../Rules.make

subdirs := vmm $(HOST_OS)
subdirlibs := vmm/vmm.o $(HOST_OS)/$(HOST_OS).o

all:
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir ; done
	@$(MAKE) plex86.o
ifeq ($(HOST_OS), null)
	/bin/rm -f a.out
	$(HOST_KERNEL_CC) -o a.out $(subdirlibs)
endif


plex86.o: $(subdirlibs)
	$(LD) -r -o $@ $^

local-clean:
	@/bin/rm -f *.o a.out

clean: local-clean
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir clean ; done

dist-clean: local-clean
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir dist-clean ; done
