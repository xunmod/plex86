#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003 Kevin P. Lawton

.PHONY: drivers clean dist-clean

include ../../Rules.make

ifeq ($(GUEST_VERSION), 2.4)
  forceh  := -include $(GUEST_LINUX_SRC)/include/asm/eflags_if.h
  forceh  += -include $(GUEST_LINUX_SRC)/include/linux/modversions.h
  machdef := 
else
ifeq ($(GUEST_VERSION), 2.5)
  forceh  := -include $(GUEST_LINUX_SRC)/include/asm/eflags_if.h
  machdef := -I$(GUEST_LINUX_SRC)/include/asm-i386/mach-default
else
  # Fixme: Error should go here
endif
endif

#defs := -D__KERNEL__ -DMODULE -DMODVERSIONS
defs := -D__KERNEL__ -DMODULE
GUEST_KERNEL_CFLAGS := -I$(GUEST_LINUX_SRC)/include $(defs) \
  $(GUEST_KERNEL_CFLAGS) $(forceh) $(machdef) -iwithprefix include -nostdinc
INC_DIRS := -I../../include -I../include
DRIVER_NAMES := halnet halconsole
OBJS := halnet.o


.c.o:
	$(GUEST_KERNEL_CC) -c $(GUEST_KERNEL_CFLAGS) $(INC_DIRS) $<

drivers: $(DRIVER_NAMES)

halnet: $(OBJS)
	/bin/rm -f $@
	$(LD) -r -o $@ $^

halconsole: halconsole.o
	/bin/rm -f $@
	$(LD) -r -o $@ $^

clean:
	@/bin/rm -f *.o *.s $(DRIVER_NAMES)

dist-clean: clean
