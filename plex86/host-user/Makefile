#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003  Kevin P. Lawton
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

.PHONY: all clean dist-clean

include ../Rules.make

EXE = linuxvm
INC_DIRS = -I../include

.c.o:
	$(HOST_USER_CC) -c $(HOST_USER_CFLAGS) $(INC_DIRS) $<

$(EXE): linuxvm.o io.o hal-user.o
	$(HOST_USER_CC) -o $@ $^

clean: 
	@/bin/rm -f *.o $(EXE)

dist-clean: clean
