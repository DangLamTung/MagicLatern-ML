CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra

all: filmsim_sim devsim

filmsim_sim: filmsim_sim.c ../../fs_core.h
	$(CC) $(CFLAGS) -o $@ filmsim_sim.c

# raw develop harness: same core, but it starts from Bayer data
devsim: devsim.c ../../fs_core.h
	$(CC) $(CFLAGS) -o $@ devsim.c -lm

run: filmsim_sim
	mkdir -p out
	./filmsim_sim out
	python3 ppm2png.py out

# raw develop path on a real photograph: PPM in, one TIFF per look out
devrun: devsim
	mkdir -p dev_out
	./devsim input.ppm dev_out

clean:
	rm -rf filmsim_sim devsim out dev_out

.PHONY: all run devrun clean
