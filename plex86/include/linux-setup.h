/////////////////////////////////////////////////////////////////////////
//// $Id$
/////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton


#ifndef __LINUX_SETUP_H__
#define __LINUX_SETUP_H__


// These are the defines for the single page of data exchanged between
// the bootloader sequence and the 32-bit protected mode Linux kernel.
// Since we don't have the 16-bit bootloader phases, we can simply set
// values in this data page.  For reference, you can look in the Linux
// kernel sources at the following files, as well as dig around in
// the source.
//
//   linux/Documentation/i386/zero-page.txt
//   linux/arch/i386/kernel/setup.c
//   linux/include/asm-i386/setup.h



// Defs from linux/arch/i386/kernel/setup.c for ramdisk_flags:
#define RAMDISK_IMAGE_START_MASK 0x07FF
#define RAMDISK_PROMPT_FLAG      0x8000
#define RAMDISK_LOAD_FLAG        0x4000

#define KernelCommandLineMax 2048

typedef struct {
  // For 0x00..0x3f, see 'struct screen_info' in linux/include/linux/tty.h.
  // I just padded out the VESA parts, rather than define them.

  /* 0x000 */ Bit8u   orig_x;
  /* 0x001 */ Bit8u   orig_y;
  /* 0x002 */ Bit16u  ext_mem_k;
  /* 0x004 */ Bit16u  orig_video_page;
  /* 0x006 */ Bit8u   orig_video_mode;
  /* 0x007 */ Bit8u   orig_video_cols;
  /* 0x008 */ Bit16u  unused1;
  /* 0x00a */ Bit16u  orig_video_ega_bx;
  /* 0x00c */ Bit16u  unused2;
  /* 0x00e */ Bit8u   orig_video_lines;
  /* 0x00f */ Bit8u   orig_video_isVGA;
  /* 0x010 */ Bit16u  orig_video_points;
  /* 0x012 */ Bit8u   pad0[0x20 - 0x12]; // VESA info.
  /* 0x020 */ Bit16u  cl_magic;  // Commandline magic number (0xA33F)
  /* 0x022 */ Bit16u  cl_offset; // Commandline offset.  Address of commandline
                                 // is calculated as 0x90000 + cl_offset, bu
                                 // only if cl_magic == 0xA33F.
  /* 0x024 */ Bit8u   pad1[0x40 - 0x24]; // VESA info.

  /* 0x040 */ Bit8u   apm_bios_info[20]; // struct apm_bios_info
  /* 0x054 */ Bit8u   pad2[0x80 - 0x54];

  // Following 2 from 'struct drive_info_struct' in drivers/block/cciss.h.
  // Might be truncated?
  /* 0x080 */ Bit8u   hd0_info[16]; // hd0-disk-parameter from intvector 0x41
  /* 0x090 */ Bit8u   hd1_info[16]; // hd1-disk-parameter from intvector 0x46

  // System description table truncated to 16 bytes
  // From 'struct sys_desc_table_struct' in linux/arch/i386/kernel/setup.c.
  /* 0x0a0 */ Bit16u  sys_description_len;
  /* 0x0a2 */ Bit8u   sys_description_table[14];
                        // [0] machine id
                        // [1] machine submodel id
                        // [2] BIOS revision
                        // [3] bit1: MCA bus

  /* 0x0b0 */ Bit8u   pad3[0x1e0 - 0xb0];
  /* 0x1e0 */ Bit32u  alt_mem_k;
  /* 0x1e4 */ Bit8u   pad4[4];
  /* 0x1e8 */ Bit8u   e820map_entries;
  /* 0x1e9 */ Bit8u   eddbuf_entries; // EDD_NR
  /* 0x1ea */ Bit8u   pad5[0x1f1 - 0x1ea];
  /* 0x1f1 */ Bit8u   setup_sects; // size of setup.S, number of sectors
  /* 0x1f2 */ Bit16u  mount_root_rdonly; // MOUNT_ROOT_RDONLY (if !=0)
  /* 0x1f4 */ Bit16u  sys_size; // size of compressed kernel-part in the
                                // (b)zImage-file (in 16 byte units, rounded up)
  /* 0x1f6 */ Bit16u  swap_dev; // (unused AFAIK)
  /* 0x1f8 */ Bit16u  ramdisk_flags;
  /* 0x1fa */ Bit16u  vga_mode; // (old one)
  /* 0x1fc */ Bit16u  orig_root_dev; // (high=Major, low=minor)
  /* 0x1fe */ Bit8u   pad6[1];
  /* 0x1ff */ Bit8u   aux_device_info;
  /* 0x200 */ Bit16u  jump_setup; // Jump to start of setup code,
                                  // aka "reserved" field.
  /* 0x202 */ Bit8u   setup_signature[4]; // Signature for SETUP-header, ="HdrS"
  /* 0x206 */ Bit16u  header_format_version; // Version number of header format;
  /* 0x208 */ Bit8u   setup_S_temp0[8]; // Used by setup.S for communication with
                                        // boot loaders, look there.
  /* 0x210 */ Bit8u   loader_type;
                        // 0 for old one.
                        // else 0xTV:
                        //   T=0: LILO
                        //   T=1: Loadlin
                        //   T=2: bootsect-loader
                        //   T=3: SYSLINUX
                        //   T=4: ETHERBOOT
                        //   V=version
  /* 0x211 */ Bit8u   loadflags;
                        // bit0 = 1: kernel is loaded high (bzImage)
                        // bit7 = 1: Heap and pointer (see below) set by boot
                        //   loader.
  /* 0x212 */ Bit16u  setup_S_temp1;
  /* 0x214 */ Bit32u  kernel_start;
  /* 0x218 */ Bit32u  initrd_start;
  /* 0x21c */ Bit32u  initrd_size;
  /* 0x220 */ Bit8u   setup_S_temp2[4];
  /* 0x224 */ Bit16u  setup_S_heap_end_pointer;
  /* 0x226 */ Bit8u   pad7[0x2d0 - 0x226];

  /* 0x2d0 : Int 15, ax=e820 memory map. */
  // (linux/include/asm-i386/e820.h, 'struct e820entry')
#define E820MAX  32
#define E820_RAM  1
#define E820_RESERVED 2
#define E820_ACPI 3 /* usable as RAM once ACPI tables have been read */
#define E820_NVS  4
  struct {
    Bit64u addr;
    Bit64u size;
    Bit32u type;
    } e820map[E820MAX];

  /* 0x550 */ Bit8u   pad8[0x600 - 0x550];

  // BIOS Enhanced Disk Drive Services.
  // (From linux/include/asm-i386/edd.h, 'struct edd_info')
  // Each 'struct edd_info is 78 bytes, times a max of 6 structs in array.
  /* 0x600 */ Bit8u   eddbuf[0x7d4 - 0x600];

  /* 0x7d4 */ Bit8u   pad9[0x800 - 0x7d4];
  /* 0x800 */ Bit8u   commandline[KernelCommandLineMax];
                        // kernel commandline as copied using cl_offset.


#if 0
// Conflicts I found with old definitions that were here before.
/* 0x1fe */ Bit16u  bootsect_magic;
/* 0x400 */ struct  gdt_entry gdt[128];  // wiped by e820map?
#endif
  } __attribute__ ((packed)) linuxSetupParams_t;


#endif  // __LINUX_SETUP_H__
