#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003  Kevin P. Lawton

.PHONY: all clean dist-clean

include ../Rules.make


all:
	@if test "$(GUEST_OS)" != "none"; then $(MAKE) -C $(GUEST_OS); fi

clean:
	@if test "$(GUEST_OS)" != "none"; then $(MAKE) -C $(GUEST_OS) clean; fi

dist-clean: 
	@if test "$(GUEST_OS)" != "none"; then $(MAKE) -C $(GUEST_OS) dist-clean; fi
