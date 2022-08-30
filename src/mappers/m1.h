#include <inttypes.h>

uint8_t m1_cart_read(uint16_t address);
void m1_cart_write(uint16_t address, uint8_t value);
uint8_t m1_cart_ppu_read(uint16_t address);
