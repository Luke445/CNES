#include <inttypes.h>
#include "../memory.h"

int m3_chr_bank_num = 0;

void m3_cart_write(uint16_t address, uint8_t value) {
	m3_chr_bank_num = value & 0x3;
}

uint8_t m3_cart_ppu_read(uint16_t address) {
	return chr_rom[m3_chr_bank_num * 8192 + (address & 0x1FFF)];
}
