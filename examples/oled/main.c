#include <stdint.h>
#include <stdbool.h>
#include <uart/uart.h>
#include "font.h"

// a pointer to this is a null pointer, but the compiler does not
// know that because "sram" is a linker symbol from sections.lds.
extern uint32_t sram;

#define reg_spictrl (*(volatile uint32_t*)0x02000000)
#define reg_uart_clkdiv (*(volatile uint32_t*)0x02000004)
#define reg_leds (*(volatile uint32_t*)0x03000000)
#define reg_buttons (*(volatile uint32_t*)0x03000004)
#define reg_cs (*(volatile uint32_t*)0x05000004)
#define reg_dc (*(volatile uint32_t*)0x05000010)
#define reg_rst (*(volatile uint32_t*)0x05000014)
#define reg_xfer (*(volatile uint32_t*)0x05000008)
#define reg_prescale (*(volatile uint32_t*)0x05000000)
#define reg_mode (*(volatile uint32_t*)0x0500000c)

#define WIDTH 128
#define HEIGHT 128

#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }

#define abs(x) (x < 0 ? -x : x)

uint32_t set_irq_mask(uint32_t mask); asm (
    ".global set_irq_mask\n"
    "set_irq_mask:\n"
    ".word 0x0605650b\n"
    "ret\n"
);

void irq_handler(uint32_t irqs, uint32_t* regs) { }

void delay(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) asm volatile ("");
}

void reset() {
	reg_rst = 1;
        delay(20000);
	reg_rst = 0;
        delay(200000);
	reg_rst = 1;
}

void send_cmd(uint8_t r) {
	reg_cs = 1;
	reg_dc = 0;
	reg_cs = 0;
	reg_xfer = r;
	reg_cs = 1;
}

void send_data(uint8_t d) {
	reg_cs = 1;
	reg_dc = 1;
	reg_cs = 0;
	reg_xfer = d;
	reg_cs = 1;
}

void drawPixel(int16_t x, int16_t y, uint16_t color) {
        if((x < 0) || (y < 0) || (x >= WIDTH) || (y >= HEIGHT)) return;
	
	send_cmd(0x15);
	send_data(x);
	send_data(WIDTH-1);

	send_cmd(0x75);
	send_data(y);
	send_data(HEIGHT-1);

	send_cmd(0x5C);

	send_data(color >> 8);
	send_data(color);
}

void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
        uint16_t color) {
    int16_t steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) {
        _swap_int16_t(x0, y0);
        _swap_int16_t(x1, y1);
    }

    if (x0 > x1) {
        _swap_int16_t(x0, x1);
        _swap_int16_t(y0, y1);
    }

    int16_t dx, dy;
    dx = x1 - x0;
    dy = abs(y1 - y0);

    int16_t err = dx / 2;
    int16_t ystep;

    if (y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }

    for (; x0<=x1; x0++) {
        if (steep) {
            drawPixel(y0, x0, color);
        } else {
            drawPixel(x0, y0, color);
        }
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

void drawFastVLine(int16_t x, int16_t y,
        int16_t h, uint16_t color) {
    drawLine(x, y, x, y+h-1, color);
}

void drawFastHLine(int16_t x, int16_t y,
        int16_t w, uint16_t color) {
    drawLine(x, y, x+w-1, y, color);
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
        uint16_t color) {
    for (int16_t i=x; i<x+w; i++) {
        drawFastVLine(i, y, h, color);
    }
}

void fillScreen(uint16_t color) {
    fillRect(0, 0, WIDTH, HEIGHT, color);
}

void drawChar(int16_t x, int16_t y, unsigned char c,
  uint16_t color) {

    if((x >= WIDTH)            || // Clip right
       (y >= HEIGHT)           || // Clip bottom
       ((x + 6 - 1) < 0) || // Clip left
       ((y + 8 - 1) < 0))   // Clip top
        return;

    for(int8_t i=0; i<5; i++ ) { // Char bitmap = 5 columns
        uint8_t line = font[c * 5 + i];
        for(int8_t j=0; j<8; j++, line >>= 1) {
            if(line & 1) {
                drawPixel(x+i, y+j, color);
            } else {
                drawPixel(x+i, y+j, 0);
            }
        }
    }
}

void drawText(int16_t x, int16_t y, const char *text, int16_t c) {
    for(int i=0;text[i];i++) {
        drawChar(x + i*6,y,text[i],c);
    }
}

void main() {
    reg_uart_clkdiv = 139;

    set_irq_mask(0xff);

    // switch to dual IO mode
    reg_spictrl = (reg_spictrl & ~0x007F0000) | 0x00400000;
 
    uint32_t timer = 0;

    print("Initialising\n");      
    reg_cs = 1;
    reg_rst = 1;
    reg_prescale = 0; // 16Mhz
    reg_mode = 0;

    print("Reset\n");
    send_cmd(0xFD); // Command lock
    send_data(0x12); // 
    send_cmd(0xFD); // Command lock
    send_data(0xB1); // 
    send_cmd(0xAE); // Display off
    send_cmd(0xB3); // Set clock div
    send_cmd(0xF1); // 
    send_cmd(0xCA); // Mux ratio
    send_data(0x7F); // 
    send_cmd(0xA0); // Set remap
    send_data(0x74); // RGB
    send_cmd(0x15);
    send_data(0x00);
    send_data(0x7F);
    send_cmd(0x75);
    send_data(0x00);
    send_data(0x7F);
    send_cmd(0xA1); // Startline
    send_data(0x00); // 0
    send_cmd(0xA2); // Display offset
    send_data(0x00); // 0
    send_cmd(0xB5); // Set GPIO
    send_data(0x00); // 0
    send_cmd(0xAB); // Funcion select
    send_data(0x01); // internal diode drop
    send_cmd(0xB1); // Precharge
    send_cmd(0x32); // 
    send_cmd(0xBE); // Vcomh
    send_cmd(0x05); // 
    send_cmd(0xA6); // Normal display
    send_cmd(0xC1); // Set Contrast ABC
    send_data(0xC8); // 
    send_data(0x80); // 
    send_data(0xC8); // 
    send_cmd(0xC7); // Set Contrast Master
    send_data(0x0F); // 
    send_cmd(0xB4); // Set VSL
    send_data(0xA0); // 
    send_data(0xB5); // 
    send_data(0x55); // 
    send_cmd(0x86); // Precharge 2
    send_data(0x01); // 
    send_cmd(0xAF); // Switch on

    print("Initialisation done\n");

    fillScreen(0);

    int x = 0;
    while (1) {
        timer = timer + 1;
	if ((timer & 0xfff) == 0) {
            //print("Oled display\n");
            drawText(20,20,"Hello World!", 0xffff);
            //drawPixel(64, 64, 0xffff);
        }
    } 
}
