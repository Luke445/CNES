#include <stdio.h>
#include <string.h>
#include "control.h"


int main(int argc, char *argv[]) {
    FILE *rom_file;
    FILE *save_file;
    FILE *movie_file;
    char *rom_filename;
    bool rom_opened = false;
    bool movie_opened = false;
    bool open_save_file = false;

    for (int i = 1; i < argc; i++) {
        char *current = argv[i];

        if (current[0] == '-') {
            if (current[1] == 's') {
                open_save_file = true;
            }
        }
        else if (!rom_opened) {
            rom_filename = current;
            rom_opened = true;
        }
        else if (!movie_opened) {
            movie_file = fopen(argv[2], "r");
            if (movie_file == NULL) {
                printf("could not open movie file\n");
                return 0;
            }
            movie_opened = true;
        }

    }

    if (argc <= 1 || !rom_opened) {
        printf("usage: cnes [flags] rom_file [fm2_movie_file]\n");
        printf("flags:\n");
        printf("    -s : open a save file for the provided rom\n");
        printf("\n");
        printf("Controls:\n");
        printf("D-pad: WASD\n");
        printf("A:     k  |  B:      j\n");
        printf("Start: i  |  Select: u\n");
        return 0;
    }

    rom_file = fopen(rom_filename, "rb");
    if (rom_file == NULL) {
        printf("could not open rom file\n");
        return 0;
    }

    if (open_save_file) {
        int len = strlen(rom_filename);
        // change extention to .sav
        rom_filename[len - 3] = 's';
        rom_filename[len - 2] = 'a';
        rom_filename[len - 1] = 'v';
        save_file = fopen(rom_filename, "rb+"); // existing file
        if (save_file == NULL) {
            save_file = fopen(rom_filename, "wb+"); // create new
            if (save_file == NULL) {
                printf("could not open save file\n");
                return 0;
            }
        }
    }


    if (open_save_file)
        start_rom_save(rom_file, save_file);
    else if (movie_opened)
        start_movie(rom_file, movie_file);
    else 
        start_rom(rom_file);


    return 0;
}
