MEMORY 
	{
	dram : ORIGIN = 0x40700000,
	       LENGTH = 0x00100000
	}

SECTIONS
{
	.text :
	{
		_stext = . ;
		*(.text)
		*(.rodata)
		_etext = . ;
	} > dram
	.data :
	{
		*(.data)
		_edata = . ;
	} > dram
	.bss :
	{
		*(.bss)
		_end = ALIGN( 0x10 ) ;
	} > dram
}
