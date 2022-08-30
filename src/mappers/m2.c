#include <inttypes.h>
#include "../memory.h"

int m2_bank_num = 0;

uint8_t m2_cart_read(uint16_t address) {
	if (address >= 0xC000) {
		return prg_rom[16384*(num_prg_blocks - 1) + (address & 0x3FFF)]; // fixed to last bank
	}
	else {
		return prg_rom[16384*m2_bank_num + (address & 0x3FFF)];
	}
}

void m2_cart_write(uint16_t address, uint8_t value) {
	m2_bank_num = value & 0x7;
}
