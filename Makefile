CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -pthread
PKG_CONFIG ?= pkg-config
RAYLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags raylib)
RAYLIB_LIBS := $(shell $(PKG_CONFIG) --libs raylib)

TARGET := voxelcraft
SRC := src/main.c src/album.c src/inventory.c src/space.c src/ship.c src/nether.c src/entity.c src/terrain.c src/chunks.c src/world.c src/player.c src/interaction.c src/render.c src/particles.c src/audio.c src/weather.c src/starmap.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ $(SRC) $(RAYLIB_LIBS) -lm -pthread

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
