/////////////////////////////////////////////////////////////////////////
//// $Id$
/////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2004 Kevin P. Lawton


// This is a simple bootloader for Linux, which works strictly in
// 32-bit protected-mode.  It is built such that an external source
// (plex86 or simulator) load the kernel image and parameters, and this
// program and its parameters into memory at specific addresses.
// This bootloader can then initialize the Linux setup page, minimal
// CPU and minimal IO; enough to pass control off to the Linux kernel.


// Memory Layout:
// 0x090000 Linux setup area (2k)
// 0x090800 Linux command line (2k)
// 0x091000 Boot GDT (2k)
// 0x091800 Bootloader command line (2k)
// 0x092000 Bootloader stack (4k)
// 0x093000 Bootloader code start (this program)
// 0x100000 Linux kernel

#define LinuxKernelStart 0x100000

#define KernelSetupSize     2048
#define KernelCLSize        2048
#define BootloaderGDTSize   2048
#define BootloaderCLSize    2048
#define BootloaderStackSize 4096

typedef unsigned char      Bit8u;
typedef unsigned short     Bit16u;
typedef unsigned int       Bit32u;
typedef unsigned long long Bit64u;

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
  /* 0x800: 2k kernel command line area.  We don't define it here. */
  // kernel commandline as copied using cl_offset.


#if 0
// Conflicts I found with old definitions that were here before.
/* 0x1fe */ Bit16u  bootsect_magic; // Set to 0xaa55 on bochs.
/* 0x400 */ struct  gdt_entry gdt[128];  // wiped by e820map?
#endif
  } __attribute__ ((packed)) linuxSetupParams_t;



typedef struct {
  Bit32u low;
  Bit32u high;
  } __attribute__ ((packed)) gdt_entry_t ;

extern unsigned char kernelSetupArea[];
extern unsigned char kernelCommandLine[];
extern gdt_entry_t   bootloaderGDT[];
extern unsigned char bootloaderCommandLine[];
extern unsigned char bootloaderStack[];




#if 0
// From linux/include/asm-i386/setup.h
#define PARAM ((unsigned char *)empty_zero_page)

ok #define SCREEN_INFO (*(struct screen_info *) (PARAM+0))
ok #define EXT_MEM_K (*(unsigned short *) (PARAM+2))
ok #define ALT_MEM_K (*(unsigned long *) (PARAM+0x1e0))
ok #define APM_BIOS_INFO (*(struct apm_bios_info *) (PARAM+0x40))
-- #define DRIVE_INFO (*(struct drive_info_struct *) (PARAM+0x80))
ok #define SYS_DESC_TABLE (*(struct sys_desc_table_struct*)(PARAM+0xa0))
ok #define MOUNT_ROOT_RDONLY (*(unsigned short *) (PARAM+0x1F2))
ok #define RAMDISK_FLAGS (*(unsigned short *) (PARAM+0x1F8))
ok #define VIDEO_MODE (*(unsigned short *) (PARAM+0x1FA))
ok #define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
ok #define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
ok #define LOADER_TYPE (*(unsigned char *) (PARAM+0x210))
ok #define KERNEL_START (*(unsigned long *) (PARAM+0x214))
ok #define INITRD_START (*(unsigned long *) (PARAM+0x218))
ok #define INITRD_SIZE (*(unsigned long *) (PARAM+0x21c))
ok #define COMMAND_LINE ((char *) (PARAM+2048))

ok #define EDD_NR     (*(unsigned char *) (PARAM+EDDNR))
ok #define EDD_BUF     ((struct edd_info *) (PARAM+EDDBUF))
ok #define E820_MAP_NR (*(char*) (PARAM+E820NR))
ok #define E820_MAP    ((struct e820entry *) (PARAM+E820MAP))
#endif

// initrd= [BOOT] Specify the location of the initial ramdisk
// ramdisk_size= [RAM] Sizes of RAM disks in kilobytes
//         New name for the ramdisk parameter.
//         See Documentation/ramdisk.txt.
// ramdisk_start=  [RAM] Starting block of RAM disk image (so you can
//         place it after the kernel image on a boot floppy).
//         See Documentation/ramdisk.txt.
// Kernel command line. (Linux uses only 128 max bytes out of 2k buffer)
//memset(params->commandline, 0, sizeof(params->commandline));
//  w/ devfs:
//    root=/dev/ram0 init=/linuxrc rw
//  w/o devfs:
//    root=/dev/rd/0 init=/linuxrc rw
//strcpy((char*)params->commandline, "root=/dev/ram0 init=/linuxrc rw"
//                                   " mem=nopentium");
//strcpy((char*)params->commandline, "root=/dev/rd/0 init=/linuxrc rw");



// ===================
// Function prototypes
// ===================

void        bootloaderMain(void);
static void initLinuxSetupPage(void);
static int  strcmp(const char *s1, const char *s2);
static unsigned stringToUInt(const char *s);
static void bl_memset(void *s, int c, unsigned n);
static void bl_memcpy(void *dest, const void *src, unsigned n);
static void panic(void);
static Bit8u    inb( Bit16u port );
static Bit16u   inw( Bit16u port );
static void     outb( Bit16u port, Bit8u val );
static void     outw( Bit16u port, Bit16u val );


// =========
// Variables
// =========

static unsigned plex86MemSize = 0;
static Bit32u initrdImageLoadAddr = 0; // Signal no initrd.
static Bit32u initrdImageSize=0;
static unsigned initIO = 0;

// A structure for loading the GDT with the LGDT instruction.
struct {
  Bit16u limit __attribute__ ((packed));
  Bit32u addr  __attribute__ ((packed));
  } __attribute__ ((packed)) loadGDT = { 0x400, (Bit32u) &bootloaderGDT };


static struct {
  Bit16u len __attribute__ ((packed));
  Bit16u op  __attribute__ ((packed));
  Bit16u port  __attribute__ ((packed));
  Bit16u val  __attribute__ ((packed));
  } __attribute__ ((packed)) vgaIOLog[] = {
{1,0,0x03cc,0xc3},
{1,1,0x03c2,0xc3},
{2,1,0x03b4,0xaa0d},
{1,1,0x03b4,0x0d},
{1,0,0x03b5,0xff},
{1,0,0x03cc,0xc3},
{1,1,0x03c2,0xc2},
{2,1,0x03d4,0xaa0d},
{1,1,0x03d4,0x0d},
{1,0,0x03d5,0xff},
{1,0,0x03da,0xff},
{1,0,0x03ba,0x00},
{1,1,0x03c0,0x00},
{2,1,0x03c4,0x0100},
{1,1,0x03c2,0x67},
{2,1,0x03c4,0x0001},
{2,1,0x03c4,0x0302},
{2,1,0x03c4,0x0003},
{2,1,0x03c4,0x0204},
{2,1,0x03c4,0x0300},
{2,1,0x03d4,0x2011},
{2,1,0x03d4,0x5f00},
{2,1,0x03d4,0x4f01},
{2,1,0x03d4,0x5002},
{2,1,0x03d4,0x8203},
{2,1,0x03d4,0x5504},
{2,1,0x03d4,0x8105},
{2,1,0x03d4,0xbf06},
{2,1,0x03d4,0x1f07},
{2,1,0x03d4,0x0008},
{2,1,0x03d4,0x4f09},
{2,1,0x03d4,0x0d0a},
{2,1,0x03d4,0x0e0b},
{2,1,0x03d4,0x000c},
{2,1,0x03d4,0x000d},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x000f},
{2,1,0x03d4,0x9c10},
{2,1,0x03d4,0x8e11},
{2,1,0x03d4,0x8f12},
{2,1,0x03d4,0x2813},
{2,1,0x03d4,0x1f14},
{2,1,0x03d4,0x9615},
{2,1,0x03d4,0xb916},
{2,1,0x03d4,0xa317},
{2,1,0x03d4,0xff18},
{2,1,0x03ce,0x0000},
{2,1,0x03ce,0x0001},
{2,1,0x03ce,0x0002},
{2,1,0x03ce,0x0003},
{2,1,0x03ce,0x0004},
{2,1,0x03ce,0x1005},
{2,1,0x03ce,0x0e06},
{2,1,0x03ce,0x0007},
{2,1,0x03ce,0xff08},
{1,1,0x03da,0x00},
{1,1,0x03c4,0x01},
{1,0,0x03c5,0x00},
{2,1,0x03c4,0x2001},
{1,0,0x03da,0x00},
{1,1,0x03c0,0x00},
{1,1,0x03c0,0x00},
{1,1,0x03c0,0x01},
{1,1,0x03c0,0x01},
{1,1,0x03c0,0x02},
{1,1,0x03c0,0x02},
{1,1,0x03c0,0x03},
{1,1,0x03c0,0x03},
{1,1,0x03c0,0x04},
{1,1,0x03c0,0x04},
{1,1,0x03c0,0x05},
{1,1,0x03c0,0x05},
{1,1,0x03c0,0x06},
{1,1,0x03c0,0x14},
{1,1,0x03c0,0x07},
{1,1,0x03c0,0x07},
{1,1,0x03c0,0x08},
{1,1,0x03c0,0x38},
{1,1,0x03c0,0x09},
{1,1,0x03c0,0x39},
{1,1,0x03c0,0x0a},
{1,1,0x03c0,0x3a},
{1,1,0x03c0,0x0b},
{1,1,0x03c0,0x3b},
{1,1,0x03c0,0x0c},
{1,1,0x03c0,0x3c},
{1,1,0x03c0,0x0d},
{1,1,0x03c0,0x3d},
{1,1,0x03c0,0x0e},
{1,1,0x03c0,0x3e},
{1,1,0x03c0,0x0f},
{1,1,0x03c0,0x3f},
{1,1,0x03c0,0x10},
{1,1,0x03c0,0x0c},
{1,1,0x03c0,0x11},
{1,1,0x03c0,0x00},
{1,1,0x03c0,0x12},
{1,1,0x03c0,0x0f},
{1,1,0x03c0,0x13},
{1,1,0x03c0,0x08},
{1,0,0x03da,0x00},
{1,1,0x03c0,0x14},
{1,1,0x03c0,0x00},
{1,1,0x03c6,0xff},
{1,1,0x03c8,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x00},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x2a},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x15},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{1,1,0x03c9,0x3f},
{2,1,0x03ce,0x0005},
{1,1,0x03ce,0x06},
{1,0,0x03cf,0x0c},
{2,1,0x03ce,0x0406},
{2,1,0x03c4,0x0402},
{2,1,0x03c4,0x0604},
{1,1,0x03c4,0x01},
{1,0,0x03c5,0x20},
{1,0,0x03cc,0x67},
{2,1,0x03ce,0x0e06},
{2,1,0x03ce,0x1005},
{2,1,0x03c4,0x0302},
{2,1,0x03c4,0x0204},
{1,1,0x03c4,0x01},
{1,0,0x03c5,0x20},
{2,1,0x03c4,0x0001},
{1,0,0x03da,0x00},
{1,0,0x03ba,0xff},
{1,1,0x03c0,0x20},
{1,0,0x03da,0x00},
{1,0,0x03ba,0xff},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x000f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x010f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x020f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x030f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x040f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x050f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x060f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x070f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x080f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x090f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x0a0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x0b0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x0c0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x0d0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x0e0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x0f0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x100f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x110f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x120f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x130f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x140f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x150f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x160f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x170f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x180f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x000f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x000f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x500f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x500f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x510f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x520f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x530f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x540f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x550f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x560f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x570f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x580f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x590f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x5a0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x5b0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x5c0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x5d0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x5e0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x5f0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x600f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x610f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x620f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x630f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x640f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x650f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x660f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x670f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x680f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x690f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x6a0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x6b0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x6c0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x6d0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x6e0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x6f0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x700f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x710f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x720f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x730f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x740f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x750f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x760f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x770f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x780f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x790f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x7a0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x7b0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x500f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0x500f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa00f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa00f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa10f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa20f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa30f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa40f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa50f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa60f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa70f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa80f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa90f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xaa0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xab0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xac0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xad0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xae0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xaf0f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xb00f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xb10f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xb20f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xb30f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xb40f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa00f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xa00f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xf00f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xf00f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xf00f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xf00f},
{2,1,0x03d4,0x000e},
{2,1,0x03d4,0xf00f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x400f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x400f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x410f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x420f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x430f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x440f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x450f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x460f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x470f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x480f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x490f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x4a0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x4b0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x4c0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x4d0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x4e0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x4f0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x500f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x510f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x520f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x530f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x540f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x550f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x560f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x570f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x580f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x590f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x5a0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x5b0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x5c0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x5d0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x5e0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x5f0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x600f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x610f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x620f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x630f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x640f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x650f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x660f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x670f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x680f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x690f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x6a0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x6b0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x6c0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x6d0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x6e0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x6f0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x700f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x710f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x720f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x730f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x740f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x750f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x760f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x770f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x780f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x790f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x7a0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x7b0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x7c0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x7d0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x7e0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x7f0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x800f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x810f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x820f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x400f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x400f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x900f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x900f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x910f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x920f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x930f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x940f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x950f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x960f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x970f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x980f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x990f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x9a0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x9b0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x9c0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x9d0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x9e0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x9f0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xa00f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xa10f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xa20f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xa30f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xa40f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xa50f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xa60f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xa70f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xa80f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xa90f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xaa0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xab0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xac0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xad0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xae0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xaf0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xb00f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xb10f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xb20f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xb30f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xb40f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xb50f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xb60f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xb70f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xb80f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xb90f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xba0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xbb0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xbc0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xbd0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xbe0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xbf0f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xc00f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xc10f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0x900f},
{2,1,0x03d4,0x010e},
{2,1,0x03d4,0xe00f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x300f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x310f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x320f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x330f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x340f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x350f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x360f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x370f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x380f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x390f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x3a0f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x3b0f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x3c0f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x3d0f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x3e0f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x3f0f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x400f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x410f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x420f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x300f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0x800f},
{2,1,0x03d4,0x020e},
{2,1,0x03d4,0xd00f},
};


#ifdef DEBUG
#include <stdio.h>
#include <string.h>

  int
main(int argc, char *argv[])
{
  printf("sizeof(linuxSetupParams_t) = %u.\n", sizeof(linuxSetupParams_t));
  printf("sizeof(loadGDT) = %u.\n", sizeof(loadGDT));
  strcpy(bootloaderCommandLine, "-megs 16 -initrd-image 0x2000000 0x1000");
  bootloaderMain();
  return(0);
}
#endif


  void
bootloaderMain(void)
{
  unsigned argi = 0; /* Start at 0th arg; there is no prog name. */
  int nMegs;
#define ARGV_MAX 64
  unsigned char *argv[ARGV_MAX];
  unsigned argc;
  unsigned char *ptr;

  // Sanity checks.
  if (sizeof(linuxSetupParams_t) != KernelSetupSize) {
    goto error;
    }
  if (sizeof(loadGDT) != 6) {
    goto error;
    }

  // Convert from a parameter string into argv[], argc format.
  // We can simplify by terminating each arg separated by a whitespace,
  // with a NULL in-place, since the memory is ours to do what
  // we want.
  argc = 0;
  argv[0] = 0;
  ptr = bootloaderCommandLine;
  do {
    while (*ptr==' ') // Eat leading whitespace.
      ptr++;
    if ( ! *ptr )
      break; // No more args left.
    if ( (argc+1) >= ARGV_MAX ) // Overflow check.
      goto error;
    argv[argc++] = ptr; // Save start of arg.
    while (*ptr && (*ptr != ' ')) // Find end of arg.
      ptr++;
    if ( *ptr ) {
      *ptr++ = 0; // Arg is followed by whitespace; terminate and advance.
      }
    } while (*ptr);

  /* Initialize before processing command line. */
  nMegs = 0;

#define CheckArgsForParameter(n) \
    if ( (argi+(n)) >= argc ) goto errorArgsUnderflow


  /* Process command line. */
  while (argi < argc) {
    if ( !strcmp(argv[argi], "-megs") ) {
      CheckArgsForParameter(1);
      nMegs = stringToUInt(argv[argi+1]);
      plex86MemSize = nMegs << 20;
      argi += 2;
      }
    else if ( !strcmp(argv[argi], "-initrd-image") ) {
      CheckArgsForParameter(2);
      initrdImageLoadAddr = stringToUInt(argv[argi+1]);
      if ( initrdImageLoadAddr == 0 ) {
        goto errorUsage;
        }
      initrdImageSize = stringToUInt(argv[argi+2]);
      if ( initrdImageSize == 0)
        goto error;
      argi += 3;
      }
    else if ( !strcmp(argv[argi], "-init-io") ) {
      initIO = 1;
      argi += 1;
      }
    else {
      goto errorArgUnrecognized;
      }
    }

  // Argument consistency checks.
  if ( nMegs == 0 ) {
    goto errorUsage;
    }

  if (initIO) {
    // Initialize VGA to a reasonable state.  The BIOS normally does
    // this and then passes the resultant mode to the Linux kernel.
    unsigned i, vgaIOEntries;
    vgaIOEntries = sizeof(vgaIOLog) / sizeof(vgaIOLog[0]);
    for (i=0; i<vgaIOEntries; i++) {
      if ( vgaIOLog[i].op == 0 ) { // Read.
        if ( vgaIOLog[i].len == 1)
          (void) inb( vgaIOLog[i].port );
        else
          (void) inw( vgaIOLog[i].port );
        }
      else if ( vgaIOLog[i].op == 1 ) { // Write.
        if ( vgaIOLog[i].len == 1)
          outb( vgaIOLog[i].port, vgaIOLog[i].val );
        else
          outw( vgaIOLog[i].port, vgaIOLog[i].val );
        }
      else
        panic();
      }
    }

  initLinuxSetupPage();

  if (initIO) {
    // Note: A20 line should be enabled already.

    // Setup PICs the way Linux likes it
    outb( 0x20, 0x11 );
    outb( 0xA0, 0x11 );
    outb( 0x21, 0x20 );
    outb( 0xA1, 0x28 );
    outb( 0x21, 0x04 );
    outb( 0xA1, 0x02 );
    outb( 0x21, 0x01 );
    outb( 0xA1, 0x01 );
    outb( 0x21, 0xFF );
    outb( 0xA1, 0xFB );

    // Disable interrupts and NMIs
    // Note: EFLAGS.IF should be 0 already.
    outb( 0x70, 0x80 );
    }

#ifndef DEBUG
  asm volatile (
    // Addr of setup page passed to Linux kernel via ESI.
    "movl $kernelSetupArea, %esi \n\t"
    "ljmp $0x10, $0x100000 \n\t" // Fixme: should be LinuxKernelStart
    );
#endif


errorArgUnrecognized:
errorUsage:
errorArgsUnderflow: // Not enough parameters in command line for option.
error:
  panic();
}

  void
initLinuxSetupPage(void)
{
  linuxSetupParams_t *params =
         (linuxSetupParams_t *) kernelSetupArea;
  Bit32u memNMegs = plex86MemSize >> 20;

  bl_memset( params, '\0', KernelSetupSize);

#if 0
  // Stuff from older code.  These fields conflicted with new
  // definitions of the boot parameter area, as per the file
  // "linux/Documentation/i386/zero-page.txt".
  params->bootsect_magic = 0xaa55;
#endif

  params->orig_x = 0;
  params->orig_y = 0;
  /* Memory size (total mem - 1MB, in KB) */
  params->ext_mem_k = (memNMegs - 1) * 1024;
  params->orig_video_page = 0;
  params->orig_video_mode = 3;
  params->orig_video_cols = 80;
  params->orig_video_ega_bx = 3;
  params->orig_video_lines = 25;
  params->orig_video_isVGA = 1;
  params->orig_video_points = 16;

  // Commandline magic.  If it's 0xa33f, then the address of the commandline
  // is calculated as 0x90000 + cl_offset.
  params->cl_magic = 0xa33f;
  params->cl_offset = 0x800;

  //bl_memset(&params->apm_bios_info, 0, sizeof(params->apm_bios_info));
  //bl_memset(&params->hd0_info, 0, sizeof(params->hd0_info));
  //bl_memset(&params->hd1_info, 0, sizeof(params->hd1_info));
  params->sys_description_len = 0; // Make at least 4.
  params->sys_description_table[0] = 0; // FIXME
  params->sys_description_table[1] = 0; // FIXME
  params->sys_description_table[2] = 0; // FIXME
  params->sys_description_table[3] = 0; // FIXME
  params->alt_mem_k = params->ext_mem_k; // FIXME
  params->e820map_entries = 0;
  params->eddbuf_entries = 0;
  params->setup_sects = 0; // size of setup.S, number of sectors
  params->mount_root_rdonly = 0;
  params->sys_size = 0; // FIXME: size of compressed kernel-part in the
                        // (b)zImage-file (in 16 byte units, rounded up)
  params->swap_dev = 0; // (unused AFAIK)
  if ( initrdImageLoadAddr ) {
    // params->ramdisk_flags = RAMDISK_LOAD_FLAG; // FIXME
    params->ramdisk_flags = 0; // FIXME
    }
  else {
    params->ramdisk_flags = 0; // FIXME
    }
  params->vga_mode = 3; // FIXME: (old one)
  params->orig_root_dev = 0x0000; // (high=Major,low=minor) fixme: bochs 0x0100
  params->aux_device_info = 0; // FIXME
  params->jump_setup = 0; // FIXME
  bl_memcpy(params->setup_signature, "HdrS", 4); // Signature for SETUP-header.
  params->header_format_version = 0x0203; // Current header format.
  //bl_memset(params->setup_S_temp0, 0, sizeof(params->setup_S_temp0));
  params->loader_type = 1; // Loadlin.  Should we use this?

  // bit0 = 1: kernel is loaded high (bzImage)
  // bit7 = 1: Heap and pointer (see below) set by boot loader.
  params->loadflags = 0x1;
  params->setup_S_temp1 = 0; // FIXME

  params->kernel_start = LinuxKernelStart;
  params->initrd_start = initrdImageLoadAddr;
  params->initrd_size  = initrdImageSize;

  //bl_memset(params->setup_S_temp2, 0, sizeof(params->setup_S_temp2));
  params->setup_S_heap_end_pointer = 0; // FIXME

  // Int 15, ax=e820 memory map.
  // params->e820map[0].addr = ;
  // params->e820map[0].size = ;
  // params->e820map[0].type = ;
  // ...

  // BIOS Enhanced Disk Drive Services.
  // (From linux/include/asm-i386/edd.h, 'struct edd_info')
  // Each 'struct edd_info is 78 bytes, times a max of 6 structs in array.
  //bl_memset(params->eddbuf, 0, sizeof(params->eddbuf));
}

  int
strcmp(const char *s1, const char *s2)
{
  unsigned char c1, c2;

  if ( (!s1) || (!s2) )
    panic(); // Bad string(s).

  do {
    c1 = *s1++;
    c2 = *s2++;
    if (c1 < c2) return -1; // S1 < S2
    if (c1 > c2) return  1; // S1 > S2
    } while (c1 && c2);

  return 0; // Nulls reach for both strings; S1 == S2
}

  unsigned
stringToUInt(const char *s)
{
  // Convert a decimal or hexidecimal string to an unsigned int.
  unsigned val;

  if ( !s )
    panic();
  val = 0;
  if ( (s[0]=='0') && (s[1]=='x') ) {
    s += 2; // Skip "0x" leader.
    if ( ! *s )
      panic(); // No hex digits follow?
    while ( 1 ) {
      if ((*s>='0') && (*s<='9')) {
        val = (val<<4) | (*s - '0');
        }
      else if ((*s>='a') && (*s<='f')) {
        val = (val<<4) | (10+(*s - 'a'));
        }
      else if ((*s>='A') && (*s<='F')) {
        val = (val<<4) | (10+(*s - 'A'));
        }
      else
        panic(); // Not a valid hexidecimal digit.
      s++;
      if ( ! *s )
        break; // End of string, done.
      }
    }
  else {
    if ( ! *s )
      panic(); // No digits?
    while ( 1 ) {
      if ((*s>='0') && (*s<='9')) {
        val = (val*10) + (*s - '0');
        }
      else
        panic(); // Not a valid hexidecimal digit.
      s++;
      if ( ! *s )
        break; // End of string, done.
      }
    }
#ifdef DEBUG
  printf("stringToUInt: 0x%08x (%u).\n", val, val);
#endif
  return( val );
}

  void
bl_memset(void *s, int c, unsigned n)
{
  unsigned char *ptr = (unsigned char *) s;

  if ( ! ptr )
    panic();

  while (n) {
    *ptr++ = c;
    n--;
    }
}

  void
bl_memcpy(void *dest, const void *src, unsigned n)
{
  unsigned char *d = (unsigned char *) dest;
  unsigned char *s = (unsigned char *) src;

  if ( (!d) || (!s) )
    panic();

  while (n) {
    *d++ = *s++;
    n--;
    }
}

  void
panic(void)
{
#ifndef DEBUG
  asm volatile ("cli; hlt");
#else
  printf("DEBUG: panic.\n");
#endif
}

  Bit8u
inb( Bit16u port )
{
  Bit8u ret;

  asm volatile("inb (%%dx),%%al"
    : "=a" (ret)
    : "d" (port)
    );
  return(ret);
}

  Bit16u
inw( Bit16u port )
{
  Bit16u ret;

  asm volatile ("inw (%%dx),%%ax"
    :"=a" (ret)
    :"d" (port)
    );
  return(ret);
}

  void
outb( Bit16u port, Bit8u val )
{
  asm volatile ("outb %%al,(%%dx)"
    :
    : "d" (port), "a" (val)
    );
}

  void
outw( Bit16u port, Bit16u val )
{
  asm volatile ("outw %%ax,%%dx"
    :
    :
    "d" (port), "a" (val)
    );
}
