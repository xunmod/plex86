#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2004 Kevin P. Lawton

.PHONY: all clean dist-clean

include ../Rules.make

GUEST_OS = linux

all:
	@$(MAKE) -C $(GUEST_OS)

clean:
	@$(MAKE) -C $(GUEST_OS) clean

dist-clean: 
	@$(MAKE) -C $(GUEST_OS) dist-clean
