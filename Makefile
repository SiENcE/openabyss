# Builds ./openabyss from src/: C11 and SDL 3 (pkg-config sdl3).

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL3_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)
SRC     := $(wildcard src/uw_*.c) src/tools/uwshell.c
HEADERS := $(wildcard src/*.h) src/tools/uwshell.h
NAMES   := -DUW_PROGRAM='"openabyss"' -DUW_WINDOW_TITLE='"OpenAbyss"' -DUW_PREF_DIR='"openabyss"'

openabyss: $(SRC) $(HEADERS)
	@if [ -z "$(SDL3_LIBS)" ]; then echo "make: pkg-config finds no sdl3"; exit 1; fi
	$(CC) $(CFLAGS) -DUW_NO_HARNESS -DUW_NO_ACCOUNTING $(NAMES) $(SDL3_CFLAGS) $(SRC) -lm $(SDL3_LIBS) -o $@

clean:
	rm -f openabyss

.PHONY: clean
