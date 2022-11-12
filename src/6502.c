#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "6502.h"
#include "memory.h"
#include "control.h"

// dumps the entire instuction flow for debuging
#define DEBUG 0
// adds extra dummy reads and writes to improve accuracy slightly at the cost of performance
#define HIGH_ACCURACY 1

uint8_t a;  // a Register (8-bit)
uint8_t x;  // x Register (8-bit)
uint8_t y;  // y Register (8-bit)
uint16_t pc;  // Program Counter (16-bit)
uint8_t s;  // Stack Pointer (8-bit)

// processor status (8-bit)
uint8_t c;  // carry
uint8_t z;  // zero
uint8_t i;  // interrupt
uint8_t d;  // decimal
uint8_t b;  // break
// unused bit here
uint8_t v;  // overflow
uint8_t n;  // negative


uint16_t address = 0;

void init_cpu() {
    get_opcodes();

    // registers
    a = 0;
    x = 0;
    y = 0;
    s = 0xFD;
    // set program counter to reset vector
    pc = peek2(0xFFFC);

    // flags
    c = 0;
    z = 0;
    i = 1;
    d = 0;
    b = 0;
    v = 0;
    n = 0;

}

void step() {
    Instruction *opcode = &opcodes[peek(pc)];
#if DEBUG
    // this check is not really necessary because all instructions have been filled in (including illegal/undocumented)
    if (opcode->function == NULL) {
        printf("invalid instruction at pc = 0x%x, (0x%x)\n", pc, peek(pc));
        //pc++;
        //return 0;
        exit(0);
    }

    if (pc == 0xD0C6) {
        printf("rng update -- %02x\n", peek(0x4A));
       // printf("%s   s: 0x1%x, pc: 0x%x, cycles: %d\n", opcode->debug_name, s, pc, opcode->cycles);
    }
#endif

    add_cpu_cycles(opcode->cycles);
    opcode->get_address();

    opcode->function();

    pc += 1;
}

// helper functions
void set_n_and_z(uint8_t value) {  // n and z are always set together
    z = !value;
    n = value >> 7;
}

uint8_t status_to_int() {
    uint8_t out = 
        (c & 1) << 0 |
        (z & 1) << 1 |
        (i & 1) << 2 |
        (d & 1) << 3 |
        (b & 1) << 4 |
        (1 & 1) << 5 |
        (v & 1) << 6 |
        (n & 1) << 7;
    return out;
}

void set_status(uint8_t value) {
    c = value & 0x1;
    z = (value >> 1) & 0x1;
    i = (value >> 2) & 0x1;
    d = (value >> 3) & 0x1;
    b = (value >> 4) & 0x1;
    // unused bit here
    v = (value >> 6) & 0x1;
    n = (value >> 7) & 0x1;
}

void branch(uint16_t addr) {
    uint16_t new;
    uint8_t value = peek(addr);

    if (value & 0x80) {
        new = pc + (value - 0x100);
    } else {
        new = pc + value;
    }

    if (((pc+1) & 0xF00) != ((new+1) & 0xF00)) {
        add_cpu_cycles(2);
    } else {
        add_cpu_cycles(1);
    }
    pc = new;
}

uint8_t peek_stack() {
    s = (s + 1) & 0xFF;
    return peek(0x0100 | s);
}

void poke_stack(uint8_t value) {
    poke((0x0100 | (s & 0xFF)), value);
    s = (s - 1) & 0xFF;
}

// interupts
void reset() { // soft reset from console button
    printf("reset\n");
    add_cpu_cycles(7);
    
    s -= 3;
    pc = peek2(0xFFFC);

    i = 1;
}

void nmi() {
    add_cpu_cycles(7);
    poke_stack((pc >> 8) & 0xFF);
    poke_stack(pc & 0xFF);

    poke_stack(status_to_int());

    i = 1;

    pc = peek2(0xFFFA);
}

void irq() {
    if (!i) {
        //printf("irq\n");
        add_cpu_cycles(7);
        poke_stack((pc >> 8) & 0xFF);
        poke_stack(pc & 0xFF);

        poke_stack(status_to_int());

        i = 1;

        pc = peek2(0xFFFE);
    }
}

// address functions
void implied() {
#if HIGH_ACCURACY
    peek(pc + 1); // dummy read
#endif
}

void immediate() {
    pc += 1;
    address = pc;
}

void zero_page() {
    pc += 1;
    address = peek(pc);
}

void zero_page_x() {
    pc += 1;
    address = (peek(pc) + x) & 0xFF;
}

void zero_page_y() {
    pc += 1;
    address = (peek(pc) + y) & 0xFF;
}

void absolute() {
    pc += 2;
    address = peek2(pc - 1);
}

void absolute_x() {
    pc += 2;
#if HIGH_ACCURACY
    uint16_t tmp = peek2(pc - 1);
    address = (tmp + x) & 0xFFFF;
    peek((tmp & 0xFF00) | (address & 0xFF));
#else
    address = (peek2(pc - 1) + x) & 0xFFFF;
#endif
}

void delayed_absolute_x() {
    pc += 2;
    uint16_t tmp = peek2(pc - 1);
    address = (tmp + x) & 0xFFFF;
    if ((tmp & 0xF00) != (address & 0xF00)) {
#if HIGH_ACCURACY
        peek((tmp & 0xFF00) | (address & 0xFF));
#endif
        add_cpu_cycles(1);
    }
}

void absolute_y() {
    pc += 2;
#if HIGH_ACCURACY
    uint16_t tmp = peek2(pc - 1);
    address = (tmp + y) & 0xFFFF;
    peek((tmp & 0xFF00) | (address & 0xFF));
#else
    address = (peek2(pc - 1) + y) & 0xFFFF;
#endif
}

void delayed_absolute_y() {
    pc += 2;
    uint16_t tmp = peek2(pc - 1);
    address = (tmp + y) & 0xFFFF;
    if ((tmp & 0xF00) != (address & 0xF00)) {
#if HIGH_ACCURACY
        peek((tmp & 0xFF00) | (address & 0xFF));
#endif
        add_cpu_cycles(1);
    }
}

void indirect() {
    pc += 2;
    uint16_t first_addr = peek2(pc - 1);
    // simulate page wrap around
    uint16_t second_addr = (first_addr & 0xFF00) | ((first_addr + 1) & 0xFF);
    address = peek(first_addr) | (peek(second_addr) << 8);
}

void indirect_x() {
    pc += 1;
    uint8_t first_addr = peek(pc) + x;
    // simulate page wrap around
    address = peek(first_addr) | (peek((first_addr + 1) & 0xFF) << 8);
}

void indirect_y() {
    pc += 1;
    uint8_t first_addr = peek(pc);
#if HIGH_ACCURACY
    uint16_t tmp = peek(first_addr) | (peek((first_addr + 1) & 0xFF) << 8);
    address = (tmp + y) & 0xFFFF;
    if ((address & 0xF00) != (tmp & 0xF00))
        peek((tmp & 0xFF00) | (address & 0xFF));
#else
    // simulate page wrap around
    address = (peek(first_addr) | (peek((first_addr + 1) & 0xFF) << 8)) + y;
#endif
}

void delayed_indirect_y() {
    pc += 1;
    uint8_t first_addr = peek(pc);
    // simulate page wrap around
    uint16_t tmp = peek(first_addr) | (peek((first_addr + 1) & 0xFF) << 8);
    address = (tmp + y) & 0xFFFF;
    if ((tmp & 0xF00) != (address & 0xF00)) {
#if HIGH_ACCURACY
        peek((tmp & 0xFF00) | (address & 0xFF));
#endif
        add_cpu_cycles(1);
    }
}

// Instructions

void adc() {
    uint8_t value = peek(address);
    uint16_t r = value + a + c;
    v = (~(a ^ value) & (a ^ r) & 0x80) ? true : false;  // overflow
    c = (r & 0x100) == 0x100;  // carry;
    a = r & 0xFF;
    set_n_and_z(a);
}

void and() {
    a &= peek(address);
    set_n_and_z(a);
}

void asl_acc() {
    c = (a & 0x80) == 0x80;
    a <<= 1;
    set_n_and_z(a);
}

void asl() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    c = (value & 0x80) == 0x80;
    value <<= 1;
    poke(address, value);
    set_n_and_z(value);
}

void bit() {
    uint8_t value = peek(address);
    n = (value & 0x80) == 0x80;
    v = (value & 0x40) == 0x40;
    z = !(a & value);
}

void bcc() {
    if (!c)
        branch(address);
}

void bcs() {
    if (c)
        branch(address);
}

void beq() {
    if (z)
        branch(address);
}

void bmi() {
    if (n)
        branch(address);
}

void bne() {
    if (!z)
        branch(address);
}

void bpl() {
    if (!n)
        branch(address);
}

void bvc() {
    if (!v)
        branch(address);
}

void bvs() {
    if (v)
        branch(address);
}

void BRK() {
    pc += 2;
    poke_stack((pc >> 8) & 0xFF);
    poke_stack(pc & 0xFF);

    b = 1;
    poke_stack(status_to_int());
    b = 0;

    i = 1;

    pc = peek2(0xFFFE) - 1;
}

void clc() {
    c = 0;
}

void cld() {
    d = 0;
}

void cli() {
    i = 0;
}

void clv() {
    v = 0;
}

void cmp() {
    int value = a - peek(address);
    c = value >= 0;
    set_n_and_z(value & 0xFF);
}

void cpx() {
    int value = x - peek(address);
    c = value >= 0;
    set_n_and_z(value & 0xFF);
}

void cpy() {
    int value = y - peek(address);
    c = value >= 0;
    set_n_and_z(value & 0xFF);
}

void dec() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    poke(address, --value);
    set_n_and_z(value);
}

void dex() {
    x--;
    set_n_and_z(x);
}

void dey() {
    y--;
    set_n_and_z(y);
}

void eor() {
    a ^= peek(address);
    set_n_and_z(a);
}

void inc() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    poke(address, ++value);
    set_n_and_z(value);
}

void inx() {
    x++;
    set_n_and_z(x);
}

void iny() {
    y++;
    set_n_and_z(y);
}

void jmp() {
    pc = address - 1;
}

void jsr() {
    poke_stack((pc >> 8) & 0xFF);
    poke_stack(pc & 0xFF);

    pc = address - 1;
}

void lda() {
    a = peek(address);
    set_n_and_z(a);
}

void ldx() {
    x = peek(address);
    set_n_and_z(x);
}

void ldy() {
    y = peek(address);
    set_n_and_z(y);
}

void lsr_acc() {
    c = a & 0x1;
    a >>= 1;
    set_n_and_z(a);
}

void lsr() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    c = value & 0x1;
    value >>= 1;
    poke(address, value);
    set_n_and_z(value);
}

void nop() {
}

void ora() {
    a |= peek(address);
    set_n_and_z(a);
}

void pha() {
    poke_stack(a);
}

void php() {
    b = 1;
    poke_stack(status_to_int());
    b = 0;
}

void pla() {
    a = peek_stack();
    set_n_and_z(a);
}

void plp() {
    set_status(peek_stack());
}

void rol_acc() {
    uint8_t tmp = (a & 0x80) >> 7;
    a = (a << 1 | c) & 0xFF;
    c = tmp;
    set_n_and_z(a);
}

void rol() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    uint8_t tmp = (value & 0x80) >> 7;
    value = (value << 1 | c) & 0xFF;
    poke(address, value);
    c = tmp;
    set_n_and_z(value);
}

void ror_acc() {
    uint8_t tmp = a & 0x1;
    a = (c << 7) | a >> 1;
    c = tmp;
    set_n_and_z(a);
}

void ror() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    uint8_t tmp = value & 0x1;
    value = (c << 7) | value >> 1;
    poke(address, value);
    c = tmp;
    set_n_and_z(value);
}

void rti() {
    set_status(peek_stack());

    pc = peek_stack() | (peek_stack() << 8);
    pc -= 1;
}

void rts() {
    pc = peek_stack() | (peek_stack() << 8);
}

void sbc() {
    uint8_t value = peek(address);
    uint16_t r = a - value - (!c);
    v = ((a ^ value) & (a ^ r) & 0x80) ? true : false;
    c = !(r & 0x100);

    a = r & 0xFF;
    set_n_and_z(a);
}

void sec() {
    c = 1;
}

void sed() {
    d = 1;
}

void sei() {
    i = 1;
}

void sta() {
    poke(address, a);
}

void stx() {
    poke(address, x);
}

void sty() {
    poke(address, y);
}

void tax() {
    x = a;
    set_n_and_z(a);
}

void tay() {
    y = a;
    set_n_and_z(a);
}

void tsx() {
    x = s;
    set_n_and_z(s);
}

void txa() {
    a = x;
    set_n_and_z(x);
}

void txs() {
    s = x;
}

void tya() {
    a = y;
    set_n_and_z(y);

}


// undocumented opcodes
void aac() {
    uint8_t value = peek(address);
    a &= value;
    c = (a & 0x80) == 0x80;
    set_n_and_z(a);
}

void aax() {
    poke(address, a & x);
}

void arr() {
    uint8_t value = peek(address);
    a &= value;
    a = ((a >> 1) & 0x7F) | (c << 7);
    c = a & 0x40 ? true : false;
    v = ((a & 0x40) ^ ((a & 0x20) << 1)) ? true : false;
    set_n_and_z(a);
}

void asr() {
    uint8_t value = peek(address);
    a &= value;
    c = a & 0x1;
    a >>= 1;
    set_n_and_z(a);
}

void atx() {
    a = x = peek(address);
    set_n_and_z(a);
}

void axa() {
    printf("unimplemented unstable instruction!\n");
}

void axs() {
    uint8_t value = peek(address);
    uint16_t tmp = (x & a) - value;
    x = tmp & 0xFF;
    c = (tmp & 0x100) ? false : true;
    set_n_and_z(x);
}

void dcp() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    poke(address, --value);

    int value2 = a - value;
    c = value2 >= 0;
    set_n_and_z(value2 & 0xFF);
}

void isc() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    poke(address, ++value);

    uint16_t r = a - value - (!c);
    v = ((a ^ value) & (a ^ r) & 0x80) ? true : false;
    c = !(r & 0x100);

    a = r & 0xFF;
    set_n_and_z(a);
}

void kil() {
    printf("cpu halted!\n");
    exit(0);
}

void lar() {
    printf("unimplemented unstable instruction!\n");
}

void lax() {
    uint8_t value = peek(address);
    x = value;
    a = value;
    set_n_and_z(value);
}

void rla() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    uint8_t tmp = (value & 0x80) >> 7;
    value = (value << 1 | c) & 0xFF;
    poke(address, value);
    c = tmp;
    a &= value;
    set_n_and_z(a);
}

void rra() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    uint8_t tmp = value & 0x1;
    value = (c << 7) | value >> 1;
    poke(address, value);
    c = tmp;

    uint16_t r = value + a + c;
    v = (~(a ^ value) & (a ^ r) & 0x80) ? true : false;  // overflow
    c = (r & 0x100) == 0x100;  // carry;
    a = r & 0xFF;
    set_n_and_z(a);
}

void slo() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    c = (value & 0x80) == 0x80;
    value <<= 1;
    poke(address, value);
    a |= value;
    set_n_and_z(a);
}

void sre() {
    uint8_t value = peek(address);
#if HIGH_ACCURACY
    poke(address, value); // dummy write
#endif
    c = value & 0x1;
    value >>= 1;
    poke(address, value);
    a ^= value;
    set_n_and_z(a);
}

void sxa() {
    uint16_t tmp = peek2(pc - 1);

    tmp = (tmp & 0xFF00) | (address & 0x00FF);
    if ((tmp & 0xF00) != (address & 0xF00)) {
        tmp &= x << 8;
    }

    poke(tmp, x & ((tmp >> 8) + 1)); 
}

void sya() {
    uint16_t tmp = peek2(pc - 1);

    tmp = (tmp & 0xFF00) | (address & 0x00FF);
    if ((tmp & 0xF00) != (address & 0xF00)) {
        tmp &= y << 8;
    }

    poke(tmp, y & ((tmp >> 8) + 1)); 
}

void xaa() {
    printf("unimplemented unstable instruction!\n");
}

void xas() {
    printf("unimplemented unstable instruction!\n");
}


// fill the opcode table
void new_instruction(int index, void *function, int cycles, void *get_address, char *name) {
    opcodes[index].function = function;
    opcodes[index].cycles = cycles;
    opcodes[index].get_address = get_address;
    opcodes[index].debug_name = name;
}

void get_opcodes() {
	new_instruction(0x69, adc, 2, immediate, "adc");
	new_instruction(0x65, adc, 3, zero_page, "adc");
	new_instruction(0x75, adc, 4, zero_page_x, "adc");
	new_instruction(0x6D, adc, 4, absolute, "adc");
	new_instruction(0x7D, adc, 4, delayed_absolute_x, "adc");
	new_instruction(0x79, adc, 4, delayed_absolute_y, "adc");
	new_instruction(0x61, adc, 6, indirect_x, "adc");
	new_instruction(0x71, adc, 5, delayed_indirect_y, "adc");

	new_instruction(0x29, and, 2, immediate, "and");
	new_instruction(0x25, and, 3, zero_page, "and");
	new_instruction(0x35, and, 4, zero_page_x, "and");
	new_instruction(0x2D, and, 4, absolute, "and");
	new_instruction(0x3D, and, 4, delayed_absolute_x, "and");
	new_instruction(0x39, and, 4, delayed_absolute_y, "and");
	new_instruction(0x21, and, 6, indirect_x, "and");
	new_instruction(0x31, and, 5, delayed_indirect_y, "and");

	new_instruction(0x0A, asl_acc, 2, implied, "asl");  // uses accumulator instead of address;
	new_instruction(0x06, asl, 5, zero_page, "asl");
	new_instruction(0x16, asl, 6, zero_page_x, "asl");
	new_instruction(0x0E, asl, 6, absolute, "asl");
	new_instruction(0x1E, asl, 7, absolute_x, "asl");

	new_instruction(0x24, bit, 3, zero_page, "bit");
	new_instruction(0x2C, bit, 4, absolute, "bit");

	new_instruction(0x10, bpl, 2, immediate, "bpl");
	new_instruction(0x30, bmi, 2, immediate, "bmi");
	new_instruction(0x50, bvc, 2, immediate, "bvc");
	new_instruction(0x70, bvs, 2, immediate, "bvs");
	new_instruction(0x90, bcc, 2, immediate, "bcc");
	new_instruction(0xB0, bcs, 2, immediate, "bcs");
	new_instruction(0xD0, bne, 2, immediate, "bne");
	new_instruction(0xF0, beq, 2, immediate, "beq");

	new_instruction(0x00, BRK, 7, implied, "brk");

	new_instruction(0xC9, cmp, 2, immediate, "cmp");
	new_instruction(0xC5, cmp, 3, zero_page, "cmp");
	new_instruction(0xD5, cmp, 4, zero_page_x, "cmp");
	new_instruction(0xCD, cmp, 4, absolute, "cmp");
	new_instruction(0xDD, cmp, 4, delayed_absolute_x, "cmp");
	new_instruction(0xD9, cmp, 4, delayed_absolute_y, "cmp");
	new_instruction(0xC1, cmp, 6, indirect_x, "cmp");
	new_instruction(0xD1, cmp, 5, delayed_indirect_y, "cmp");

	new_instruction(0xE0, cpx, 2, immediate, "cpx");
	new_instruction(0xE4, cpx, 3, zero_page, "cpx");
	new_instruction(0xEC, cpx, 4, absolute, "cpx");

	new_instruction(0xC0, cpy, 2, immediate, "cpy");
	new_instruction(0xC4, cpy, 3, zero_page, "cpy");
	new_instruction(0xCC, cpy, 4, absolute, "cpy");

	new_instruction(0xC6, dec, 5, zero_page, "dec");
	new_instruction(0xD6, dec, 6, zero_page_x, "dec");
	new_instruction(0xCE, dec, 6, absolute, "dec");
	new_instruction(0xDE, dec, 7, absolute_x, "dec");

	new_instruction(0x49, eor, 2, immediate, "eor");
	new_instruction(0x45, eor, 3, zero_page, "eor");
	new_instruction(0x55, eor, 4, zero_page_x, "eor");
	new_instruction(0x4D, eor, 4, absolute, "eor");
	new_instruction(0x5D, eor, 4, delayed_absolute_x, "eor");
	new_instruction(0x59, eor, 4, delayed_absolute_y, "eor");
	new_instruction(0x41, eor, 6, indirect_x, "eor");
	new_instruction(0x51, eor, 5, delayed_indirect_y, "eor");

	new_instruction(0x18, clc, 2, implied, "clc");
	new_instruction(0xD8, cld, 2, implied, "cld");
	new_instruction(0x58, cli, 2, implied, "cli");
	new_instruction(0xB8, clv, 2, implied, "clv");
	new_instruction(0x38, sec, 2, implied, "sec");
	new_instruction(0x78, sei, 2, implied, "sei");
	new_instruction(0xF8, sed, 2, implied, "sed");

	new_instruction(0xE6, inc, 5, zero_page, "inc");
	new_instruction(0xF6, inc, 6, zero_page_x, "inc");
	new_instruction(0xEE, inc, 6, absolute, "inc");
	new_instruction(0xFE, inc, 7, absolute_x, "inc");

	new_instruction(0x4C, jmp, 3, absolute, "jmp");
	new_instruction(0x6C, jmp, 5, indirect, "jmp");

	new_instruction(0x20, jsr, 6, absolute, "jsr");

	new_instruction(0xA9, lda, 2, immediate, "lda");
	new_instruction(0xA5, lda, 3, zero_page, "lda");
	new_instruction(0xB5, lda, 4, zero_page_x, "lda");
	new_instruction(0xAD, lda, 4, absolute, "lda");
	new_instruction(0xBD, lda, 4, delayed_absolute_x, "lda");
	new_instruction(0xB9, lda, 4, delayed_absolute_y, "lda");
	new_instruction(0xA1, lda, 6, indirect_x, "lda");
	new_instruction(0xB1, lda, 5, delayed_indirect_y, "lda");

	new_instruction(0xA2, ldx, 2, immediate, "ldx");
	new_instruction(0xA6, ldx, 3, zero_page, "ldx");
	new_instruction(0xB6, ldx, 4, zero_page_y, "ldx");
	new_instruction(0xAE, ldx, 4, absolute, "ldx");
	new_instruction(0xBE, ldx, 4, delayed_absolute_y, "ldx");

	new_instruction(0xA0, ldy, 2, immediate, "ldy");
	new_instruction(0xA4, ldy, 3, zero_page, "ldy");
	new_instruction(0xB4, ldy, 4, zero_page_x, "ldy");
	new_instruction(0xAC, ldy, 4, absolute, "ldy");
	new_instruction(0xBC, ldy, 4, delayed_absolute_x, "ldy");

	new_instruction(0x4A, lsr_acc, 2, implied, "lsr");  // uses accumulator instead of address;
	new_instruction(0x46, lsr, 5, zero_page, "lsr");
	new_instruction(0x56, lsr, 6, zero_page_x, "lsr");
	new_instruction(0x4E, lsr, 6, absolute, "lsr");
	new_instruction(0x5E, lsr, 7, absolute_x, "lsr");

	new_instruction(0xEA, nop, 2, implied, "nop");

	new_instruction(0x09, ora, 2, immediate, "ora");
	new_instruction(0x05, ora, 3, zero_page, "ora");
	new_instruction(0x15, ora, 4, zero_page_x, "ora");
	new_instruction(0x0D, ora, 4, absolute, "ora");
	new_instruction(0x1D, ora, 4, delayed_absolute_x, "ora");
	new_instruction(0x19, ora, 4, delayed_absolute_y, "ora");
	new_instruction(0x01, ora, 6, indirect_x, "ora");
	new_instruction(0x11, ora, 5, delayed_indirect_y, "ora");

	new_instruction(0xAA, tax, 2, implied, "tax");
	new_instruction(0x8A, txa, 2, implied, "txa");
	new_instruction(0xCA, dex, 2, implied, "dex");
	new_instruction(0xE8, inx, 2, implied, "inx");
	new_instruction(0xA8, tay, 2, implied, "tay");
	new_instruction(0x98, tya, 2, implied, "tya");
	new_instruction(0x88, dey, 2, implied, "dey");
	new_instruction(0xC8, iny, 2, implied, "iny");

	new_instruction(0x2A, rol_acc, 2, implied, "rol");  // uses accumulator instead of address;
	new_instruction(0x26, rol, 5, zero_page, "rol");
	new_instruction(0x36, rol, 6, zero_page_x, "rol");
	new_instruction(0x2E, rol, 6, absolute, "rol");
	new_instruction(0x3E, rol, 7, absolute_x, "rol");

	new_instruction(0x6A, ror_acc, 2, implied, "ror");  // uses accumulator instead of address;
	new_instruction(0x66, ror, 5, zero_page, "ror");
	new_instruction(0x76, ror, 6, zero_page_x, "ror");
	new_instruction(0x6E, ror, 6, absolute, "ror");
	new_instruction(0x7E, ror, 7, absolute_x, "ror");

	new_instruction(0x40, rti, 6, implied, "rti");

	new_instruction(0x60, rts, 6, implied, "rts");

	new_instruction(0xE9, sbc, 2, immediate, "sbc");
	new_instruction(0xE5, sbc, 3, zero_page, "sbc");
	new_instruction(0xF5, sbc, 4, zero_page_x, "sbc");
	new_instruction(0xED, sbc, 4, absolute, "sbc");
	new_instruction(0xFD, sbc, 4, delayed_absolute_x, "sbc");
	new_instruction(0xF9, sbc, 4, delayed_absolute_y, "sbc");
	new_instruction(0xE1, sbc, 6, indirect_x, "sbc");
	new_instruction(0xF1, sbc, 5, delayed_indirect_y, "sbc");

	new_instruction(0x85, sta, 3, zero_page, "sta");
	new_instruction(0x95, sta, 4, zero_page_x, "sta");
	new_instruction(0x8D, sta, 4, absolute, "sta");
	new_instruction(0x9D, sta, 5, absolute_x, "sta");
	new_instruction(0x99, sta, 5, absolute_y, "sta");
	new_instruction(0x81, sta, 6, indirect_x, "sta");
	new_instruction(0x91, sta, 6, indirect_y, "sta");

	new_instruction(0x9A, txs, 2, implied, "txs");
	new_instruction(0xBA, tsx, 2, implied, "tsx");
	new_instruction(0x48, pha, 3, implied, "pha");
	new_instruction(0x68, pla, 4, implied, "pla");
	new_instruction(0x08, php, 3, implied, "php");
	new_instruction(0x28, plp, 4, implied, "plp");

	new_instruction(0x86, stx, 3, zero_page, "stx");
	new_instruction(0x96, stx, 4, zero_page_y, "stx");
	new_instruction(0x8E, stx, 4, absolute, "stx");

	new_instruction(0x84, sty, 3, zero_page, "sty");
	new_instruction(0x94, sty, 4, zero_page_x, "sty");
	new_instruction(0x8C, sty, 4, absolute, "sty");


    // undocumented opcodes
    new_instruction(0x0B, aac, 2, immediate, "aac");
    new_instruction(0x2B, aac, 2, immediate, "aac");

    new_instruction(0x87, aax, 3, zero_page, "aax");
    new_instruction(0x97, aax, 4, zero_page_y, "aax");
    new_instruction(0x83, aax, 6, indirect_x, "aax");
    new_instruction(0x8F, aax, 4, absolute, "aax");

    new_instruction(0x6B, arr, 2, immediate, "arr");

    new_instruction(0x4B, asr, 2, immediate, "asr");

    new_instruction(0xAB, atx, 2, immediate, "atx");

    new_instruction(0x9F, axa, 5, absolute_y, "axa");
    new_instruction(0x93, axa, 6, indirect_y, "axa");

    new_instruction(0xCB, axs, 2, immediate, "axs");

    new_instruction(0xC7, dcp, 5, zero_page, "dcp");
    new_instruction(0xD7, dcp, 6, zero_page_x, "dcp");
    new_instruction(0xCF, dcp, 6, absolute, "dcp");
    new_instruction(0xDF, dcp, 7, absolute_x, "dcp");
    new_instruction(0xDB, dcp, 7, absolute_y, "dcp");
    new_instruction(0xC3, dcp, 8, indirect_x, "dcp");
    new_instruction(0xD3, dcp, 8, indirect_y, "dcp");

    new_instruction(0x04, nop, 3, zero_page, "dop");
    new_instruction(0x14, nop, 4, zero_page_x, "dop");
    new_instruction(0x34, nop, 4, zero_page_x, "dop");
    new_instruction(0x44, nop, 3, zero_page, "dop");
    new_instruction(0x54, nop, 4, zero_page_x, "dop");
    new_instruction(0x64, nop, 3, zero_page, "dop");
    new_instruction(0x74, nop, 4, zero_page_x, "dop");
    new_instruction(0x80, nop, 2, immediate, "dop");
    new_instruction(0x82, nop, 2, immediate, "dop");
    new_instruction(0x89, nop, 2, immediate, "dop");
    new_instruction(0xC2, nop, 2, immediate, "dop");
    new_instruction(0xD4, nop, 4, zero_page_x, "dop");
    new_instruction(0xE2, nop, 2, immediate, "dop");
    new_instruction(0xF4, nop, 4, zero_page_x, "dop");

    new_instruction(0xE7, isc, 5, zero_page, "isc");
    new_instruction(0xF7, isc, 6, zero_page_x, "isc");
    new_instruction(0xEF, isc, 6, absolute, "isc");
    new_instruction(0xFF, isc, 7, absolute_x, "isc");
    new_instruction(0xFB, isc, 7, absolute_y, "isc");
    new_instruction(0xE3, isc, 8, indirect_x, "isc");
    new_instruction(0xF3, isc, 8, indirect_y, "isc");

    new_instruction(0x02, kil, 2, implied, "kil");
    new_instruction(0x12, kil, 2, implied, "kil");
    new_instruction(0x22, kil, 2, implied, "kil");
    new_instruction(0x32, kil, 2, implied, "kil");
    new_instruction(0x42, kil, 2, implied, "kil");
    new_instruction(0x52, kil, 2, implied, "kil");
    new_instruction(0x62, kil, 2, implied, "kil");
    new_instruction(0x72, kil, 2, implied, "kil");
    new_instruction(0x92, kil, 2, implied, "kil");
    new_instruction(0xB2, kil, 2, implied, "kil");
    new_instruction(0xD2, kil, 2, implied, "kil");
    new_instruction(0xF2, kil, 2, implied, "kil");

    new_instruction(0xBB, lar, 4, delayed_absolute_y, "lar");

    new_instruction(0xA7, lax, 3, zero_page, "lax");
    new_instruction(0xB7, lax, 4, zero_page_y, "lax");
    new_instruction(0xAF, lax, 4, absolute, "lax");
    new_instruction(0xBF, lax, 4, delayed_absolute_y, "lax");
    new_instruction(0xA3, lax, 6, indirect_x, "lax");
    new_instruction(0xB3, lax, 5, delayed_indirect_y, "lax");

    new_instruction(0x1A, nop, 2, implied, "nop");
    new_instruction(0x3A, nop, 2, implied, "nop");
    new_instruction(0x5A, nop, 2, implied, "nop");
    new_instruction(0x7A, nop, 2, implied, "nop");
    new_instruction(0xDA, nop, 2, implied, "nop");
    new_instruction(0xFA, nop, 2, implied, "nop");

    new_instruction(0x27, rla, 5, zero_page, "rla");
    new_instruction(0x37, rla, 6, zero_page_x, "rla");
    new_instruction(0x2F, rla, 6, absolute, "rla");
    new_instruction(0x3F, rla, 7, absolute_x, "rla");
    new_instruction(0x3B, rla, 7, absolute_y, "rla");
    new_instruction(0x23, rla, 8, indirect_x, "rla");
    new_instruction(0x33, rla, 8, indirect_y, "rla");

    new_instruction(0x67, rra, 5, zero_page, "rra");
    new_instruction(0x77, rra, 6, zero_page_x, "rra");
    new_instruction(0x6F, rra, 6, absolute, "rra");
    new_instruction(0x7F, rra, 7, absolute_x, "rra");
    new_instruction(0x7B, rra, 7, absolute_y, "rra");
    new_instruction(0x63, rra, 8, indirect_x, "rra");
    new_instruction(0x73, rra, 8, indirect_y, "rra");

    new_instruction(0xEB, sbc, 2, immediate, "sbc");

    new_instruction(0x07, slo, 5, zero_page, "slo");
    new_instruction(0x17, slo, 6, zero_page_x, "slo");
    new_instruction(0x0F, slo, 6, absolute, "slo");
    new_instruction(0x1F, slo, 7, absolute_x, "slo");
    new_instruction(0x1B, slo, 7, absolute_y, "slo");
    new_instruction(0x03, slo, 8, indirect_x, "slo");
    new_instruction(0x13, slo, 8, indirect_y, "slo");

    new_instruction(0x47, sre, 5, zero_page, "sre");
    new_instruction(0x57, sre, 6, zero_page_x, "sre");
    new_instruction(0x4F, sre, 6, absolute, "sre");
    new_instruction(0x5F, sre, 7, absolute_x, "sre");
    new_instruction(0x5B, sre, 7, absolute_y, "sre");
    new_instruction(0x43, sre, 8, indirect_x, "sre");
    new_instruction(0x53, sre, 8, indirect_y, "sre");

    new_instruction(0x9E, sxa, 5, absolute_y, "sxa");

    new_instruction(0x9C, sya, 5, absolute_x, "sya");

    new_instruction(0x0C, nop, 4, absolute, "top");
    new_instruction(0x1C, nop, 4, delayed_absolute_x, "top");
    new_instruction(0x3C, nop, 4, delayed_absolute_x, "top");
    new_instruction(0x5C, nop, 4, delayed_absolute_x, "top");
    new_instruction(0x7C, nop, 4, delayed_absolute_x, "top");
    new_instruction(0xDC, nop, 4, delayed_absolute_x, "top");
    new_instruction(0xFC, nop, 4, delayed_absolute_x, "top");

    new_instruction(0x8B, xaa, 2, immediate, "xaa");

    new_instruction(0x9B, xas, 5, absolute_y, "xas");
}
