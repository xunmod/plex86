#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003  Kevin P. Lawton

.PHONY: all clean dist-clean

include Rules.make

subdirs= host-kernel host-user guest-kernel guest-user

all:
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir ; done

clean:
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir clean ; done

dist-clean: 
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir dist-clean ; done
	@/bin/rm -f Rules.make include/config.h
	@/bin/rm -rf config.status config.log autom4te.cache config.cache
