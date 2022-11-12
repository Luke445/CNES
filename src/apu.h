#pragma once

void init_apu();

void reset_apu();

void mix_audio(void *unused, uint8_t *stream, int len);

void early_update_apu();

void update_apu();

void update_dmc();

void process_apu_updates();

void apu_frame_update();

void apu_half_frame_update();

void apu_quarter_frame_update();

void apu_poke(uint16_t address, uint8_t value);

uint8_t get_apu_status();
