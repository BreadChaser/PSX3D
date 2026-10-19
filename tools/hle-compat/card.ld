ENTRY(_start)
SECTIONS { . = 0x80010000; .text : { *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.MIPS*) *(.reginfo) *(.pdr) *(.comment) *(.note*) } }
