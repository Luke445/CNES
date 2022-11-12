#include <inttypes.h>
#include <stdio.h>
#include <SDL2/SDL.h>
#include "memory.h"
#include "control.h"
#include "ppu.h"


#define VRAM_DUMP 0
#define VRAM_WIDTH 512
#define VRAM_HEIGHT 480
#define VRAM_SCALE 1

#define CHR_DUMP 0
#define CHR_WIDTH 256
#define CHR_HEIGHT 128
#define CHR_SCALE 2

#define WIDTH 256
#define HEIGHT 240
#define SCALE 3


SDL_Window *vram_window;
SDL_Renderer *vram_renderer;
SDL_Texture *vram_texture;
uint32_t *vram_pixels;

SDL_Window *chr_window;
SDL_Renderer *chr_renderer;
SDL_Texture *chr_texture;
uint32_t *chr_pixels;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture * texture;
uint32_t pixels[WIDTH * HEIGHT];
uint8_t background[WIDTH];

const SDL_RendererFlags renderer_flags = SDL_RENDERER_ACCELERATED;
const SDL_RendererFlags vsync_renderer_flags = renderer_flags | SDL_RENDERER_PRESENTVSYNC;
//const SDL_RendererFlags vsync_renderer_flags = renderer_flags | 0;

bool movie_mode = false;
bool movie_2controllers = false;
FILE *movie_file;

bool controller_reading = false;
uint8_t controller_internal1 = 0;
uint8_t controller_internal2 = 0;
uint8_t controller_value1 = 0;
uint8_t controller_value2 = 0;
bool sprite0_hit = false;
bool vblank_flag = false;
bool generate_nmi = false;
bool sprite_overflow = false;

bool nmi_suppressed = false;
bool vblank_supressed = false;

int cur_scanline = 0;
int cur_scanline_pixel = 0;
int early_call_ret_value = good;
bool early_ret = false;

int secondary_oam_index = 0;
uint8_t secondary_oam[32] = {0};
uint8_t sprite_data[16];
int sprite_data_index = 0;
bool sprite0_check = false;

uint8_t nametable_byte;
uint8_t attribute_byte;
uint8_t bg_byte1;
uint8_t bg_byte2;

uint16_t tmp_addr;


SDL_Color rgb_color_lookup[] = {
	{ 84,  84,  84},  {  0,  30, 116},  {  8,  16, 144},  { 48,   0, 136},  { 68,   0, 100},  { 92,   0,  48},  { 84,   4,   0},  { 60,  24,   0},  { 32,  42,   0},  {  8,  58,   0},  {  0,  64,   0},  {  0,  60,   0},  {  0,  50,  60},  {  0,   0,   0}, {  0,   0,   0}, {  0,   0,   0},
	{152, 150, 152},  {  8,  76, 196},  { 48,  50, 236},  { 92,  30, 228},  {136,  20, 176},  {160,  20, 100},  {152,  34,  32},  {120,  60,   0},  { 84,  90,   0},  { 40, 114,   0},  {  8, 124,   0},  {  0, 118,  40},  {  0, 102, 120},  {  0,   0,   0}, {  0,   0,   0}, {  0,   0,   0},
	{236, 238, 236},  { 76, 154, 236},  {120, 124, 236},  {176,  98, 236},  {228,  84, 236},  {236,  88, 180},  {236, 106, 100},  {212, 136,  32},  {160, 170,   0},  {116, 196,   0},  { 76, 208,  32},  { 56, 204, 108},  { 56, 180, 204},  { 60,  60,  60}, {  0,   0,   0}, {  0,   0,   0},
	{236, 238, 236},  {168, 204, 236},  {188, 188, 236},  {212, 178, 236},  {236, 174, 236},  {236, 174, 212},  {236, 180, 176},  {228, 196, 144},  {204, 210, 120},  {180, 222, 120},  {168, 226, 144},  {152, 226, 180},  {160, 214, 228},  {160, 162, 160}, {  0,   0,   0}, {  0,   0,   0}
};
uint32_t color_lookup[64];


uint32_t sdl_color_to_uint32(SDL_Color *color) {
	return (uint32_t) ((color->r << 16) + (color->g << 8) + (color->b << 0));
}

int init_ppu() {
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
        return 1;

    if (VRAM_DUMP) {
	    vram_window = SDL_CreateWindow("VRAM", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, VRAM_WIDTH*VRAM_SCALE, VRAM_HEIGHT*VRAM_SCALE, 0);
	    if(!vram_window)
	        return 1;

	    vram_pixels = malloc(VRAM_WIDTH*VRAM_HEIGHT*sizeof(uint32_t));
	    memset(vram_pixels, 0, VRAM_WIDTH*VRAM_HEIGHT*sizeof(uint32_t));
    }

    if (CHR_DUMP) {
	    chr_window = SDL_CreateWindow("CHR", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, CHR_WIDTH*CHR_SCALE, CHR_HEIGHT*CHR_SCALE, 0);
	    if(!chr_window)
	        return 1;

	    chr_pixels = malloc(CHR_WIDTH*CHR_HEIGHT*sizeof(uint32_t));
	   	memset(chr_pixels, 0, CHR_WIDTH*CHR_HEIGHT*sizeof(uint32_t));
    }

    window = SDL_CreateWindow("NES", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH*SCALE, HEIGHT*SCALE, 0);
    if(!window)
        return 1;

    memset(color_lookup, 0, sizeof(color_lookup));
    for (int i = 0; i < 64; i++) {
    	color_lookup[i] = sdl_color_to_uint32((SDL_Color *) &rgb_color_lookup[i]);
    }

    update_renderers();

    nmi_occured = false;

	printf("finished ppu init\n");
	return 0;
}

void update_renderers() {
	SDL_RendererFlags flags = speed_modifier_percent == 100 ? vsync_renderer_flags : renderer_flags;

	SDL_DestroyRenderer(renderer);
    renderer = SDL_CreateRenderer(window, -1, flags);

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, WIDTH, HEIGHT);

    SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);

    if (VRAM_DUMP) {
    	SDL_DestroyRenderer(vram_renderer);
    	vram_renderer = SDL_CreateRenderer(vram_window, -1, flags);

	    vram_texture = SDL_CreateTexture(vram_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, VRAM_WIDTH, VRAM_HEIGHT);

	    SDL_RenderSetLogicalSize(vram_renderer, VRAM_WIDTH, VRAM_HEIGHT);
    }

    if (CHR_DUMP) {
    	SDL_DestroyRenderer(chr_renderer);
    	chr_renderer = SDL_CreateRenderer(chr_window, -1, flags);

	    chr_texture = SDL_CreateTexture(chr_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, CHR_WIDTH, CHR_HEIGHT);

	    SDL_RenderSetLogicalSize(chr_renderer, CHR_WIDTH, CHR_HEIGHT);
    }
}

void load_movie_file(FILE *f) {
	movie_mode = true;
	movie_file = f;
	char *checksum_key = "romChecksum base64:";
	char *port1_key = "port1 ";

	char buffer[256];
	// go up to first line with input
	while (buffer[0] != '|' && buffer[0] != EOF) {
    	fgets(buffer, 256, movie_file);
    	if (strncmp(checksum_key, buffer, strlen(checksum_key)) == 0) {
    		printf("%s", buffer + strlen(checksum_key));
    	}
    	else if (strncmp(port1_key, buffer, strlen(port1_key)) == 0) {
    		if (buffer[strlen(port1_key)] == '1') {
    			movie_2controllers = true;
    		}
    	}
	}
	fseek(movie_file, -strlen(buffer), SEEK_CUR);
}

void read_events() {
	SDL_Event e;
	uint8_t tmp = controller_internal1;
    while (SDL_PollEvent(&e)) {
    	switch (e.type) {
    		case SDL_WINDOWEVENT:
    			if (e.window.event != SDL_WINDOWEVENT_CLOSE)
    				break;
    		case SDL_QUIT:
    			emu_done = true;
    			break;
    		case SDL_KEYDOWN:
    			// WASD - directionals
    			// K = A and J = B
    			// I = start
    			// U = select
    			// R = reset
    			switch (e.key.keysym.sym) {
    				case SDLK_k: // A
    					tmp |= 0b00000001;
    					break;
    				case SDLK_j: // B
    					tmp |= 0b00000010;
    					break;
    				case SDLK_u: // Select
    					tmp |= 0b00000100;
    					break;
    				case SDLK_i: // Start
    					tmp |= 0b00001000;
    					break;
    				case SDLK_w: // Up
    					tmp |= 0b00010000;
    					break;
    				case SDLK_s: // Down
    					tmp |= 0b00100000;
    					break;
    				case SDLK_a: // Left
    					tmp |= 0b01000000;
    					break;
    				case SDLK_d: // Right
    					tmp |= 0b10000000;
    					break;
    				case SDLK_r:
    					if (!movie_mode)
    						reset_pressed = true;
    					break;
    				// other hotkeys
    				case SDLK_p: // pause
    					pause_emu = !pause_emu;
    					break;
    				case SDLK_LEFTBRACKET:
    					if (!pause_emu) {
    						if (speed_modifier_percent == 100000)
    							speed_modifier_percent = 200;
    						else if (speed_modifier_percent != 10)
    							speed_modifier_percent -= 10;
    						printf("speed modifier: %d%%\n", speed_modifier_percent);
    						update_renderers();
    					}
    					break;
    				case SDLK_RIGHTBRACKET:
    				    if (!pause_emu) {
    						if (speed_modifier_percent <= 190) {
    							speed_modifier_percent += 10;
    							printf("speed modifier: %d%%\n", speed_modifier_percent);
    						}
    						else {
    							speed_modifier_percent = 100000;
    							printf("speed modifier: unlimited\n");
    						}
    						update_renderers();
    					}
    					break;
    				case SDLK_t: // test
    					//sprite0_hit = true;
    					break;
    			}
    			break;
    		case SDL_KEYUP:
    			switch (e.key.keysym.sym) {
    				case SDLK_k: // A
    				    tmp &= ~0b00000001;
    					break;
    				case SDLK_j: // B
    					tmp &= ~0b00000010;
    					break;
    				case SDLK_u: // Select
    					tmp &= ~0b00000100;
    					break;
    				case SDLK_i: // Start
    					tmp &= ~0b00001000;
    					break;
    				case SDLK_w: // Up
    					tmp &= ~0b00010000;
    					break;
    				case SDLK_s: // Down
    					tmp &= ~0b00100000;
    					break;
    				case SDLK_a: // Left
    					tmp &= ~0b01000000;
    					break;
    				case SDLK_d: // Right
    					tmp &= ~0b10000000;
    					break;
    			}
    			break;
    	}
    }

    if (!movie_mode) {
    	controller_internal1 = tmp;

    	if (controller_reading) {
    		controller_value1 = controller_internal1;
    		controller_value2 = controller_internal2;
    	}
    }
}

void read_movie() {
	char buffer[256];
	// read the next line in playback file
	if ( fgets(buffer, 256, movie_file) == NULL ) {
		printf("movie playback finished\n");
		movie_mode = false;
		return;
	}

	// if blank line, go to next line
	if (buffer[0] != '|') {
		read_movie();
		return;
	}

	// parse controller input
	uint8_t tmp1 = 0;
	for (int i = 3; i < 11; i++) {
		tmp1 = (tmp1 << 1) | (buffer[i] != '.' ? 1 : 0);
	}
	uint8_t tmp2 = 0;
	if (movie_2controllers) {
		for (int i = 12; i < 20; i++) {
			tmp2 = (tmp2 << 1) | (buffer[i] != '.' ? 1 : 0);
		}
	}

	// check for commands
	if (buffer[1] == '1')
		reset_pressed = true;
	else if (buffer[1] == '2') {
		printf("movie hard reset command!!!\n");
		reset_pressed = true;
	}

	controller_internal1 = tmp1;
	controller_internal2 = tmp2;

	if (controller_reading) {
		controller_value1 = controller_internal1;
		controller_value2 = controller_internal2;
	}
}

uint8_t get_controller1() {
	uint8_t out = controller_value1 & 0x1;
	controller_value1 >>= 1;
	return out;
}

uint8_t get_controller2() {
	uint8_t out = controller_value2 & 0x1;
	controller_value2 >>= 1;
	return out;
}

void set_controller(bool value) {
	if (value && !controller_reading) {
		controller_value1 = controller_internal1;
		controller_value2 = controller_internal2;
	}
	controller_reading = value;
}

void flags_update(int cur_pixel) {
	if (show_sprites && sprite0_check) {
		uint8_t y, sprite_flags, next_byte, next_byte2;
		int x_inc, x, color_index;
		bool horizontal_flip;

		y = secondary_oam[0] + 1;
		if (y >= 0xF0)
			return;

		sprite_flags = secondary_oam[2];
		x = secondary_oam[3];
		if (sprite_flags & 0x40) // horizontal flip
			x_inc = 1;
		else {
			x_inc = -1;
			x += 7;
		}

		next_byte = sprite_data[0];
		next_byte2 = sprite_data[1];

		for (int j = 0; j < 8; j++) {
			if (left_clip_sprites && x < 8)
	           	continue;
			color_index = ((1 & next_byte2) << 1 | (1 & next_byte));
	    	next_byte = next_byte >> 1;
	        next_byte2 = next_byte2 >> 1;
	        if ((color_index & 0x3) != 0 && x < WIDTH) {
	        	if ((background[x] & 0x3) != 0 && x != 255) {
	        		if (x <= cur_pixel)
	           			sprite0_hit = true;
	           	}
	        }

	        x += x_inc;
	    }

	}
}

uint8_t get_ppu_status() {
	uint8_t out = 0;

	if (cur_scanline == 241) {
		if (cur_scanline_pixel == 1 || cur_scanline_pixel == 2 || cur_scanline_pixel == 3)
			nmi_suppressed = true;
		if (cur_scanline_pixel == 1)
			vblank_supressed = true;
	}


	if (cur_scanline < HEIGHT && cur_scanline_pixel <= 255) // dry run of line up to current pixel to set flags
		flags_update(cur_scanline_pixel);

	if (vblank_flag) {
		out |= 0b10000000;
		vblank_flag = false;
		generate_nmi = false;
	}

	if (sprite0_hit)
		out |= 0b01000000;

	if (sprite_overflow)
		out |= 0b00100000;

	return out;
}

void print_buffer(uint8_t *buffer, int len) {
	for (int i = 0; i < len; i++) {
		printf("%02x", buffer[i]);
	}
	printf("\n");
}

void draw_tile(uint32_t *pixel_buffer, int line_width, int pos, uint16_t tile_addr, int palette_num) {
	bool horizontal_flip, vertical_flip = false;
	uint8_t next_byte, next_byte2;

	int palette_index, pixel_index;
	Palette tile_palette;
	if (palette_num == -1) {
		tile_palette[0] = sdl_color_to_uint32( &( (SDL_Color) {0, 0, 0} ) );
		tile_palette[1] = sdl_color_to_uint32( &( (SDL_Color) {85, 85, 85} ) );
		tile_palette[2] = sdl_color_to_uint32( &( (SDL_Color) {170, 170, 170} ) );
		tile_palette[3] = sdl_color_to_uint32( &( (SDL_Color) {255, 255, 255} ) );
	}
	else {
		tile_palette[0] = sdl_color_to_uint32((SDL_Color *) &rgb_color_lookup[palette_ram[0]]);
		tile_palette[1] = sdl_color_to_uint32((SDL_Color *) &rgb_color_lookup[palette_ram[palette_num*4 + 1]]);
		tile_palette[2] = sdl_color_to_uint32((SDL_Color *) &rgb_color_lookup[palette_ram[palette_num*4 + 2]]);
		tile_palette[3] = sdl_color_to_uint32((SDL_Color *) &rgb_color_lookup[palette_ram[palette_num*4 + 3]]);
	}

	int x_inc = horizontal_flip ? 1 : -1;
	int x_start = horizontal_flip ? 0 : 7;

	for (int y = 0; y >= 0 && y < 8; y++) {
		next_byte = ppu_peek(tile_addr + y);
		next_byte2 = ppu_peek(tile_addr + y+8);
		for (int x = x_start; x >= 0 && x < 8; x+=x_inc) {
			palette_index = (1 & next_byte2) << 1 | (1 & next_byte);
           	next_byte = next_byte >> 1;
           	next_byte2 = next_byte2 >> 1;

			if (vertical_flip)
				pixel_index = pos + (7-y)*line_width+x;
			else
           		pixel_index = pos + y*line_width+x;

			pixel_buffer[pixel_index] = tile_palette[palette_index];
		}
	}
}

void finish_line(int line) {
	if (show_sprites) {
		uint8_t priority[256];
		memset(priority, 0, 256);
		uint8_t y, sprite_flags;
		int palette_num, x_inc, x;
		int data_index = 0;
		bool sprite_priority;
		for (int i = 0; i < secondary_oam_index; i+=4) {
			y = secondary_oam[i] + 1;
			if (y >= 0xF0)
				continue;

			sprite_flags = secondary_oam[i + 2];
			x = secondary_oam[i + 3];

			palette_num = 4 + (sprite_flags & 0x3);
			sprite_priority = sprite_flags & 0x20 ? true : false;
			
			if (sprite_flags & 0x40) // horizontal flip
				x_inc = 1;
			else {
				x_inc = -1;
				x += 7;
			}

			uint8_t next_byte = sprite_data[data_index++];
			uint8_t next_byte2 = sprite_data[data_index++];

			for (int j = 0; j < 8; j++) {
				if (left_clip_sprites && x < 8)
		           	continue;
				int color_index = palette_num*4 + ((1 & next_byte2) << 1 | (1 & next_byte));
		    	next_byte >>= 1;
		        next_byte2 >>= 1;
		        if ((color_index & 0x3) != 0 && x < WIDTH) {
		        	if (sprite0_check && i == 0 && (background[x] & 0x3) != 0 && x != 255) {
		           		sprite0_hit = true;
		           	}
		           	if (sprite_priority)
		           		priority[x] = 1;

		           	if (!priority[x] || (background[x] & 0x3) == 0) {
		           		background[x] = color_index;
		           	}
		        }
		        x += x_inc;
		    }
		}
	}

	int start_pixel = line*WIDTH;
	for (int pixel = 0; pixel < WIDTH; pixel++) {
		if (start_pixel + pixel <= WIDTH*HEIGHT) {
			pixels[start_pixel + pixel] = color_lookup[palette_ram[background[pixel]]];
		}
	}
}

void load_secondary_oam(int line) {
	// load sprites and set sprite overflow if either rendering is enabled
	memset(secondary_oam, 0xFF, 32);
	secondary_oam_index = 0;
	sprite0_check = false;
	int sprite_height;
	if (double_height_sprites)
		sprite_height = 15;
	else
		sprite_height = 7;
	if (show_sprites || show_background) {
		for (int i = 0; i < 256; i += 4) {
			uint8_t sprite_line = oam_data[i];
			if (sprite_line >= 0xF0)
				continue;
			sprite_line++; 
			if (sprite_line >= (line - sprite_height) && sprite_line <= line) {
				if (secondary_oam_index <= 28) {
					memcpy(secondary_oam + secondary_oam_index, oam_data + i, 4);
					if (i == 0)
						sprite0_check = true;
					secondary_oam_index += 4;
				}
				else {
					sprite_overflow = true;
					break;
				}
			}
		}
	}
}

void dump_chr_rom() {
	int index = 0;
	for (int y = 0; y < 16; y++) {
		for (int x = 0; x < 16; x++) {
			draw_tile(chr_pixels, CHR_WIDTH, y*8*CHR_WIDTH + x*8, index*16, -1);
			index++;
		}
	}

	for (int y = 0; y < 16; y++) {
		for (int x = 16; x < 32; x++) {
			draw_tile(chr_pixels, CHR_WIDTH, y*8*CHR_WIDTH + x*8, index*16, -1);
			index++;
		}
	}
}

void dump_vram() {
	int tile_pos, palette_num;
	uint16_t tile_addr, nametable_addr, attribute_addr;
	int x2, y2;
	for (int n = 0; n < 4; n++) {
		for (int y = 0; y < 30; y++) {
			for (int x = 0; x < 32; x++) {
				nametable_addr = 0x2000 | (n << 10) | (y << 5) | x;
				attribute_addr = 0x23C0 | (n << 10) | ((y >> 2) << 3) | (x >> 2);
				tile_pos = ((nametable_addr >> 5) & 0x2) + ((nametable_addr >> 1) & 0x1);

				tile_addr = background_table_addr + ppu_peek(nametable_addr) * 16;
				palette_num = (ppu_peek(attribute_addr) >> (tile_pos * 2)) & 0x3;

				x2 = x*8 + ( (n & 1) ? 32 : 0 )*8;
				y2 = y*8 + ( (n & 2) ? 30 : 0 )*8;
				draw_tile(vram_pixels, VRAM_WIDTH, y2*VRAM_WIDTH + x2, tile_addr, palette_num);
			}
		}
	}

	/*uint8_t y, flags, x;
	uint16_t tile_addr;
	bool vertical_flip, horizontal_flip;
	for (int i = 0; i < 64; i++) {
		y = oam_data[i*4] + 1;
		if (y >= 0xF0)
			continue;
		tile_addr = sprite_table_addr + oam_data[i*4 + 1]*16;
		flags = oam_data[i*4 + 2];
		x = oam_data[i*4 + 3];

		horizontal_flip = flags & 0x40 ? true : false;
		vertical_flip = flags & 0x80 ? true : false;
		draw_tile(y*WIDTH + x, tile_addr, 4 + (flags & 0x3), horizontal_flip, vertical_flip);
	}*/
}

void next_frame() {
	read_events();
	master_clock.frames++;

	if (skip_frame) {
		skip_frame = false;
		return;
	}

	SDL_UpdateTexture(texture, NULL, pixels, WIDTH * sizeof(uint32_t));
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);

	if (VRAM_DUMP) {
		dump_vram();

		SDL_UpdateTexture(vram_texture, NULL, vram_pixels, VRAM_WIDTH * sizeof(uint32_t));
		SDL_RenderCopy(vram_renderer, vram_texture, NULL, NULL);
		SDL_RenderPresent(vram_renderer);
	}

	if (CHR_DUMP) {
		dump_chr_rom();

		SDL_UpdateTexture(chr_texture, NULL, chr_pixels, CHR_WIDTH * sizeof(uint32_t));
		SDL_RenderCopy(chr_renderer, chr_texture, NULL, NULL);
		SDL_RenderPresent(chr_renderer);
	}
}

void draw_tile_slice(int line, int pixel) {
	if (show_background) {
		int palette_index, color_index;
		int x_fine_offset = x_scroll & 0x7;

		int tile_pos = ((ppu_address >> 5) & 0x2) + ((ppu_address >> 1) & 0x1);
		palette_index = (attribute_byte >> (tile_pos * 2)) & 0x3;

		for (int i = 7; i >= 0; i--) {
			color_index = palette_index*4 + ((1 & bg_byte2) << 1 | (1 & bg_byte1));
           	bg_byte1 = bg_byte1 >> 1;
           	bg_byte2 = bg_byte2 >> 1;
           	int scanline_index = pixel + i - x_fine_offset;
           	if (left_clip_background && (scanline_index & 0xFF) < 8)
           		continue;

           	if ((color_index & 0x3) != 0 && scanline_index >= 0 && scanline_index < WIDTH)
           		background[scanline_index & 0xFF] = color_index;
        }
	}
}

void ppu_address_horiz_inc() {
	if ((ppu_address & 0x001F) == 31) {
		ppu_address &= ~0x001F;
		ppu_address ^= 0x0400;
	}
	else
		ppu_address += 1;
}

void ppu_address_vert_inc() {
	if ((ppu_address & 0x7000) != 0x7000)
		ppu_address += 0x1000;
	else {
		ppu_address &= ~0x7000;
		int y = (ppu_address & 0x03E0) >> 5;
		if (y == 29) {
			y = 0;
			ppu_address ^= 0x0800;
		}
		else if (y == 31)
			y = 0;
		else
	    	y += 1;
		ppu_address = (ppu_address & ~0x03E0) | (y << 5);
	}
}

uint16_t get_sprite_addr(int line, int sprite_index) {
	uint16_t sprite_addr;

	uint8_t y = secondary_oam[sprite_index * 2] + 1;

	uint8_t value = secondary_oam[sprite_index * 2 + 1];

	uint8_t flags = secondary_oam[sprite_index * 2 + 2];
	bool vertical_flip = flags & 0x80 ? true : false;

	int tile_vertical_offset;

	if (vertical_flip) { // vertical flip
		if (double_height_sprites)
			tile_vertical_offset = 15 - (line + 1 - y);
		else
			tile_vertical_offset = 7 - (line + 1 - y);
	}
	else
		tile_vertical_offset = line + 1 - y;

	if (double_height_sprites) {
		if (tile_vertical_offset >= 8) {
			sprite_addr = ((value & 0xFE) + 1)*16;
			tile_vertical_offset -= 8;
		}
		else
			sprite_addr = (value & 0xFE)*16;

		if (value & 0x1)
			sprite_addr += 0x1000;
	} 
	else
		sprite_addr = sprite_table_addr + value*16;

	return sprite_addr + tile_vertical_offset;
}

void ppu_cycle(int line, int pixel) {
	if (line == 241) {
		if (pixel == 1) {
			if (!vblank_supressed)
				vblank_flag = true;
			else
				vblank_supressed = false;

			// read next input for movie playback when vblank is set
			if (movie_mode) {
		   		read_movie();
			}
		}
		else if (pixel == 3) {
			if (!nmi_suppressed) {
				generate_nmi = true;
				nmi_occured = false;
			}
			else
				nmi_suppressed = false;
		}
	}

	if (line >= 240 && line <= 260)
		return; // no drawing during v-blank


	// tile fetches
	// p=0 is idle
	int p = pixel;

	// visible scanline (tiles)
	if (p >= 1 && p <= 255) { // tile fetches
		if (line == 261) {
			if (p == 1) {
				vblank_flag = false;
				sprite0_hit = false;
				sprite_overflow = false;

			}
			else if (p == 6) {
				generate_nmi = false;
				nmi_occured = false;
			}
		}
		if (show_background || show_sprites) {
			p &= 0x7;
			switch (p) {
				case 1:
					// nt byte
					nametable_byte = ppu_peek(0x2000 | (ppu_address & 0x0FFF));
					break;
				case 3:
					// at byte
					attribute_byte = ppu_peek(0x23C0 | (ppu_address & 0x0C00) | ((ppu_address >> 4) & 0x38) | ((ppu_address >> 2) & 0x07));
					break;
				case 5:
					// low bg tile byte
					tmp_addr = background_table_addr + nametable_byte * 16 + ((ppu_address >> 12) & 0x7);
					bg_byte1 = ppu_peek(tmp_addr);
					break;
				case 7:
					// high bg tile byte
					bg_byte2 = ppu_peek(tmp_addr + 8);
					// draw 8 pixels here
					if (line != 261)
						draw_tile_slice(line, pixel + 9);
					break;
				case 0: // this is 8 (first 0 is skipped)
					ppu_address_horiz_inc();
					break;
			}
		}
	}
	else if (p == 256) {
		if (show_background || show_sprites) {
			ppu_address_vert_inc();
		}
	}
	else if (p == 257) { // sprite eval and stuff
		// hori (v) = hori (t)
		if (show_background || show_sprites) {
			oam_address = 0;
			ppu_address = (ppu_address & ~0b10000011111) | (ppu_address_tmp & 0b10000011111);
		}

		// finish drawing scanline, and incorporate the sprites currently loaded
		if (line != 261)
			finish_line(line);

		memset(background, 0, WIDTH);

		// clear secondary OAM
		// sprite evaluation for next scanline
		load_secondary_oam(line + 1);

		if (line == 239) {
			next_frame();
		}

		// first fetch for sprites (garbage)
		// TODO

		sprite_data_index = 0;
	}
	else if (p <= 320) { // sprite fetches
		if (show_background || show_sprites) {
			oam_address = 0;
			if (line == 261) {
				if (p >= 280 && p <= 304) {
					ppu_address = (ppu_address & 0b10000011111) | (ppu_address_tmp & ~0b10000011111);
				}
			}

			p &= 0x7;
			switch (p) {
				case 1: // nt byte (garbage)
					break;
				case 3: // at byte (garbage)
					break;
				case 5:
					// low bg tile byte
					tmp_addr = get_sprite_addr(line, sprite_data_index);
					sprite_data[sprite_data_index++] = ppu_peek(tmp_addr);
					break;
				case 7:
					// high bg tile byte
					sprite_data[sprite_data_index++] = ppu_peek(tmp_addr + 8);
					break;
			}
		}
	}
	else if (p <= 336) { // first 2 tiles for next line
		if (show_background || show_sprites) {
			p &= 0x7;
			switch (p) {
				case 1:
					// nt byte
					nametable_byte = ppu_peek(0x2000 | (ppu_address & 0x0FFF));
					break;
				case 3:
					// at byte
					attribute_byte = ppu_peek(0x23C0 | (ppu_address & 0x0C00) | ((ppu_address >> 4) & 0x38) | ((ppu_address >> 2) & 0x07));
					break;
				case 5:
					// low bg tile byte
					tmp_addr = background_table_addr + nametable_byte * 16 + ((ppu_address >> 12) & 0x7);
					bg_byte1 = ppu_peek(tmp_addr);
					break;
				case 7:
					// high bg tile byte
					bg_byte2 = ppu_peek(tmp_addr + 8);
					// draw 8 pixels here
					draw_tile_slice(line + 1, pixel - 327);
					break;
				case 0: // this is 8 (first 0 is skipped)
					ppu_address_horiz_inc();
					break;
			}
		} 
	}
	else { // p <= 340      garbage fetches
		if (line == 261 && p == 338 && (master_clock.frames % 2) == 1 && (show_background || show_sprites)) {
			cur_scanline_pixel = 339;
		}
	}
}

void early_update_ppu() {
	early_call_ret_value = update_ppu(get_ppu_clocks());
	early_ret = true;
}

int update_ppu(int cycles_passed) {
	int tmp = cycles_passed;
	if (emu_done) {
		SDL_DestroyRenderer(renderer);
   		SDL_DestroyWindow(window);

	    if (VRAM_DUMP) {
			SDL_DestroyRenderer(vram_renderer);
	   		SDL_DestroyWindow(vram_window);
		}

		if (CHR_DUMP) {
			SDL_DestroyRenderer(chr_renderer);
	   		SDL_DestroyWindow(chr_window);
		}
		SDL_Quit();
		return quit;
	}

	if (early_ret) {
		early_ret = false;
		return early_call_ret_value;
	}

	int ret_val = good;
	
	for (int i = 0; i < cycles_passed; i++) {
		ppu_cycle(cur_scanline, cur_scanline_pixel);

		cur_scanline_pixel++;
		if (cur_scanline_pixel == 341) {
			cur_scanline++;
			if (cur_scanline == 262)
				cur_scanline = 0; 
			cur_scanline_pixel = 0;
		}
	}

	if (generate_nmi && vblank_nmi && !nmi_occured) {
		ret_val = do_nmi;
		nmi_occured = true;
	}

	return ret_val;
}
