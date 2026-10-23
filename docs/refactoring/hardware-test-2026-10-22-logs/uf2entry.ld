MEMORY { FLASH (rx) : ORIGIN = 0x26000, LENGTH = 4K }
SECTIONS { .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > FLASH }
