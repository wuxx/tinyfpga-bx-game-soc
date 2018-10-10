#include <stdint.h>
#include <stdbool.h>
#include <uart/uart.h>

// a pointer to this is a null pointer, but the compiler does not
// know that because "sram" is a linker symbol from sections.lds.
extern uint32_t sram;

#define reg_spictrl (*(volatile uint32_t*)0x02000000)
#define reg_uart_clkdiv (*(volatile uint32_t*)0x02000004)
#define reg_buttons  (*(volatile uint32_t*)0x03000000)

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

void main() {
    reg_uart_clkdiv = 139;

    set_irq_mask(0xff);

    // switch to dual IO mode
    reg_spictrl = (reg_spictrl & ~0x007F0000) | 0x00400000;
 
    uint32_t timer = 0;
       
    while (1) {
        timer = timer + 1;

        if ((timer & 0xfff) == 0xfff) {
          print("Buttons: ");
          print_hex(reg_buttons, 2);
          print("\n");
        } 
    } 
}
