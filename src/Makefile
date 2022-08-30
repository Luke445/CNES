CC = gcc
CFLAGS = -O2

cnes : main.o control.o memory.o 6502.o ppu.o apu.o m0.o m1.o m2.o m3.o m4.o m7.o m11.o
	$(CC) $(CFLAGS) -o ../cnes *.o -lSDL2
	rm *.o

main.o : main.c control.h
	$(CC) $(CFLAGS) -c main.c

control.o : control.c control.h memory.h ppu.h apu.h 6502.h
	$(CC) $(CFLAGS) -c control.c

memory.o : memory.c control.h memory.h ppu.h apu.h mappers/*.h
	$(CC) $(CFLAGS) -c memory.c

6502.o : 6502.c 6502.h memory.h control.h
	$(CC) $(CFLAGS) -c 6502.c

ppu.o : ppu.c ppu.h memory.h control.h
	$(CC) $(CFLAGS) -c ppu.c

apu.o : apu.c apu.h memory.h control.h
	$(CC) $(CFLAGS) -c apu.c

# builds all mappers
m%.o : mappers/m%.c mappers/m%.h memory.h control.h
	$(CC) $(CFLAGS) -c $<

clean :
	rm cnes *.o