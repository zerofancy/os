/* ld script to make the Linux/CRIS kernel
 * Authors: Bjorn Wesen (bjornw@axis.com)
 *
 * For now, on Etrax-100 LX, the DRAM starts virtually at 0x6. Normally
 * it should be at 0xc.
 */	

SECTIONS
{
	. = 0x60000000;               /* DRAM starts virtually at 0x60000000 */
	_dram_start = .;
	_ibr_start = .;
	. = . + 0x4000; /* see head.S and pages reserved at the start */

	_text = .;                    /* Text and read-only data */
	_text_start = .;              /* lots of aliases */
	_stext = .;
	__stext = .;
	.text : {
		*(.text)
		*(.fixup)
		*(.text.__*)
		*(.rodata)
	}

	. = ALIGN(4);                /* Exception table */
  	__start___ex_table = .;
  	__ex_table : { *(__ex_table) }
  	__stop___ex_table = .;

	_etext = . ;                  /* End of text section */ 
	__etext = .;

	. = ALIGN (4);
	___data_rom_start = . ;
	___data_start = . ;
	__Sdata = . ;
	.data : {                     /* Data */
		*(.data)
	}
	__edata = . ;                 /* End of data section */
	_edata = . ;

	. = ALIGN(8192);              /* init_task and stack, must be aligned */
  	.data.init_task : { *(.data.init_task) }

  	. = ALIGN(8192);              /* Init code and data */
  	___init_begin = .;
  	.text.init : { *(.text.init) }
  	.data.init : { *(.data.init) }
  	. = ALIGN(16);
  	___setup_start = .;
  	.setup.init : { *(.setup.init) }
  	___setup_end = .;
  	___initcall_start = .;
  	.initcall.init : { *(.initcall.init) }
  	___initcall_end = .;
	__vmlinux_end = .;            /* last address of the physical file */
  	. = ALIGN(8192);
  	___init_end = .;

	__data_end = . ;              /* Move to _edata ? */
	__bss_start = .;              /* BSS */
	.bss : {
		*(COMMON)
		*(.bss)
	}

	. =  ALIGN (0x20);
	_end = .;
	__end = .;

	/* Sections to be discarded */
  	/DISCARD/ : {
        	*(.text.exit)
        	*(.data.exit)
		*(.exitcall.exit)
        }

	_dram_end = 0x60000000 + @ETRAX_DRAM_SIZE_M@*1024*1024;
}
