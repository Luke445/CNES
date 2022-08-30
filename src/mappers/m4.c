#include <inttypes.h>
#include "../memory.h"
#include "../control.h"

enum bank_num_select {
	m4_2k_chr_bank1,
	m4_2k_chr_bank2,

	m4_1k_chr_bank1,
	m4_1k_chr_bank2,
	m4_1k_chr_bank3,
	m4_1k_chr_bank4,

	m4_8k_prg_bank1,
	m4_8k_prg_bank2,
};
int bank_select = 0;

int m4_2k_chr_bank1_num = 0;
int m4_2k_chr_bank2_num = 0;

int m4_1k_chr_bank1_num = 0;
int m4_1k_chr_bank2_num = 0;
int m4_1k_chr_bank3_num = 0;
int m4_1k_chr_bank4_num = 0;

int m4_8k_prg_bank1_num = 0;
int m4_8k_prg_bank2_num = 0;

bool swap_prg_banks, swap_chr_banks = false;

bool was_last_a12_high = false;
int irq_counter = 0;
uint8_t irq_reload_value = 0;
bool reload_irq, irq_enabled = false;

uint8_t m4_cart_read(uint16_t address) {
	switch (address & 0x6000) {
		default:
		case 0x0:
			if (swap_prg_banks)
				return prg_rom[8192*(num_prg_blocks*2 - 2) + (address & 0x1FFF)];
			else
				return prg_rom[8192*m4_8k_prg_bank1_num + (address & 0x1FFF)];
		case 0x2000:
			return prg_rom[8192*m4_8k_prg_bank2_num + (address & 0x1FFF)];
		case 0x4000:
			if (swap_prg_banks)
				return prg_rom[8192*m4_8k_prg_bank1_num + (address & 0x1FFF)];
			else
				return prg_rom[8192*(num_prg_blocks*2 - 2) + (address & 0x1FFF)];
		case 0x6000:
			return prg_rom[8192*(num_prg_blocks*2 - 1) + (address & 0x1FFF)];
	}
}

void m4_cart_write(uint16_t address, uint8_t value) {
	switch (address & 0x6001) {
		case 0x0:
			// maps to the enum
			bank_select = value & 0x7;

			swap_prg_banks = (value & 0x40) ? true : false;
			swap_chr_banks = (value & 0x80) ? true : false;
			break;
		case 0x1:
			switch (bank_select) {
				case m4_2k_chr_bank1:
					m4_2k_chr_bank1_num = (value & (num_chr_blocks*8 - 1)) >> 1;
					break;
				case m4_2k_chr_bank2:
					m4_2k_chr_bank2_num = (value & (num_chr_blocks*8 - 1)) >> 1;
					break;

				case m4_1k_chr_bank1:
					m4_1k_chr_bank1_num = value & (num_chr_blocks*8 - 1);
					break;
				case m4_1k_chr_bank2:
					m4_1k_chr_bank2_num = value & (num_chr_blocks*8 - 1);
					break;
				case m4_1k_chr_bank3:
					m4_1k_chr_bank3_num = value & (num_chr_blocks*8 - 1);
					break;
				case m4_1k_chr_bank4:
					m4_1k_chr_bank4_num = value & (num_chr_blocks*8 - 1);
					break;

				case m4_8k_prg_bank1:
					m4_8k_prg_bank1_num = value & (num_prg_blocks*2 - 1);
					break;
				case m4_8k_prg_bank2:
					m4_8k_prg_bank2_num = value & (num_prg_blocks*2 - 1);
					break;
			}
			break;
		case 0x2000:
			mirroring = (value & 0x1) ? horizontal_mirroring : vertical_mirroring;
			break;
		case 0x2001:
			// ram protect - not implementing bc not used and creates compatability issues
			break;
		case 0x4000:
			irq_reload_value = value;
			break;
		case 0x4001:
			reload_irq = true;
			break;
		case 0x6000:
			irq_enabled = false;
			generate_irq &= ~cart_irq;
			break;
		case 0x6001:
			irq_enabled = true;
			break;
	}
}

void m4_count_scanline() {
	if (irq_counter == 0 || reload_irq) {
		irq_counter = irq_reload_value;
		reload_irq = false;
	}
	else {
		irq_counter--;
		if (irq_counter == 0 && irq_enabled)
			generate_irq |= cart_irq;
	}
}

uint8_t m4_cart_ppu_read(uint16_t address) {
	bool a12 = (address & (1 << 12)) != 0 ? true : false;
	if (a12 && !was_last_a12_high) {
		m4_count_scanline();
	}
	was_last_a12_high = a12;


	if ( (address >= 0x1000 && swap_chr_banks) || (address < 0x1000 && !swap_chr_banks) ) {
		address &= 0xFFF;
		if (address >= 0x800)
			return chr_rom[2048*m4_2k_chr_bank2_num + (address & 0x7FF)];
		else
			return chr_rom[2048*m4_2k_chr_bank1_num + (address & 0x7FF)];
	}
	else {
		switch (address & 0xC00) {
			default:
			case 0x0:
				return chr_rom[1024*m4_1k_chr_bank1_num + (address & 0x3FF)];
			case 0x400:
				return chr_rom[1024*m4_1k_chr_bank2_num + (address & 0x3FF)];
			case 0x800:
				return chr_rom[1024*m4_1k_chr_bank3_num + (address & 0x3FF)];
			case 0xC00:
				return chr_rom[1024*m4_1k_chr_bank4_num + (address & 0x3FF)];
		}
	}
}
