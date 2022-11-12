#include <inttypes.h>
#include <stdio.h>
#include <SDL2/SDL.h>
#include "apu.h"
#include "memory.h"
#include "control.h"

SDL_AudioDeviceID dev_id;
SDL_AudioCVT cvt;

bool early_updated = false;
int apu_clock = 0;
int left_over_cycles = 0;
Clock apu_last_update;

#define output_freq 44100
#define sample_size 4096 * 8

#define apu_freq 894886

bool duty_lookup[4][8] = {
	{0, 0, 0, 0, 0, 0, 0, 1},
	{0, 0, 0, 0, 0, 0, 1, 1},
	{0, 0, 0, 0, 1, 1, 1, 1},
	{1, 1, 1, 1, 1, 1, 0, 0}
};

float pulse_mixer_lookup[31];

// quarter of a second
#define audio_buffer_size apu_freq / 8
float *audio_buffer;
int audio_buffer_playback_pos;
int audio_buffer_writing_pos;


bool disable_interupts = false;
bool sequence_5step = false;
bool frame_interupt_flag = false;
bool dmc_interupt_flag = false;

typedef struct {
	int envelope_volume;
	int envelope_divider;
	bool envelope_start;
	bool loop;
	bool constant_volume;
} Envelope;


typedef struct {
	Envelope env;
	uint16_t timer;
	uint8_t length;
	int duty;
	bool halt;
	int counter;
	bool current_duty;
	int sweep_divider;
	bool sweep_reload;
	int wave_tick;
	int volume;
	uint8_t sweep_var;
	bool halt_length;
} Pulse;
Pulse p1;
Pulse p2;

typedef struct {
	uint16_t timer;
	uint8_t length;
	uint8_t reload_value;
	bool reload;
	bool control;
	bool halt;
	int wave_tick;
	int linear_counter;
	int counter;
	int volume;
} Triangle;
Triangle tri;

typedef struct {
	Envelope env;
	uint16_t shift_reg;
	int period;
	int volume;
	uint8_t length;
	int counter;
	bool mode;
	bool halt;
	bool halt_length;
} Noise;
Noise noise;

typedef struct {
	uint8_t cur_sample;
	int sample_bits_left;
	int rate;
	int rate_counter;
	uint8_t output;
	uint16_t sample_addr;
	uint16_t sample_length;
	uint16_t cur_addr;
	uint16_t bytes_left;
	bool irq_enabled;
	bool loop_flag;
	bool halt;
} DMC;
DMC dmc;

float output_buffer[sample_size];

void init_apu() {
    SDL_AudioSpec desired, obtained;

    desired.freq = apu_freq;
    desired.format = AUDIO_F32;
    desired.channels = 1;
    desired.samples = sample_size;
    desired.userdata = 0;
    desired.callback = mix_audio;

    dev_id = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);

    /*SDL_BuildAudioCVT(&cvt, AUDIO_F32, 1, apu_freq, AUDIO_F32, 1, output_freq);
	SDL_assert(cvt.needed);
	cvt.len = sample_size * sizeof(float);
	cvt.buf = (uint8_t *)output_buffer;*/

	memset(&p1, 0, sizeof(Pulse));
	memset(&p2, 0, sizeof(Pulse));
	memset(&tri, 0, sizeof(Triangle));
	memset(&noise, 0, sizeof(Noise));
	memset(&dmc, 0, sizeof(DMC));

	audio_buffer = malloc(sizeof(float) * audio_buffer_size); 
	memset(audio_buffer, 0, sizeof(float) * audio_buffer_size);
	audio_buffer_playback_pos = 0;
	audio_buffer_writing_pos = audio_buffer_size / 2;

	// write 0 to 0x4000 to 0x4013
    for (int i = 0; i <= 0x13; i++)
    	apu_poke(0x4000 + i, 0x0);
    apu_poke(0x4017, 0x0);
	reset_apu();

	SDL_PauseAudioDevice(dev_id, 0);
	SDL_UnlockAudioDevice(dev_id);

	for (int i = 0; i < 31; i++) {
		pulse_mixer_lookup[i] = 95.52 / ((8128.0 / i) + 100);
	}
}

void reset_apu() {
    apu_poke(0x4015, 0x0);
    //apu_poke(0x4017, apu_open_bus);
    //disable_interupts = false;
    //apu_poke(0x4017, 0x0);
    frame_interupt_flag = false;
    dmc_interupt_flag = false;
    generate_irq &= ~apu_irq;
    generate_irq  &= ~dmc_irq;

    apu_clock = 4;

	noise.shift_reg = 1;
}

int generate_pulse(Pulse *p) {
	int out = 0;
	if (p->length >= 1 && p->timer >= 8) {
		if (p->current_duty) {
			if (p->env.constant_volume)
				out = p->volume;
			else
				out = p->env.envelope_volume;
		}

		p->counter--;
		if (p->counter <= 0) {
			p->counter = p->timer;

			p->wave_tick += 1;
			if (p->wave_tick >= 8)
				p->wave_tick = 0;
			
			p->current_duty = duty_lookup[p->duty][p->wave_tick];
		}
	}
	return out;

}

/* more accurate, but creates pops
int tri_lookup[] = {
	15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
	0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15
};*/
int tri_lookup[] = {
	0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
	15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1,  0
};

int generate_triangle() {
	int out = 0;
	if (tri.length >= 1 && tri.linear_counter >= 1) {
		out = tri.volume;

		tri.counter -= 2;
		if (tri.counter <= 0) {
			tri.counter = tri.timer;

			tri.wave_tick = (tri.wave_tick + 1) % 32;
			tri.volume = tri_lookup[tri.wave_tick];
		}
	}
	return out;
}

int generate_noise() {
	int out = 0;
	if (noise.length >= 1) {
		if ((noise.shift_reg & 1) == 0) {
			if (noise.env.constant_volume)
				out = noise.volume;
			else
				out = noise.env.envelope_volume;
		}

		if (noise.counter >= noise.period) {
			int bit;
			if (noise.mode)
				bit = (noise.shift_reg & 0x1) ^ ((noise.shift_reg & 0x40) >> 6);
			else
				bit = (noise.shift_reg & 0x1) ^ ((noise.shift_reg & 0x2) >> 1);
			noise.shift_reg >>= 1;
			noise.shift_reg = (bit << 14) | noise.shift_reg;

			noise.counter = 0;
		}
		else {
			noise.counter++;
		}
	}
	return out;
}

int generate_dmc() {
	return dmc.output;
	/*for (int i = 0; i < sample_size; i++) {
		dmc.buffer[i] = dmc.output;
	}*/
}

float last_change = 0;
float last_value = 0;
const float decay = 0.01;
float final_out[sample_size];
void mix_audio(void *unused, uint8_t *stream, int len) {
	if (pause_emu) {
		memset(stream, 0, len);
		return;
	}

	int num_samples = len / sizeof(float);
	if (num_samples != sample_size) {
		printf("audio sample size wrong\n");
		memset(stream, 0, len);
		return;
	}

	float cur_sample;
	for (int i = 0; i < sample_size; i++) {
		cur_sample = audio_buffer[audio_buffer_playback_pos++];
		if (audio_buffer_playback_pos >= audio_buffer_size) {
			audio_buffer_playback_pos = 0;
			//printf("%d, %d %d \n", audio_buffer_writing_pos, audio_buffer_size / 2, cycles_todo);
		}

		// smooths out audio to prevent pops
		last_change = last_value - cur_sample;
		if (last_change > decay)
			last_change -= decay;
		else if (last_change < decay)
			last_change += decay;
		else
			last_change = 0;
		final_out[i] = last_change + cur_sample;
		last_value = final_out[i];
	}

	memcpy(stream, (uint8_t *)final_out, len);


	// calculate how many cycles the cpu is allowed to run for
	cycles_todo += sample_size * 2;
	int ideal_write_pos = audio_buffer_playback_pos + (audio_buffer_size / 2);
	if (ideal_write_pos > audio_buffer_size)
		ideal_write_pos -= audio_buffer_size;
	int diff = audio_buffer_writing_pos - ideal_write_pos;
	if (diff < 0) {
		cycles_todo = diff * -2;
	}
}

void read_next_dmc_byte() {
	dmc.cur_sample = peek(dmc.cur_addr);
	// need to delay cpu by some cycles (1-4)
	add_cpu_cycles(4); // this is not quite accurate, but should be good enough for most games

	if (dmc.cur_addr != 0xFFFF)
		dmc.cur_addr++;
	else
		dmc.cur_addr = 0x8000;

	dmc.bytes_left--;
	if (dmc.bytes_left == 0) {
		if (dmc.loop_flag) {
			dmc.cur_addr = dmc.sample_addr;
			dmc.bytes_left = dmc.sample_length;
		}
		else if (dmc.irq_enabled) {
			dmc_interupt_flag = true;
			generate_irq |= dmc_irq;
		}
	}
}

void update_dmc() {
	if (dmc.sample_bits_left == 0) {
		if (dmc.bytes_left != 0) {
			read_next_dmc_byte();
			dmc.sample_bits_left = 8;
		}
		else {
			return;
		}
	}

	if ((dmc.cur_sample & 1) == 0) {
		if (dmc.output >= 2)
			dmc.output -= 2;
	}
	else {
		if (dmc.output <= 125)
			dmc.output += 2;
	}

	dmc.cur_sample >>= 1;
	
	dmc.sample_bits_left--;
}

#define APU_NO_UPDATES 			0
#define APU_QUARTER_FRAME_CLOCK 1
#define APU_HALF_FRAME_CLOCK 	2
#define APU_FRAME_CLOCK 		4
int apu_pending_updates = 0;
bool between_cycles = false;
void clock_apu() {
	// called every apu cycle, 1 apu cycle = 2 cpu cycles
	apu_clock++;

	if (dmc.rate_counter == 0) {
		update_dmc();
		dmc.rate_counter = dmc.rate;
	}
	dmc.rate_counter--;

	if (!sequence_5step) {
		switch (apu_clock) {
			case 3728:
				apu_pending_updates |= APU_QUARTER_FRAME_CLOCK;
				break;
			case 7456:
				apu_pending_updates |= APU_QUARTER_FRAME_CLOCK;
				apu_pending_updates |= APU_HALF_FRAME_CLOCK;
				break;
			case 11185:
				apu_pending_updates |= APU_QUARTER_FRAME_CLOCK;
				break;
			case 14914:
				apu_frame_update();
				apu_pending_updates |= APU_QUARTER_FRAME_CLOCK;
				apu_pending_updates |= APU_HALF_FRAME_CLOCK;
				apu_pending_updates |= APU_FRAME_CLOCK;
				break;
			case 14915:
				apu_frame_update();
				apu_clock = 0;
				break;
		}
	}
	else {
		switch (apu_clock) {
			case 3728:
				apu_pending_updates |= APU_QUARTER_FRAME_CLOCK;
				break;
			case 7456:
				apu_pending_updates |= APU_QUARTER_FRAME_CLOCK;
				apu_pending_updates |= APU_HALF_FRAME_CLOCK;
				break;
			case 11185:
				apu_pending_updates |= APU_QUARTER_FRAME_CLOCK;
				break;
			case 18640:
				apu_pending_updates |= APU_QUARTER_FRAME_CLOCK;
				apu_pending_updates |= APU_HALF_FRAME_CLOCK;
				break;
			case 18641:
				apu_clock = 0;
				break;
		}
	}
}

void generate_samples(int clocks_passed) {
	float pulse_out, tnd_out;
	const float volume_mult = 2.0;

	for (int i = 0; i < clocks_passed; i++) {
		pulse_out = pulse_mixer_lookup[generate_pulse(&p1) + generate_pulse(&p2)];
		tnd_out = (generate_triangle() / 8227.0) + (generate_noise() / 12241.0) + (generate_dmc() / 22638.0);
		if (tnd_out != 0.0) {
			tnd_out = 159.79 / ((1.0 / tnd_out) + 100);
		}
		else {
			tnd_out = 0.0;
		}

		audio_buffer[audio_buffer_writing_pos++] = (pulse_out + tnd_out) * volume_mult;
		if (audio_buffer_writing_pos >= audio_buffer_size) {
			audio_buffer_writing_pos = 0;
		}
	}
}

void update_apu() {
	if (early_updated) {
		early_updated = false;
		return;
	}

	int cycles_to_clock = master_clock.cpu_cycles;
	master_clock.cpu_cycles = 0;
	int apu_clocks_passed = 0;

	while (cycles_to_clock >= 2) {
		if (apu_pending_updates != APU_NO_UPDATES) {
			process_apu_updates(apu_clocks_passed);
			apu_clocks_passed = 0;
		}
		clock_apu();
		apu_clocks_passed++;
		cycles_to_clock -= 2;
	}

	// cycles_to_clock should be 0 or 1
	if (between_cycles) {
		if (cycles_to_clock == 0) {
			if (apu_pending_updates != APU_NO_UPDATES) {
				process_apu_updates(apu_clocks_passed);
				apu_clocks_passed = 0;
			}
		}
		else { // 1 cycle
			clock_apu();
			apu_clocks_passed++;
			between_cycles = false;
		}
	}
	else {
		if (cycles_to_clock == 1) {
			if (apu_pending_updates != APU_NO_UPDATES) {
				process_apu_updates(apu_clocks_passed);
				apu_clocks_passed = 0;
			}
			between_cycles = true;
		}
	}

	generate_samples(apu_clocks_passed);
}

void process_apu_updates(int apu_clocks) {
	// catch the apu up to the current sample before changing things
	generate_samples(apu_clocks);

	if (apu_pending_updates & APU_QUARTER_FRAME_CLOCK) {
		apu_quarter_frame_update();
		apu_pending_updates &= ~APU_QUARTER_FRAME_CLOCK;
	}

	if (apu_pending_updates & APU_HALF_FRAME_CLOCK) {
		apu_half_frame_update();
		apu_pending_updates &= ~APU_HALF_FRAME_CLOCK;
	}

	if (apu_pending_updates & APU_FRAME_CLOCK) {
		apu_frame_update();
		apu_pending_updates &= ~APU_FRAME_CLOCK;
	}
}

void early_update_apu() {
	update_apu();
	early_updated = true;
}

void half_frame_pulse_update(Pulse *p) {
	if (!p->halt_length) {
		if (p->length != 0)
			p->length--;
	}

	if (p->sweep_var & 0x80) {
		p->sweep_divider--;
		if (p->sweep_divider < 0) {
			p->sweep_divider = (p->sweep_var >> 4) & 0x7;
			int change_amount = p->length >> (p->sweep_var & 0x7);
			if (p->sweep_var & 0x8)
				change_amount = -change_amount;
			p->timer += change_amount;
			if (p->timer >= 0x7ff || p->timer < 8)
				p->length = 0;

		}
		if (p->sweep_divider < 0 || p->sweep_reload)
			p->sweep_reload = false;
	}
}

void clock_env(Envelope *env, int volume) {
	if (!env->envelope_start) {
        env->envelope_divider--;
        if (env->envelope_divider < 0) {
            env->envelope_divider = volume;
            if (env->envelope_volume > 0)
                env->envelope_volume--;
            else if (env->loop)
                env->envelope_volume = 15;
        }
    }
    else {
        env->envelope_start = false;
        env->envelope_volume = 15;
        env->envelope_divider = volume;
    }

}

void apu_frame_update() {
	if (!disable_interupts) {
		frame_interupt_flag = true; 
		generate_irq |= apu_irq;
	}
}

void apu_half_frame_update() {
	half_frame_pulse_update(&p1);
	half_frame_pulse_update(&p2);
	if (!tri.control) {
		if (tri.length != 0)
			tri.length--;
	}
	if (!noise.halt_length) {
		if (noise.length != 0)
			noise.length--;
	}
}

void apu_quarter_frame_update() {
	clock_env(&p1.env, p1.volume);
	clock_env(&p2.env, p2.volume);
	clock_env(&noise.env, noise.volume);

	if (tri.reload) {
		tri.linear_counter = tri.reload_value;
	} 
	else if (tri.linear_counter > 0) {
		tri.linear_counter--;
	}
	if (!tri.control) {
		tri.reload = false;
	}
}

uint8_t length_lookup[] = {10,254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
						   12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

int noise_period_lookup[] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};

int dmc_rate_lookup[] = {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54};

void apu_poke(uint16_t address, uint8_t value) {
	early_update_apu();
	switch (address & 0x1F) {
		case 0x0:
			p1.duty = (value >> 6) & 0x3;
			p1.volume = value & 0xF;
			p1.env.constant_volume = value & 0x10 ? true : false;
			p1.halt_length = value & 0x20 ? true : false;
			p1.env.loop = p1.halt_length;
			break;
		case 0x1:
			p1.sweep_var = value;
			p1.sweep_reload = true;
			break;
		case 0x2:
			p1.timer = (p1.timer & 0x0700) | value;
			break;
		case 0x3:
			p1.timer = (p1.timer & 0x00FF) | ((value & 0x7) << 8);
			if (!p1.halt)
				p1.length = length_lookup[value >> 3];
			p1.env.envelope_start = true;
			break;
		case 0x4:
			p2.duty = (value >> 6) & 0x3;
			p2.volume = value & 0xF;
			p2.env.constant_volume = value & 0x10 ? true : false;
			p2.halt_length = value & 0x20 ? true : false;
			p2.env.loop = p2.halt_length;
			break;
		case 0x5:
			p2.sweep_var = value;
			p2.sweep_reload = true;
			break;
		case 0x6:
			p2.timer = (p2.timer & 0x0700) | value;
			break;
		case 0x7:
			p2.timer = (p2.timer & 0x00FF) | ((value & 0x7) << 8);
			if (!p2.halt)
				p2.length = length_lookup[value >> 3];
			p2.env.envelope_start = true;
			break;
		case 0x8:
			tri.control = value & 0x80 ? true : false;
			tri.reload_value = value & 0x7F;
			break;
		case 0xA:
			tri.timer = (tri.timer & 0x0700) | value;
			break;
		case 0xB:
			tri.timer = (tri.timer & 0x00FF) | ((value & 0x7) << 8);
			if (!tri.halt)
				tri.length = length_lookup[value >> 3];
			tri.reload = true;
			break;
		case 0xC:
			noise.halt_length = value & 0x20 ? true : false;
			noise.env.loop = noise.halt_length;
			noise.env.constant_volume = value & 0x10 ? true : false;
			noise.volume = value & 0xF;
			break;
		case 0xE:
			noise.mode = value & 0x80 ? true : false;
			noise.period = noise_period_lookup[value & 0xF];
			break;
		case 0xF:
			if (!noise.halt)
				noise.length = length_lookup[value >> 3];
			noise.env.envelope_start = true;
			break;
		case 0x10:
			dmc.irq_enabled = value & 0x80 ? true : false;
			if (!dmc.irq_enabled) {
				dmc_interupt_flag = false;
				generate_irq &= ~dmc_irq;
			}
			dmc.loop_flag = value & 0x40 ? true : false;
			dmc.rate = (dmc_rate_lookup[value & 0xF] / 2); // divide by 2 to go from cpu to apu cycles
			break;
		case 0x11:
			dmc.output = value & 0x7F;
			break;
		case 0x12:
			dmc.sample_addr = 0xC000 + (value * 64);
			break;
		case 0x13:
			dmc.sample_length = (value * 16) + 1;
			break;
		case 0x15:
			p1.halt = value & 0x1 ? false : true;
			if ((value & 0x1) == 0)
				p1.length = 0;
			p2.halt = value & 0x2 ? false : true;
			if ((value & 0x2) == 0)
				p2.length = 0;
			tri.halt = value & 0x4 ? false : true;
			if ((value & 0x4) == 0)
				tri.length = 0;
			noise.halt = value & 0x8 ? false : true;
			if ((value & 0x8) == 0)
				noise.length = 0;

			dmc.halt = value & 0x10 ? false : true;
			if ((value & 0x10) == 0)
				dmc.bytes_left = 0;
			else if (dmc.bytes_left == 0) {
				dmc.bytes_left = dmc.sample_length;
				dmc.cur_addr = dmc.sample_addr;
			}
			dmc_interupt_flag = false;
			generate_irq &= ~dmc_irq;
			break;
		case 0x17:
			// MIxx xxxx
			disable_interupts = value & 0x40 ? true : false;
			if (disable_interupts) {
				frame_interupt_flag = false;
				generate_irq &= ~apu_irq;
			}
			sequence_5step = value & 0x80 ? true : false;
			if (sequence_5step) {
				apu_quarter_frame_update();
				apu_half_frame_update();
			}

			apu_clock = -2;
			break;
	}
}

uint8_t get_apu_status() {
	uint8_t out = 0;

	if (p1.length >= 1)
		out |= 0x1;
	if (p2.length >= 1)
		out |= 0x2;
	if (tri.length >= 1)
		out |= 0x4;
	if (noise.length >= 1)
		out |= 0x8;
	if (dmc.bytes_left >= 1)
		out |= 0x10;

	if (frame_interupt_flag) {
		out |= 0x40;
		frame_interupt_flag = false;
	}
	if (dmc_interupt_flag) {
		out |= 0x80;
	}

	generate_irq &= ~apu_irq;

	return out;
}



