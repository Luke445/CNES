#include <inttypes.h>

uint8_t m0_cart_read(uint16_t address);
void m0_cart_write(uint16_t address, uint8_t value);
uint8_t m0_cart_ppu_read(uint16_t address);
void m0_cart_ppu_write(uint16_t address, uint8_t value);
