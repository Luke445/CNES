#pragma once
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>

// shared for direct access by mappers
int num_prg_blocks;
int num_chr_blocks;

char *prg_rom;

char *chr_rom;
bool chr_ram;
enum mirror_modes {
	vertical_mirroring,
	horizontal_mirroring,
	one_screen_a,
	one_screen_b
};
int mirroring;

// shared for direct access by ppu
uint8_t oam_data[256];
uint8_t palette_ram[32];
int sprite_table_addr;
int background_table_addr;
int base_nametable;
uint8_t x_scroll;
uint8_t y_scroll;
uint16_t ppu_address_tmp;
uint16_t ppu_address;
bool left_clip_background;
bool left_clip_sprites;
bool show_background;
bool show_sprites;
bool vblank_nmi;
bool double_height_sprites;


void init_memory(FILE *rom_path);

void load_save_file(FILE *save_file);

void close_memory();

uint8_t peek(uint16_t address);

uint16_t peek2(uint16_t address);

void poke(uint16_t address, uint8_t value);

uint8_t ppu_peek(uint16_t address);

void ppu_poke(uint16_t address, uint8_t value);
