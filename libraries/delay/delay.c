#include <delay/delay.h>

void delay(uint32_t millis) {
  for (uint32_t i = 0; i < (millis << 6); i++) asm volatile ("");
}

