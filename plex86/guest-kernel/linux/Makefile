#  $Id$
#
#  plex86: run multiple x86 operating systems concurrently
#  Copyright (C) 1999-2003 Kevin P. Lawton

.PHONY: all clean dist-clean

include ../../Rules.make

ifeq ($(GUEST_VERSION), none)
  forceh  :=
  machdef :=
else
ifeq ($(GUEST_VERSION), 2.4)
  forceh  := -include $(GUEST_LINUX_SRC)/include/asm/eflags_if.h
  forceh  += -include $(GUEST_LINUX_SRC)/include/linux/modversions.h
  machdef := 
else
ifeq ($(GUEST_VERSION), 2.5)
  forceh  := -include $(GUEST_LINUX_SRC)/include/asm/eflags_if.h
  machdef := -I$(GUEST_LINUX_SRC)/include/asm-i386/mach-default
else
ifeq ($(GUEST_VERSION), 2.6)
  forceh  := -include $(GUEST_LINUX_SRC)/include/asm/eflags_if.h
  machdef := -I$(GUEST_LINUX_SRC)/include/asm-i386/mach-default
else
  # Fixme: Error should go here
endif
endif
endif
endif

#defs := -D__KERNEL__ -DMODULE -DMODVERSIONS
defs := -D__KERNEL__ -DMODULE
GUEST_KERNEL_CFLAGS := -I$(GUEST_LINUX_SRC)/include $(defs) \
  $(GUEST_KERNEL_CFLAGS) $(forceh) $(machdef) -iwithprefix include -nostdinc
INC_DIRS := -I../../include -I../include
DRIVER_NAMES := halnet halconsole
OBJS := halnet.o

SETUP32_LDFLAGS = -nostartfiles -nostdlib -Wl,-Ttext,0x90000
SETUP32_DEBUG =
#SETUP32_LDFLAGS =
#SETUP32_DEBUG = -DDEBUG -g
SETUP32_CFLAGS = -Wall $(INC_DIRS) -fomit-frame-pointer \
          -mcpu=pentium $(SETUP32_DEBUG) -g -O2 $<

all-targets := setup32.img setup32-cmdline
ifneq ($(GUEST_VERSION), none)
all-targets += $(DRIVER_NAMES)
endif


.c.o:
	$(GUEST_KERNEL_CC) -c $(GUEST_KERNEL_CFLAGS) $(INC_DIRS) $<

.S.o:
	$(GUEST_KERNEL_CC) -c $(GUEST_KERNEL_CFLAGS) $(INC_DIRS) $<

all: $(all-targets)

halnet: $(OBJS)
	/bin/rm -f $@
	$(LD) -r -o $@ $^

halconsole: halconsole.o
	/bin/rm -f $@
	$(LD) -r -o $@ $^

setup32.img: setup32
	objcopy -O binary setup32 setup32.img
setup32: setup32-entry.o setup32.o
	$(GUEST_KERNEL_CC) -o $@ $(SETUP32_LDFLAGS) setup32-entry.o setup32.o
setup32.o: setup32.c
	$(GUEST_KERNEL_CC) -c $(SETUP32_CFLAGS) $<
setup32-entry.o: setup32-entry.S
	$(GUEST_KERNEL_CC) -c $(SETUP32_CFLAGS) $<

# The setup32-cmdline is an ancillary program which can read in a
# binary image version of setup32 and set the command lines for both
# setup32 and the Linux kernel.  Plex86 does this already, but this
# is a helpful utility for using setup32 in other simulators/VMs,
# like bochs, where linux can be loaded directly into memory.  The
# output is a file of the same size as setup32 but with the command
# lines written in the appropriate buffer areas.
# NOTE: this is really a program run in the host user space.
setup32-cmdline: setup32-cmdline.o
	$(HOST_USER_CC) -o $@ $^
setup32-cmdline.o: setup32-cmdline.c
	$(HOST_USER_CC) -c $(HOST_USER_CFLAGS) $(INC_DIRS) $<

clean:
	@/bin/rm -f *.o *.s $(DRIVER_NAMES)

dist-clean: clean
