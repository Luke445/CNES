#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "memory.h"
#include "ppu.h"
#include "apu.h"
#include "control.h"

#include "mappers/m0.h"
#include "mappers/m1.h"
#include "mappers/m2.h"
#include "mappers/m3.h"
#include "mappers/m4.h"
#include "mappers/m7.h"
#include "mappers/m11.h"

char NES_FILE_HEADER[4] = "NES\x1A";

bool has_save_file = false;
FILE *save_file;

int flags1;
bool has_battery;
// iNES 1.0 always implies 8k of prg ram
bool has_wram = true;
int wram_size = 8192;
bool has_trainer;
bool ignore_mirror;

int flags2;
bool unisystem;
bool playchoice;
bool NES2;

int mapper;

char *trainer;

char *wram;

char internal_ram[2048] = {0};

char vram[2048] = {0};

uint8_t ppu_read_buffer = 0;
bool ppu_reg_write_latch = false;
uint8_t oam_address = 0;
uint8_t ppu_bus_data = 0;
uint8_t cpu_bus_data = 0;

int vram_inc = 1;

typedef uint8_t (*read_func)(uint16_t);
typedef void (*write_func)(uint16_t, uint8_t);

read_func cart_read_func;
write_func cart_write_func;
read_func cart_ppu_read_func;
write_func cart_ppu_write_func;


void init_memory(FILE *rom_file) {
	uint8_t header[16];
	fread(header, 16, 1, rom_file);

	if (memcmp(header, NES_FILE_HEADER, 4)) {
		printf("File type not reconized\n");
		exit(0);
	}

	num_prg_blocks = header[4];
	num_chr_blocks = header[5];

	flags1 = header[6];
	mirroring = flags1 & 0x1 ? vertical_mirroring : horizontal_mirroring;
	has_battery = (flags1 & 0x2) >> 1;
	printf("battery: %d\n", has_battery);
	has_trainer = (flags1 & 0x4) >> 2;
	printf("trainer: %d\n", has_trainer);
	ignore_mirror = (flags1 & 0x8) >> 3;
	printf("4-screen mirroring: %d\n", ignore_mirror);

	flags2 = header[7];
	unisystem = flags2 & 0x1;
	playchoice = (flags2 & 0x2) >> 1;
	if ((flags2 & 0x0C) == 0x08) { // iNES 2.0
		printf("iNES 2.0\n");
		/*
		has_wram = false;
		int prg_ram_size = header[10];
		if ((prg_ram_size & 0xF) != 0) { // volitile ram
			has_wram = true;
			wram_size = 64 << (prg_ram_size & 0xF);
		}
		if (((prg_ram_size & 0xF0) != 0)) { // non-volitile ram
			has_wram = has_battery = true;
			wram_size = 64 << (prg_ram_size & 0xF0);
		}*/
	}
	else {
		if (header[8] != 0)
			wram_size = 8192 * header[8];
	}

	mapper = ((flags1 & 0xF0) >> 4) | (flags2 & 0xF0);

	printf("mapper: %d\n", mapper);

	if (has_trainer) {
		trainer = (char *) malloc(512);
		fread(trainer, 512, 1, rom_file);
	}

	if (has_wram) {
		if (wram_size != 8192)
			printf("weird wram size\n");
		wram = (char *) malloc(wram_size);
		if (has_save_file) {
			fread(wram, wram_size, 1, save_file);
		}
		else
			memset(wram, 0, wram_size);
	}

	printf("num_prg_blocks: %d\n", num_prg_blocks);
	prg_rom = (char *) malloc(num_prg_blocks * 16384);
	fread(prg_rom, num_prg_blocks * 16384, 1, rom_file);

	printf("num_chr_blocks: %d\n", num_chr_blocks);
	if (num_chr_blocks >= 1) {
		chr_rom = (char *) malloc(num_chr_blocks * 8192);
		fread(chr_rom, num_chr_blocks * 8192, 1, rom_file);
		chr_ram = false;
	}
	else {
		chr_rom = (char *) malloc(8192);
		chr_ram = true;
	}

	// default
	cart_read_func = m0_cart_read;
	cart_write_func = m0_cart_write;
	cart_ppu_read_func = m0_cart_ppu_read;
	cart_ppu_write_func = m0_cart_ppu_write; // unimplemented
	switch (mapper) {
		case 0:
			break;
		case 1:
			cart_read_func = m1_cart_read;
			cart_write_func = m1_cart_write;
			cart_ppu_read_func = m1_cart_ppu_read;
			break;
		case 2:
			cart_read_func = m2_cart_read;
			cart_write_func = m2_cart_write;
			break;
		case 3:
			cart_write_func = m3_cart_write;
			cart_ppu_read_func = m3_cart_ppu_read;
			break;
		case 4:
			cart_read_func = m4_cart_read;
			cart_write_func = m4_cart_write;
			cart_ppu_read_func = m4_cart_ppu_read;
			break;
		case 7:
			cart_read_func = m7_cart_read;
			cart_write_func = m7_cart_write;
			break;
		case 11:
			cart_read_func = m11_cart_read;
			cart_write_func = m11_cart_write;
			cart_ppu_read_func = m11_cart_ppu_read;
			break;
		default:
			printf("unsupported mapper\n");
			exit(0);
	}

	memset(palette_ram, 0, 32);
	base_nametable = 0;
	vblank_nmi = false;

	printf("mirroring: %d\n", mirroring);

	ppu_address = 0;
	printf("finished mem init\n");
}

void load_save_file(FILE *s) {
	save_file = s;
	has_save_file = true;
}

void close_memory() {
	if (has_battery) {
		if (has_save_file) {
			fseek(save_file, 0, SEEK_SET);
			fwrite(wram, 1, wram_size, save_file);
		}
	}
}

uint8_t peek(uint16_t address) {
	if (address >= 0x8000) {
		return cart_read_func(address);
	}
	else if (address >= 0x6000 && address <= 0x7FFF) {
		if (has_wram)
			return wram[address & 0x1FFF];
	}
	else if (address >= 0x4000 && address <= 0x40FF) {
		// NES APU and I/O registers
		switch (address & 0x1F) {
			case 0x15: // APU status
				early_update_apu();
				cpu_bus_data = get_apu_status();
				return cpu_bus_data;
			case 0x16: // controller 1
				cpu_bus_data = get_controller1() | (cpu_bus_data & 0xE0);
				return cpu_bus_data;
			case 0x17: // controller 2 (not mapped to buttons)
				cpu_bus_data = get_controller2() | (cpu_bus_data & 0xE0);
				return cpu_bus_data;
			default:
				return cpu_bus_data;
		}
	}
	else if (address & 0x2000) {
		early_update_ppu();
		uint8_t value, tmp;
		switch (address & 0x7) {
			case 0:  // PPUCTRL (unused)
			case 1:  // PPUMASK (unused)
			case 3:  // OAMADDR (unused)
			case 5:  // PPUSCROLL (unused)
			case 6:  // PPUADDR (unused)
				return ppu_bus_data;

			case 2:  // PPUSTATUS
				ppu_reg_write_latch = false;
				tmp = (get_ppu_status() & 0xE0) | (ppu_bus_data & 0x1F);
				ppu_bus_data = tmp;
				//printf("PPUCTRL READ: %x %d\n", value, master_clock.cycles);
				return tmp;
			case 4:  // OAMDATA
				tmp = oam_data[oam_address];
				ppu_bus_data = tmp;
				return tmp;
			case 7:  // PPUDATA
				value = ppu_peek(ppu_address);
				if (ppu_address <= 0x3EFF) {
					tmp = ppu_read_buffer;
					ppu_read_buffer = value;
				}
				else {
					ppu_read_buffer = vram[ppu_address & 0x7FF]; // this needs to account for vram mirroring
					tmp = value;
				}
				ppu_address += vram_inc;
				ppu_bus_data = tmp;
				return tmp;
		}
		
	}
	else if (address < 0x2000) {
		return internal_ram[address & 0x7FF];
	}

	return 0;
}

uint16_t peek2(uint16_t address) {
	return peek(address) | (peek(address + 1) << 8);
}

void poke(uint16_t address, uint8_t value) {
	cpu_bus_data = value;
	if (address >= 0x8000) {
		cart_write_func(address, value);
	}
	else if (address >= 0x6000 && address <= 0x7FFF) {
		if (has_wram)
			wram[address & 0x1FFF] = value;
	}
	else if (address >= 0x4000) {
		// NES APU and I/O registers
		address &= 0x1F;
		if (address == 0x14) {
			uint16_t base_copy_addr = value << 8;
			if (base_copy_addr == 0)
				for (int i = 0; i < 256; i++)
					oam_data[i] = peek(base_copy_addr + i);
			else {
				// simulate page wrap around for offset dma copy
				for (int i = 0; i < 256; i++)
					oam_data[(i + oam_address) & 0xFF] = peek(base_copy_addr + i);
			}

			// TODO: check this
			if ((master_clock.ppu_cycles % 2) == 1) {// extra cycle on odd cycles
				master_clock.ppu_cycles += 1 * 3;
				master_clock.cpu_cycles += 1;
			}
			master_clock.ppu_cycles += 513 * 3;
			master_clock.cpu_cycles += 513;
		}
		else if (address == 0x16)
			set_controller(value & 1);
		else
			apu_poke(address, value);
	}
	else if (address & 0x2000) {
		ppu_bus_data = value;
		early_update_ppu();
		switch (address & 0x7) {
			case 0:  // PPUCTRL
				base_nametable = value & 0x3;
				ppu_address_tmp = (ppu_address_tmp & 0x73FF) | ((uint16_t)base_nametable << 10);

				vram_inc = (value & 0x4) ? 32 : 1;
				sprite_table_addr = (value & 0x8) ? 0x1000 : 0;
				background_table_addr = (value & 0x10) ? 0x1000 : 0;
				double_height_sprites = (value & 0x20) ? true : false;

				vblank_nmi = (value & 0x80) ? true : false;
				if (!vblank_nmi) {
					nmi_occured = false;
					// disabling nmi
				}
				//printf("PPUCTRL WRITE: %d %d\n", base_nametable, master_clock.cycles);
				break;
			case 1:  // PPUMASK
				left_clip_background = (value & 0x2) ? false : true;
				left_clip_sprites = (value & 0x4) ? false : true;
				show_background = (value & 0x8) ? true : false;
				show_sprites = (value & 0x10) ? true : false;
				break;
			case 2:  // PPUSTATUS (unused)
				break;
			case 3:  // OAMADDR
				oam_address = value;
				break;
			case 4:  // OAMDATA
				oam_data[oam_address++] = value;
				break;
			case 5:  // PPUSCROLL
				if (ppu_reg_write_latch) {
					y_scroll = value;
					ppu_address_tmp = (ppu_address_tmp & 0x7C1F) | (((uint16_t)value & 0xF8) << 2);
					ppu_address_tmp = (ppu_address_tmp & 0x0FFF) | (((uint16_t)value & 0x7) << 12);
				} 
				else {
					x_scroll = value;
					ppu_address_tmp = (ppu_address_tmp & 0x7FE0) | (value >> 3);
				}
				ppu_reg_write_latch = !ppu_reg_write_latch;
				break;
			case 6:  // PPUADDR
				if (ppu_reg_write_latch) {
					ppu_address_tmp = (ppu_address_tmp & 0x7F00) | value;
					ppu_address = ppu_address_tmp;
				}
				else
					ppu_address_tmp = (ppu_address_tmp & 0xFF) | ((value & 0x3F) << 8);
				ppu_reg_write_latch = !ppu_reg_write_latch;
				break;
			case 7:  // PPUDATA
				ppu_poke(ppu_address, value);
				ppu_address += vram_inc;
				break;
		}
		
	}
	else if (address < 0x2000) {
		internal_ram[address & 0x7FF] = value;
	}
}

uint8_t ppu_peek(uint16_t address) {
	address &= 0x3FFF;

	if (address <= 0x1FFF) {
		return cart_ppu_read_func(address);
	}
	else if (address <= 0x3EFF) {
		switch (mirroring) {
			default:
			case vertical_mirroring:
				return vram[address & 0x7FF];
			case horizontal_mirroring:
				if ((address & 0x800) == 0)
					return vram[address & 0x3FF];
				else
					return vram[0x400 + (address & 0x3FF)];
			case one_screen_a:
				return vram[address & 0x3FF];
			case one_screen_b:
				return vram[0x400 + (address & 0x3FF)];
		}
	}
	else {
		// palette
		if ((address & 0x3) == 0 && (address & 0x1F) >= 0x10) // background mirroring
			return palette_ram[address & 0xF];
		else
			return palette_ram[address & 0x1F];
	}
}

void ppu_poke(uint16_t address, uint8_t value) {
	address &= 0x3FFF;
	if (address <= 0x1FFF) {
		if (chr_ram)
			chr_rom[address] = value;
	}
	else if (address <= 0x3EFF) {
		switch (mirroring) {
			default:
			case vertical_mirroring:
				vram[address & 0x7FF] = value;
				break;
			case horizontal_mirroring:
				if ((address & 0x800) == 0)
					vram[address & 0x3FF] = value;
				else
					vram[0x400 + (address & 0x3FF)] = value;
				break;
			case one_screen_a:
				vram[address & 0x3FF] = value;
				break;
			case one_screen_b:
				vram[0x400 + (address & 0x3FF)] = value;
				break;
		}
	}
	else {
		// palette
		if ((address & 0x3) == 0 && (address & 0x1F) >= 0x10) // background mirroring
			palette_ram[address & 0xF] = value;
		else
			palette_ram[address & 0x1F] = value;
	}
}
