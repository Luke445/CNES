#pragma once
#include <inttypes.h>
#include <stdbool.h>

typedef void (*func)(void);

typedef struct Instruction {
	func function;
	int cycles;
	func get_address;
	char *debug_name;
} Instruction;


Instruction opcodes[256];


void init_cpu();

void step();

void nmi();

void irq();

void reset();

void get_opcodes();
