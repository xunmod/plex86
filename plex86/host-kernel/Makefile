#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003 Kevin P. Lawton
#
#  This library is free software; you can redistribute it and/or
#  modify it under the terms of the GNU Lesser General Public
#  License as published by the Free Software Foundation; either
#  version 2 of the License, or (at your option) any later version.
#
#  This library is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#  Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public
#  License along with this library; if not, write to the Free Software
#  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

.PHONY: all local-clean clean dist-clean

include ../Rules.make

subdirs := vmm $(HOST_OS)
subdirlibs := vmm/vmm.o $(HOST_OS)/$(HOST_OS).o

all:
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir ; done
	@$(MAKE) plex86.o

plex86.o: $(subdirlibs)
	$(LD) -r -o $@ $^

local-clean:
	@/bin/rm -f *.o

clean: local-clean
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir clean ; done

dist-clean: local-clean
	@for subdir in $(subdirs); do $(MAKE) -C $$subdir dist-clean ; done
