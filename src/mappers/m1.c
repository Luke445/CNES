#include <inttypes.h>
#include "../memory.h"

int m1_bank_num = 0;
int m1_chr_bank1_num = 0;
int m1_chr_bank2_num = 0;
int shift_reg = 0x20; // slightly modified to make coding easier
bool chr_4k_banks = false;

enum bank_mode {
	double_bank,
	fixed_16_start,
	fixed_16_end
};

int m1_banking_mode = fixed_16_end;

uint8_t m1_cart_read(uint16_t address) {
	if (m1_banking_mode == double_bank) {
		return prg_rom[32768*(m1_bank_num >> 1) + (address & 0x7FFF)];
	}
	else if (m1_banking_mode == fixed_16_start) {
		if (address <= 0xBFFF)
			return prg_rom[address & 0x3FFF]; // fixed to first bank
		else
			return prg_rom[16384*m1_bank_num + (address & 0x3FFF)];
	}
	else { // fixed_16_end
		if (address >= 0xC000)
			return prg_rom[16384*(num_prg_blocks - 1) + (address & 0x3FFF)]; // fixed to last bank
		else
			return prg_rom[16384*m1_bank_num + (address & 0x3FFF)];
	}
}

void m1_cart_write(uint16_t address, uint8_t value) {
	if (value & 0x80)
		shift_reg = 0x20;
	else {
		shift_reg >>= 1;
		shift_reg = (shift_reg & 0x1F) | ((value & 0x1) << 5);
		if (shift_reg & 0x1) {
			shift_reg >>= 1;

			switch (address & 0x6000) {
				case 0x0: // control
					switch (shift_reg & 0x3) {
						case 0:
							mirroring = one_screen_a;
							break;
						case 1:
							mirroring = one_screen_b;
							break;
						case 2:
							mirroring = vertical_mirroring;
							break;
						case 3:
							mirroring = horizontal_mirroring;
							break;	
					}
					switch ((shift_reg >> 2) & 0x3) {
						case 0:
						case 1:
							m1_banking_mode = double_bank;
							break;
						case 2:
							m1_banking_mode = fixed_16_start;
							break;
						case 3:
							m1_banking_mode = fixed_16_end;
							break;
					}
					chr_4k_banks = shift_reg & 0x10 ? true : false;
					break;
				case 0x2000: // chr bank 0
					m1_chr_bank1_num = shift_reg & 0x1F;
					if (m1_chr_bank1_num > num_chr_blocks*2)
						m1_chr_bank1_num = num_chr_blocks - 1;
					break;
				case 0x4000: // chr bank 1
					m1_chr_bank2_num = shift_reg & 0x1F;
					if (m1_chr_bank2_num > num_chr_blocks*2)
						m1_chr_bank2_num = num_chr_blocks - 1;
					break;
				case 0x6000: // prg bank
					m1_bank_num = shift_reg & 0x1F;
					if (m1_bank_num > num_prg_blocks)
						m1_bank_num = num_prg_blocks - 1;
					break;
			}

			shift_reg = 0x20;
		}
	}
}

uint8_t m1_cart_ppu_read(uint16_t address) {
	if (chr_4k_banks) {
		if (address <= 0xFFF)
			return chr_rom[4096*m1_chr_bank1_num + (address & 0xFFF)];
		else
			return chr_rom[4096*m1_chr_bank2_num + (address & 0xFFF)];
	}
	else {
		return chr_rom[4096*(m1_chr_bank1_num & 0x1E) + address];
	}
}






