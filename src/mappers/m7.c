#include <inttypes.h>
#include "../memory.h"

int m7_chr_bank_num = 0;

uint8_t m7_cart_read(uint16_t address) {
	return prg_rom[32768*m7_chr_bank_num + (address & 0x7FFF)];
}

void m7_cart_write(uint16_t address, uint8_t value) {
	m7_chr_bank_num = value & (num_prg_blocks/2 - 1);
	mirroring = value & 0x10 ? one_screen_b : one_screen_a;
}
