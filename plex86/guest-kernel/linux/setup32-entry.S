/////////////////////////////////////////////////////////////////////////
//// $Id$
/////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2003  Kevin P. Lawton

#if 0
Notes: (Fixme)
  // Leave zeroed:
  //   LDTR
  //   TR
  //   IDTR
  //   data segments
  //   DRx
  //   TRx
  //   CR[1..4]
#endif

#define KernelCodeDesc .long 0x0000ffff ; .long 0x00cf9a00
#define KernelDataDesc .long 0x0000ffff ; .long 0x00cf9200
#define UserCodeDesc   .long 0x0000ffff ; .long 0x00cffa00
#define UserDataDesc   .long 0x0000ffff ; .long 0x00cff200
#define NullDesc       .long 0x00000000 ; .long 0x00000000

.text
.type  _start,@function
.globl _start
_start:

.globl kernelSetupArea // Linux setup area
kernelSetupArea:
  .fill 2048, 1, 0

.globl kernelCommandLine // Linux command line
kernelCommandLine:
  .fill 2048, 1, 0

.globl bootloaderGDT // Bootloader GDT
bootloaderGDT:
  //                  2.4.20      2.5.59
  // LinuxBootCsSlot     2           2
  // LinuxBootDsSlot     3           3
  // LinuxKernelCsSlot   2          12
  // LinuxKernelDsSlot   3          13
  // LinuxUserCsSlot     4          14
  // LinuxUserDsSlot     5          15

  NullDesc       // [0]
  NullDesc       // [1]
  KernelCodeDesc // [2]
  KernelDataDesc // [3]
  UserCodeDesc   // [4]
  UserDataDesc   // [5]
  NullDesc       // [6]
  NullDesc       // [7]
  NullDesc       // [8]
  NullDesc       // [9]
  NullDesc       // [10]
  NullDesc       // [11]
  KernelCodeDesc // [12]
  KernelDataDesc // [13]
  UserCodeDesc   // [14]
  UserDataDesc   // [15]
  .fill 1920, 1, 0 // Remainder of 2k area unused (2k-16*8)

.globl bootloaderCommandLine // Bootloader command line
bootloaderCommandLine:
  .fill 2048, 1, 0

.globl bootloaderStack // Bootloader stack
bootloaderStack:
  .fill 4096, 1, 0

bootloaderStart:
  cs
    lgdtl (loadGDT)
  movw $0x18, %dx
  movw %dx, %ds
  movw %dx, %es
  movw %dx, %ss
  movl $bootloaderStack+4096, %esp // Fixme: use +BootloaderStackSize
  pushl $2 // EFLAGS.IF=0 (reserved bit=1)
  popfl
  movl  $0x00000033, %eax // CR0.PE=1
  movl  %eax, %cr0
  xorl %ebp,%ebp // Clear the stack frame pointer
  call  bootloaderMain
