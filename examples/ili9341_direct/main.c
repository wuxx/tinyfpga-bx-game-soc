#include <stdint.h>
#include <stdbool.h>
#include <uart/uart.h>
#include "font.h"
#include <button/button.h>

// a pointer to this is a null pointer, but the compiler does not
// know that because "sram" is a linker symbol from sections.lds.
extern uint32_t sram;

#define reg_spictrl (*(volatile uint32_t*)0x02000000)
#define reg_uart_clkdiv (*(volatile uint32_t*)0x02000004)
#define reg_leds (*(volatile uint32_t*)0x03000000)

#define reg_cs (*(volatile uint32_t*)0x05000004)
#define reg_dc (*(volatile uint32_t*)0x05000008)
#define reg_rst (*(volatile uint32_t*)0x0500000c)
#define reg_xfer (*(volatile uint32_t*)0x05000000)

#define WIDTH 320
#define HEIGHT 240

#define ILI9341_SOFTRESET          0x01
#define ILI9341_SLEEPIN            0x10
#define ILI9341_SLEEPOUT           0x11
#define ILI9341_NORMALDISP         0x13
#define ILI9341_INVERTOFF          0x20
#define ILI9341_INVERTON           0x21
#define ILI9341_GAMMASET           0x26
#define ILI9341_DISPLAYOFF         0x28
#define ILI9341_DISPLAYON          0x29
#define ILI9341_COLADDRSET         0x2A
#define ILI9341_PAGEADDRSET        0x2B
#define ILI9341_MEMORYWRITE        0x2C
#define ILI9341_PIXELFORMAT        0x3A
#define ILI9341_FRAMECONTROL       0xB1
#define ILI9341_DISPLAYFUNC        0xB6
#define ILI9341_ENTRYMODE          0xB7
#define ILI9341_POWERCONTROL1      0xC0
#define ILI9341_POWERCONTROL2      0xC1
#define ILI9341_VCOMCONTROL1       0xC5
#define ILI9341_VCOMCONTROL2       0xC7
#define ILI9341_MEMCONTROL         0x36
#define ILI9341_MADCTL             0x36

#define ILI9341_MADCTL_MY  0x80
#define ILI9341_MADCTL_MX  0x40
#define ILI9341_MADCTL_MV  0x20
#define ILI9341_MADCTL_ML  0x10
#define ILI9341_MADCTL_RGB 0x00
#define ILI9341_MADCTL_BGR 0x08
#define ILI9341_MADCTL_MH  0x04

uint32_t set_irq_mask(uint32_t mask); asm (
    ".global set_irq_mask\n"
    "set_irq_mask:\n"
    ".word 0x0605650b\n"
    "ret\n"
);

void irq_handler(uint32_t irqs, uint32_t* regs) { }

void delay(uint32_t n) {
  for (uint32_t i = 0; i < (n << 6); i++) asm volatile ("");
}

void send_cmd(uint8_t r) {
        //reg_cs = 0;
	reg_dc = 0;
	reg_xfer = r;
        //reg_cs = 1; // Don't know why this makes things more unreliable
}

void send_data(uint8_t d) {
        //reg_cs = 0;
	reg_dc = 1;
	reg_xfer = d;
        //reg_cs = 1;
}

void reset() {
        reg_cs = 1;
	reg_rst = 0;
        delay(2);
	reg_rst = 1;
        reg_cs = 0;
        for(int i=0;i<3;i++) reg_xfer = 0x00;
        //reg_cs = 1;
}

void init() {
	reset();

        delay(200);

        send_cmd(ILI9341_SOFTRESET);

        delay(50);

        send_cmd(ILI9341_DISPLAYOFF);

	send_cmd(ILI9341_POWERCONTROL1);
	send_data(0x23);

	send_cmd(ILI9341_POWERCONTROL2);
	send_data(0x10);

	send_cmd(ILI9341_VCOMCONTROL1); 
	send_data(0x2B); 
	send_data(0x2B); 

	send_cmd(ILI9341_VCOMCONTROL2); 
	send_data(0xC0); 

	send_cmd(ILI9341_MEMCONTROL); 
	send_data(ILI9341_MADCTL_MV | ILI9341_MADCTL_MX | ILI9341_MADCTL_MY | ILI9341_MADCTL_BGR);

	send_cmd(ILI9341_PIXELFORMAT); 
	send_data(0x55);

	send_cmd(ILI9341_FRAMECONTROL); 
	send_data(0x00); 
	send_data(0x1B);
 
	send_cmd(ILI9341_ENTRYMODE); 
	send_data(0x07); 

        send_cmd(ILI9341_SLEEPOUT);

        delay(150);

	send_cmd(ILI9341_DISPLAYON); 

        delay(500);
}

void set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
        send_cmd(ILI9341_COLADDRSET);
        send_data(x1 >> 8);
        send_data(x1);
        send_data(x2 >> 8);
        send_data(x2);

        send_cmd(ILI9341_PAGEADDRSET);
        send_data(y1 >> 8);
        send_data(y1);
        send_data(y2 >> 8);
        send_data(y2);
}

void clear(uint16_t c, int s, int w) {
        set_window(s, 0, s+w-1, HEIGHT-1);
        send_cmd(ILI9341_MEMORYWRITE);
        uint8_t c1 = c >> 8;
        uint8_t c2 = c;

        reg_dc = 1;
        for(int i=0; i< HEIGHT*w; i++) {
        	reg_xfer = c1;
        	reg_xfer = c2;
	}
}

void clearScreen(uint16_t c) {
  clear(c, 0, 320); // Sometimes only draws 64 pixels wide
  clear(c, 64, 256);
}

void drawPixel(int16_t x, int16_t y, uint16_t color) {
        if((x < 0) || (y < 0) || (x >= WIDTH) || (y >= HEIGHT)) return;
        
	set_window(x, y, WIDTH-1, HEIGHT-1);	
	send_cmd(ILI9341_MEMORYWRITE);

	send_data(color >> 8);
	send_data(color);
}

void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bc) {
    if((x >= WIDTH)      || // Clip right
       (y >= HEIGHT)     || // Clip bottom
       ((x + 6 - 1) < 0) || // Clip left
       ((y + 8 - 1) < 0))   // Clip top
        return;

    for(int8_t i=0; i<5; i++ ) { // Char bitmap = 5 columns
        uint8_t line = font[c * 5 + i];
        for(int8_t j=0; j<8; j++, line >>= 1) 
            drawPixel(x+i, y+j, line & 1 ? color : bc);
    }
}

void drawText(int16_t x, int16_t y, const char *text, uint16_t c, uint16_t bc) {
    for(int i=0;text[i];i++) drawChar(x + i*6, y,text[i], c, bc);
}

void main() {
    reg_uart_clkdiv = 139;

    set_irq_mask(0xff);

    // switch to dual IO mode
    reg_spictrl = (reg_spictrl & ~0x007F0000) | 0x00400000;

    init();

    clearScreen(0x6E5D);

    char *games[] = {"Pac Man", "Tetris", "Frogger", "Super Marion Bros"};
    int num_games = 4;

    drawText(80,40,"Choose a game :", 0x00A0, 0x6E5D);

    for(int i=0;i<num_games;i++) 
      drawText(92, 80 + i*20, games[i], 0xD0B7, 0x6E5D);

    drawText(80, 80, "* ",  0xD0B7, 0x6E5D);

    int index = 0, old_index;
    uint8_t buttons = 0, old_buttons;
 
    while (1) {
      old_buttons = buttons;
      buttons = reg_buttons;
      
      old_index = index;

      if ((buttons & BUTTON_B))  
        if (++index == num_games) index = 0;
  
      if (index != old_index) {
        drawText(80, 80 + (old_index*20), "  ", 0xD0B7, 0x6E5D);
        drawText(80, 80 + (index*20), "* ", 0xD0B7, 0x6E5D);
      }

      delay(5);
    } 
}
