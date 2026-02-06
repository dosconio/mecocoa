
#ifndef BOARD_DETAIL
#warning Recommand define BOARD_DETAIL and do init_specific
#endif

using namespace uni;

extern bool init_clock(void);

#define init_specific() StrAppend(_IDN_BOARD, BOARD_DETAIL)

// #include "stm32mp13xx_hal.h"

#undef RCC
#undef PLL1
#undef PLL2
#undef PLL3
#undef PLL4

#undef GPIO


// (No ADVERTISEMENT!) Openedv(R) Board [1o2o3x]
// LED
extern GPIN& LED;
// KEY
//    : ! The Pin also CONNECT the JTAG TCK. So using this is bad while debugging !
extern GPIN& KEY;
// LCD 888
extern GPIN* LCDR[8];
extern byte LCDR_AF[8];
extern GPIN* LCDG[8];
extern byte LCDG_AF[8];
extern GPIN* LCDB[8];
extern byte LCDB_AF[8] ;
extern GPIN& LCD_BL;
extern GPIN& LCD_DE;// AF11
extern GPIN& LCD_CLK;// AF13
extern GPIN& LCD_HSYNC;// AF13
extern GPIN& LCD_VSYNC;// AF11



