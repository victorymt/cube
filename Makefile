CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -pthread
PKG_CONFIG ?= pkg-config
RAYLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags raylib)
RAYLIB_LIBS := $(shell $(PKG_CONFIG) --libs raylib)

TARGET := voxelcraft
TEST_TARGET := tests/test_world_environment
SRC := src/main.c src/album.c src/inventory.c src/space.c src/world_environment.c src/ship.c src/nether.c src/entity.c src/ecology.c src/terrain.c src/discovery.c src/chunks.c src/world.c src/player.c src/interaction.c src/render.c src/particles.c src/audio.c src/weather.c src/starmap.c

.PHONY: all run test clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ $(SRC) $(RAYLIB_LIBS) -lm -pthread

run: $(TARGET)
	./$(TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): tests/test_world_environment.c src/world_environment.c src/world_environment.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_world_environment.c src/world_environment.c -lm

clean:
	rm -f $(TARGET) $(TEST_TARGET)
