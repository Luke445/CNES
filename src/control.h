#pragma once
#include <stdio.h>
#include <stdbool.h>

typedef struct Clock {
    int ppu_cycles;
    int cpu_cycles;
    int frames;
} Clock;
Clock master_clock;

int cycles_todo;
bool skip_frame;

enum irq_masks {
    apu_irq = 1,
    cart_irq = 2,
    dmc_irq = 4
};
int generate_irq;
bool reset_pressed;

bool pause_emu;
bool emu_done;
int speed_modifier_percent;

void add_cpu_cycles(int cycles);

int get_ppu_clocks();

void start_rom(FILE *f);

void start_movie(FILE *rom_file, FILE *movie_file);

void start_rom_save(FILE *rom_file, FILE *save_file);
