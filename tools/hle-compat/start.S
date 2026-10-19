.set noreorder
.text
.globl _start
_start:
 lui $sp,0x8020
 addiu $sp,$sp,-16
 jal main
 nop
.globl bios_a0
bios_a0:
 move $t1,$a0
 move $a0,$a1
 move $a1,$a2
 move $a2,$a3
 j 0x800000a0
 nop
.globl bios_b0
bios_b0:
 move $t1,$a0
 move $a0,$a1
 move $a1,$a2
 move $a2,$a3
 j 0x800000b0
 nop
