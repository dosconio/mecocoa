// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240220
// AllAuthor: @dosconio
// ModuTitle: RTC 0x70 - PIC 0xA0 
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/interrupt.h"
#include "../../include/RTC.h"

void RTC_Init()
{
	outpb(PORT_RTC, 0x8B);// mgk: RTC Register B and NMI
	outpb(PORT_RTC|1, 0x12);// mgk: Set Reg-B {Ban Periodic, Open Update-Int, BCD, 24h}
	outpb(_i8259A_SLV_IMR|1, innpb(_i8259A_SLV_IMR) & 0xFE);// Slave 0 is linked with RTC
	outpb(PORT_RTC, 0x0C);// mgk: ?
	innpb(PORT_RTC|1);// Read Reg-C, reset pending interrupt
	//{TODO} Check PIC Device?
}
