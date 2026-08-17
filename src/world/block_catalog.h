#ifndef VOXELCRAFT_BLOCK_CATALOG_H
#define VOXELCRAFT_BLOCK_CATALOG_H

#include "world/world_types.h"

typedef struct BlockCatalogEntry {
    const char *name;
    Color baseColor;
    BlockTexture sideTexture;
    BlockTexture topTexture;
    BlockTexture bottomTexture;
    BlockRenderShape renderShape;
    float collisionHeight;
    bool translucent;
} BlockCatalogEntry;

#define BLOCK_ENTRY(TYPE, NAME, R, G, B, TEXTURE) \
    [TYPE] = { NAME, { R, G, B, 255 }, TEXTURE, TEXTURE, TEXTURE, \
               BLOCK_RENDER_CUBE, 1.0f, false }
#define BLOCK_FACE_ENTRY(TYPE, NAME, R, G, B, SIDE, TOP, BOTTOM) \
    [TYPE] = { NAME, { R, G, B, 255 }, SIDE, TOP, BOTTOM, \
               BLOCK_RENDER_CUBE, 1.0f, false }
#define BLOCK_META_ENTRY(TYPE, NAME, R, G, B, TEXTURE, SHAPE, HEIGHT, TRANS) \
    [TYPE] = { NAME, { R, G, B, 255 }, TEXTURE, TEXTURE, TEXTURE, \
               SHAPE, HEIGHT, TRANS }

static const BlockCatalogEntry blockCatalog[BLOCK_NATURAL_END + 1] = {
    BLOCK_META_ENTRY(BLOCK_AIR, "Air", 118, 122, 124, TEX_DIRT,
                     BLOCK_RENDER_CUBE, 0.0f, false),
    BLOCK_FACE_ENTRY(BLOCK_GRASS, "Grass", 84, 170, 67,
                     TEX_GRASS_SIDE, TEX_GRASS_TOP, TEX_DIRT),
    BLOCK_ENTRY(BLOCK_DIRT, "Dirt", 121, 77, 43, TEX_DIRT),
    BLOCK_ENTRY(BLOCK_STONE, "Stone", 118, 122, 124, TEX_STONE),
    BLOCK_FACE_ENTRY(BLOCK_WOOD, "Wood", 142, 91, 42,
                     TEX_WOOD_SIDE, TEX_WOOD_TOP, TEX_WOOD_TOP),
    BLOCK_ENTRY(BLOCK_SAND, "Sand", 214, 197, 132, TEX_SAND),
    BLOCK_ENTRY(BLOCK_LEAVES, "Leaves", 46, 128, 55, TEX_LEAVES),
    BLOCK_ENTRY(BLOCK_RED, "Red", 207, 55, 54, TEX_RED),
    BLOCK_ENTRY(BLOCK_ORANGE, "Orange", 229, 126, 38, TEX_ORANGE),
    BLOCK_ENTRY(BLOCK_YELLOW, "Yellow", 238, 207, 64, TEX_YELLOW),
    BLOCK_ENTRY(BLOCK_BLUE, "Blue", 51, 116, 220, TEX_BLUE),
    BLOCK_ENTRY(BLOCK_PURPLE, "Purple", 143, 72, 202, TEX_PURPLE),
    BLOCK_ENTRY(BLOCK_GREEN, "Green", 64, 185, 85, TEX_GREEN),
    BLOCK_ENTRY(BLOCK_CYAN, "Cyan", 47, 188, 207, TEX_CYAN),
    BLOCK_ENTRY(BLOCK_PINK, "Pink", 226, 96, 161, TEX_PINK),
    BLOCK_ENTRY(BLOCK_WHITE, "White", 232, 235, 224, TEX_WHITE),
    BLOCK_ENTRY(BLOCK_GRAY, "Gray", 112, 119, 126, TEX_GRAY),
    BLOCK_ENTRY(BLOCK_BLACK, "Black", 28, 31, 35, TEX_BLACK),
    BLOCK_ENTRY(BLOCK_PLANK, "Plank", 156, 100, 48, TEX_PLANK),
    BLOCK_ENTRY(BLOCK_BRICK, "Brick", 148, 62, 48, TEX_BRICK),
    BLOCK_META_ENTRY(BLOCK_GLASS, "Glass", 205, 230, 235, TEX_GLASS,
                     BLOCK_RENDER_CUBE, 1.0f, true),
    BLOCK_META_ENTRY(BLOCK_WATER, "Water", 52, 118, 205, TEX_WATER,
                     BLOCK_RENDER_CUBE, 0.0f, true),
    BLOCK_ENTRY(BLOCK_SNOW, "Snow", 238, 244, 246, TEX_SNOW),
    BLOCK_ENTRY(BLOCK_ICE, "Ice", 148, 205, 226, TEX_ICE),
    BLOCK_ENTRY(BLOCK_CACTUS, "Cactus", 78, 152, 62, TEX_CACTUS),
    BLOCK_ENTRY(BLOCK_BEDROCK, "Bedrock", 58, 58, 64, TEX_BEDROCK),
    BLOCK_ENTRY(BLOCK_COAL_ORE, "Coal Ore", 90, 92, 96, TEX_COAL_ORE),
    BLOCK_ENTRY(BLOCK_IRON_ORE, "Iron Ore", 190, 152, 108, TEX_IRON_ORE),
    BLOCK_ENTRY(BLOCK_GOLD_ORE, "Gold Ore", 232, 196, 64, TEX_GOLD_ORE),
    BLOCK_ENTRY(BLOCK_DIAMOND_ORE, "Diamond Ore", 92, 214, 232,
                TEX_DIAMOND_ORE),
    BLOCK_META_ENTRY(BLOCK_TORCH, "Torch", 255, 186, 62, TEX_TORCH,
                     BLOCK_RENDER_CUBE, 1.0f, true),
    BLOCK_META_ENTRY(BLOCK_ALBUM, "Album", 118, 76, 42, TEX_ALBUM,
                     BLOCK_RENDER_CUBE, 1.0f, true),
    BLOCK_META_ENTRY(BLOCK_SLAB, "Stone Slab", 118, 122, 124, TEX_STONE,
                     BLOCK_RENDER_CUBE, 0.5f, false),
    BLOCK_ENTRY(BLOCK_DOOR, "Door", 156, 104, 52, TEX_DOOR),
    BLOCK_META_ENTRY(BLOCK_DOOR_OPEN, "Open Door", 140, 92, 46, TEX_DOOR,
                     BLOCK_RENDER_CUBE, 0.0f, false),
    BLOCK_ENTRY(BLOCK_MOON_ROCK, "Moon Rock", 138, 142, 148,
                TEX_MOON_ROCK),
    BLOCK_ENTRY(BLOCK_METEORITE, "Meteorite", 92, 78, 70, TEX_METEORITE),
    BLOCK_ENTRY(BLOCK_MOON_SAND, "Moon Sand", 190, 186, 176,
                TEX_MOON_SAND),
    BLOCK_ENTRY(BLOCK_STAR_MATTER, "Star Matter", 238, 236, 222,
                TEX_STAR_MATTER),
    BLOCK_ENTRY(BLOCK_SPACESHIP, "Spaceship", 196, 202, 210,
                TEX_SPACESHIP),
    BLOCK_META_ENTRY(BLOCK_STONE_STAIRS, "Stone Stairs", 118, 122, 124,
                     TEX_STONE, BLOCK_RENDER_CUBE, 0.5f, false),
    BLOCK_META_ENTRY(BLOCK_WOOD_STAIRS, "Wood Stairs", 156, 100, 48,
                     TEX_PLANK, BLOCK_RENDER_CUBE, 0.5f, false),
    BLOCK_ENTRY(BLOCK_FENCE, "Fence", 150, 98, 50, TEX_FENCE),
    BLOCK_ENTRY(BLOCK_FENCE_GATE, "Fence Gate", 150, 98, 50, TEX_FENCE),
    BLOCK_META_ENTRY(BLOCK_FENCE_GATE_OPEN, "Open Fence Gate", 138, 90, 46,
                     TEX_FENCE, BLOCK_RENDER_CUBE, 0.0f, false),
    BLOCK_META_ENTRY(BLOCK_GLASS_PANE, "Glass Pane", 205, 230, 235,
                     TEX_GLASS, BLOCK_RENDER_CUBE, 1.0f, true),
    BLOCK_META_ENTRY(BLOCK_LAVA, "Lava", 224, 96, 24, TEX_LAVA,
                     BLOCK_RENDER_CUBE, 0.0f, true),
    BLOCK_META_ENTRY(BLOCK_FLOWER, "Flower", 208, 62, 54, TEX_FLOWER,
                     BLOCK_RENDER_CROSS, 0.0f, true),
    BLOCK_META_ENTRY(BLOCK_MUSHROOM, "Mushroom", 196, 52, 46,
                     TEX_MUSHROOM, BLOCK_RENDER_CROSS, 0.0f, true),
    BLOCK_ENTRY(BLOCK_BOOKSHELF, "Bookshelf", 118, 76, 40,
                TEX_BOOKSHELF),
    BLOCK_ENTRY(BLOCK_HAY_BALE, "Hay Bale", 218, 172, 66, TEX_HAY),
    BLOCK_ENTRY(BLOCK_PUMPKIN, "Pumpkin", 224, 138, 42, TEX_PUMPKIN),
    BLOCK_ENTRY(BLOCK_NETHERRACK, "Netherrack", 116, 48, 42,
                TEX_NETHERRACK),
    BLOCK_ENTRY(BLOCK_SOUL_SAND, "Soul Sand", 124, 106, 88,
                TEX_SOUL_SAND),
    BLOCK_ENTRY(BLOCK_GLOWSTONE, "Glowstone", 250, 220, 110,
                TEX_GLOWSTONE),
    BLOCK_ENTRY(BLOCK_STONE_BRICKS, "Stone Bricks", 138, 140, 142,
                TEX_STONE_BRICKS),
    BLOCK_ENTRY(BLOCK_SANDSTONE, "Sandstone", 216, 200, 150,
                TEX_SANDSTONE),
    BLOCK_ENTRY(BLOCK_OBSIDIAN, "Obsidian", 22, 16, 30, TEX_OBSIDIAN),
    BLOCK_META_ENTRY(BLOCK_NETHER_PORTAL, "Nether Portal", 158, 52, 190,
                     TEX_NETHER_PORTAL, BLOCK_RENDER_CUBE, 1.0f, true),
    BLOCK_ENTRY(BLOCK_SPACESHIP_CORE_NORTH, "Spaceship", 196, 202, 210,
                TEX_SPACESHIP),
    BLOCK_ENTRY(BLOCK_SPACESHIP_CORE_EAST, "Spaceship", 196, 202, 210,
                TEX_SPACESHIP),
    BLOCK_ENTRY(BLOCK_SPACESHIP_CORE_SOUTH, "Spaceship", 196, 202, 210,
                TEX_SPACESHIP),
    BLOCK_ENTRY(BLOCK_SPACESHIP_CORE_WEST, "Spaceship", 196, 202, 210,
                TEX_SPACESHIP),
    BLOCK_ENTRY(BLOCK_SPACESHIP_OCCUPIED, "Spaceship", 196, 202, 210,
                TEX_SPACESHIP),
    BLOCK_ENTRY(BLOCK_GRAVEL, "Gravel", 112, 108, 104, TEX_GRAVEL),
    BLOCK_ENTRY(BLOCK_CLAY, "Clay", 151, 164, 170, TEX_CLAY),
    BLOCK_ENTRY(BLOCK_MUD, "Mud", 91, 68, 48, TEX_MUD),
    BLOCK_ENTRY(BLOCK_MOSSY_STONE, "Mossy Stone", 92, 112, 76,
                TEX_MOSSY_STONE),
    BLOCK_ENTRY(BLOCK_RED_SAND, "Red Sand", 184, 96, 54, TEX_RED_SAND),
    BLOCK_ENTRY(BLOCK_BASALT, "Basalt", 58, 61, 66, TEX_BASALT),
    BLOCK_ENTRY(BLOCK_COPPER_ORE, "Copper Ore", 184, 112, 72,
                TEX_COPPER_ORE),
    BLOCK_ENTRY(BLOCK_CRYSTAL, "Crystal", 126, 188, 212, TEX_CRYSTAL),
    BLOCK_ENTRY(BLOCK_GRANITE, "Granite", 126, 113, 108, TEX_GRANITE),
    BLOCK_ENTRY(BLOCK_LIMESTONE, "Limestone", 188, 183, 157,
                TEX_LIMESTONE),
    BLOCK_ENTRY(BLOCK_SHALE, "Shale", 75, 82, 86, TEX_SHALE),
    BLOCK_ENTRY(BLOCK_MARBLE, "Marble", 211, 211, 205, TEX_MARBLE),
    BLOCK_ENTRY(BLOCK_PEAT, "Peat", 67, 50, 38, TEX_PEAT),
    BLOCK_ENTRY(BLOCK_PERMAFROST, "Permafrost", 126, 137, 139,
                TEX_PERMAFROST),
    BLOCK_ENTRY(BLOCK_ROCK_SALT, "Rock Salt", 218, 207, 198,
                TEX_ROCK_SALT),
    BLOCK_ENTRY(BLOCK_VOLCANIC_ASH, "Volcanic Ash", 73, 68, 67,
                TEX_VOLCANIC_ASH),
    BLOCK_ENTRY(BLOCK_PUMICE, "Pumice", 167, 157, 143, TEX_PUMICE),
    BLOCK_ENTRY(BLOCK_SULFUR_ORE, "Sulfur Ore", 206, 183, 50,
                TEX_SULFUR_ORE),
    BLOCK_ENTRY(BLOCK_PACKED_ICE, "Packed Ice", 101, 164, 193,
                TEX_PACKED_ICE),
    BLOCK_ENTRY(BLOCK_QUARTZ_ORE, "Quartz Ore", 205, 196, 205,
                TEX_QUARTZ_ORE),
    BLOCK_ENTRY(BLOCK_LOAM, "Loam", 112, 76, 48, TEX_LOAM),
    BLOCK_ENTRY(BLOCK_PODZOL, "Podzol", 91, 67, 43, TEX_PODZOL),
    BLOCK_ENTRY(BLOCK_SILT, "Silt", 132, 121, 103, TEX_SILT),
    BLOCK_ENTRY(BLOCK_CHALK, "Chalk", 218, 216, 199, TEX_CHALK),
    BLOCK_ENTRY(BLOCK_GNEISS, "Gneiss", 116, 112, 119, TEX_GNEISS),
    BLOCK_ENTRY(BLOCK_LATERITE, "Laterite", 154, 74, 45, TEX_LATERITE),
    BLOCK_ENTRY(BLOCK_SCORIA, "Scoria", 73, 48, 45, TEX_SCORIA),
    BLOCK_ENTRY(BLOCK_REGOLITH, "Regolith", 143, 136, 127, TEX_REGOLITH),
    BLOCK_ENTRY(BLOCK_SALT_CRUST, "Salt Crust", 232, 224, 207,
                TEX_SALT_CRUST),
    BLOCK_ENTRY(BLOCK_TIN_ORE, "Tin Ore", 166, 174, 176, TEX_TIN_ORE),
    BLOCK_ENTRY(BLOCK_SILVER_ORE, "Silver Ore", 196, 202, 207,
                TEX_SILVER_ORE),
    BLOCK_ENTRY(BLOCK_NICKEL_ORE, "Nickel Ore", 151, 166, 132,
                TEX_NICKEL_ORE),
    BLOCK_META_ENTRY(BLOCK_TALL_GRASS, "Tall Grass", 83, 151, 63,
                     TEX_TALL_GRASS, BLOCK_RENDER_CROSS, 0.0f, true),
    BLOCK_META_ENTRY(BLOCK_FERN, "Fern", 53, 126, 66, TEX_FERN,
                     BLOCK_RENDER_CROSS, 0.0f, true),
    BLOCK_META_ENTRY(BLOCK_REED, "Reed", 116, 153, 70, TEX_REED,
                     BLOCK_RENDER_CROSS, 0.0f, true),
    BLOCK_META_ENTRY(BLOCK_MOSS_CARPET, "Moss Carpet", 62, 111, 53,
                     TEX_MOSS_CARPET, BLOCK_RENDER_CARPET, 0.0f, true),
    BLOCK_META_ENTRY(BLOCK_LICHEN, "Lichen", 137, 151, 92, TEX_LICHEN,
                     BLOCK_RENDER_CROSS, 0.0f, true),
    BLOCK_META_ENTRY(BLOCK_MICROBIAL_MAT, "Microbial Mat", 96, 126, 105,
                     TEX_MICROBIAL_MAT, BLOCK_RENDER_CARPET, 0.0f, true),
    BLOCK_META_ENTRY(BLOCK_MYCELIUM, "Mycelium", 142, 132, 151,
                     TEX_MYCELIUM, BLOCK_RENDER_CARPET, 0.0f, true),
    BLOCK_ENTRY(BLOCK_LIVING_STEM, "Living Stem", 99, 91, 67,
                TEX_LIVING_STEM),
    BLOCK_META_ENTRY(BLOCK_CANOPY_FROND, "Canopy Frond", 63, 137, 101,
                     TEX_CANOPY_FROND, BLOCK_RENDER_CUBE, 1.0f, true),
    BLOCK_META_ENTRY(BLOCK_LUMINOUS_POD, "Luminous Pod", 126, 220, 174,
                     TEX_LUMINOUS_POD, BLOCK_RENDER_CUBE, 1.0f, true),
    BLOCK_ENTRY(BLOCK_FUNGAL_STEM, "Fungal Stem", 174, 164, 152,
                TEX_FUNGAL_STEM),
    BLOCK_META_ENTRY(BLOCK_SPORE_CAP, "Spore Cap", 164, 86, 151,
                     TEX_SPORE_CAP, BLOCK_RENDER_CUBE, 1.0f, true),
    BLOCK_ENTRY(BLOCK_CRYSTAL_BLOOM, "Crystal Bloom", 137, 205, 220,
                TEX_CRYSTAL_BLOOM),
    BLOCK_ENTRY(BLOCK_VENT_CHIMNEY, "Vent Chimney", 72, 67, 65,
                TEX_VENT_CHIMNEY),
    BLOCK_META_ENTRY(BLOCK_CHEMO_MAT, "Chemosynthetic Mat", 170, 133, 54,
                     TEX_CHEMO_MAT, BLOCK_RENDER_CARPET, 0.0f, true)
};

#undef BLOCK_META_ENTRY
#undef BLOCK_FACE_ENTRY
#undef BLOCK_ENTRY

static inline const BlockCatalogEntry *BlockCatalogGet(BlockType type)
{
    int index = (int)type;
    if (index < BLOCK_AIR || index > BLOCK_NATURAL_END ||
        !blockCatalog[index].name) {
        index = BLOCK_AIR;
    }
    return &blockCatalog[index];
}

#endif
