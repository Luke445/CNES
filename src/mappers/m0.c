#include <inttypes.h>
#include "../memory.h"


uint8_t m0_cart_read(uint16_t address) {
	if (num_prg_blocks == 1)
		return prg_rom[address & 0x3FFF];
	else
		return prg_rom[address & 0x7FFF];
}

void m0_cart_write(uint16_t address, uint8_t value) {
}

uint8_t m0_cart_ppu_read(uint16_t address) {	
	return chr_rom[address];
}

void m0_cart_ppu_write(uint16_t address, uint8_t value) {
}
