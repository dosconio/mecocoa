# TAB LF
SECTIONS
{
	#{TEMP} use 0x10000 to load shell32
	. = 0x22000;
	.text : {*(.text)}
	. = 0x21000;
	.data : {*(.data)}
	.bss : {*(.bss)}
}