#include <inttypes.h>

uint8_t m4_cart_read(uint16_t address);
void m4_cart_write(uint16_t address, uint8_t value);
uint8_t m4_cart_ppu_read(uint16_t address);
void m4_count_scanline();
