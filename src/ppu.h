#pragma once
#include <stdio.h>

typedef uint32_t Palette[4];

enum ppu_codes {
    good,
    do_nmi,
    frame,
    quit
};

bool nmi_occured;

int init_ppu();

void update_renderers();

uint8_t get_controller1();

uint8_t get_controller2();

void set_controller(bool value);

uint8_t get_ppu_status();

void print_buffer(uint8_t *buffer, int len);

void draw_tile_slice(int line, int pixel);

uint16_t get_sprite_addr(int line, int sprite_index);

void ppu_cycle(int line, int pixel);

void finish_line(int line, int flags_update);

void load_secondary_oam(int line);

void load_movie_file(FILE *movie_file);

void read_events();

void render_frame();

void next_frame();

void early_update_ppu();

int update_ppu();
