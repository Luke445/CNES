#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include "control.h"
#include "memory.h"
#include "ppu.h"
#include "apu.h"
#include "6502.h"

/* clock hertz
master   =  21477272
cpu(/12) =  1789773;
ppu(/4)  =  5369318;
*/
// ppu cycles per frame
const int cycles_per_frame = 89342;
const double fps_limit = 60.0;
double ms_per_frame;

void add_cpu_cycles(int cycles) {
    master_clock.ppu_cycles += cycles * 3;
    master_clock.cpu_cycles += cycles;

    cycles_todo -= cycles;
}

int get_ppu_clocks() {
    int tmp = master_clock.ppu_cycles;
    master_clock.ppu_cycles = 0;
    return tmp;
}

void start_rom(FILE *rom_file) {
    master_clock.ppu_cycles = 0;
    master_clock.frames = 0;
    int countedFrames = 0;
    struct timeval start, end;
    double ms_elapsed;
    double avgFPS;
    double time_spent;
    int ppu_status;
    bool odd_frame = false;
    generate_irq = 0;
    speed_modifier_percent = 100;
    pause_emu = false;
    emu_done = false;

    init_memory(rom_file);
    if ( init_ppu(&master_clock) != 0 ) {
        printf("ppu init failed\n");
        return;
    }
    init_cpu();
    init_apu();

    cycles_todo = 0;
    skip_frame = false;

    while (1) {
        while (cycles_todo > 0) {
            // 3 ppu cycles per cpu cycle
            step();

            update_apu();
            ppu_status = update_ppu(get_ppu_clocks());

            if (reset_pressed) {
                reset_apu();
                reset();
                reset_pressed = false;
            }

            if (generate_irq != 0)
                irq();

            if (cycles_todo > cycles_per_frame) // occasionally skip a frame if VSYNC prevents 60.1 fps
                skip_frame = true;

            if (ppu_status != good) {
                //printf("status %d\n", ppu_status);
                if (ppu_status == do_nmi)
                    nmi();
                else
                    goto end;
            }
        }
        usleep(1000);

        while (pause_emu) {
            usleep(100000); // 0.1 sec
            read_events();
            if (emu_done)
                goto end;
        }
    }

end:
    close_memory();
}

void start_movie(FILE *rom_file, FILE *movie_file) {
    load_movie_file(movie_file);
    start_rom(rom_file);
}

void start_rom_save(FILE *rom_file, FILE *save_file) {
    load_save_file(save_file);
    start_rom(rom_file);
}
