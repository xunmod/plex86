/////////////////////////////////////////////////////////////////////////
//// $Id$
/////////////////////////////////////////////////////////////////////////
////
////  Copyright (C) 2004 Kevin P. Lawton


#define BootCodeDesc .long 0x0000ffff ; .long 0x00cf9b00
#define BootDataDesc .long 0x0000ffff ; .long 0x00cf9300
#define NullDesc     .long 0x00000000 ; .long 0x00000000

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
  NullDesc     // [0]
  NullDesc     // [1]
  BootCodeDesc // [2]
  BootDataDesc // [3]
  .fill 2016, 1, 0 // Remainder of 2k area unused (2k-4*8)

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
