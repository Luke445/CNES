#include <inttypes.h>
#include "../memory.h"

int m11_bank_num = 0;
int m11_chr_bank_num = 0;

uint8_t m11_cart_read(uint16_t address) {
	return prg_rom[m11_bank_num*32768 + (address & 0x7FFF)];
}

void m11_cart_write(uint16_t address, uint8_t value) {
	m11_bank_num = value & 0x3;
	m11_chr_bank_num = (value >> 4) & 0xF;
}

uint8_t m11_cart_ppu_read(uint16_t address) {
	return chr_rom[m11_chr_bank_num*8192 + (address & 0x1FFF)];
}
