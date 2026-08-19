#include "app/game_save.h"

#include "world/surface_save.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t currentPlanetId = 77u;
static int currentPlanetOriginX = 1000;
static int currentPlanetOriginZ = 4090;

uint32_t PlanetWorldSeed(void)
{
    return currentPlanetId;
}

int PlanetWorldOriginX(void)
{
    return currentPlanetOriginX;
}

int PlanetWorldOriginZ(void)
{
    return currentPlanetOriginZ;
}

bool WorldIsSurfaceDimension(WorldDimension dimension)
{
    return dimension == WORLD_DIMENSION_HOME ||
           dimension == WORLD_DIMENSION_PLANET;
}

static bool Near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static void WriteAddress(FILE *file, SurfaceAddress address)
{
    uint32_t face = (uint32_t)address.face;
    int32_t u = (int32_t)address.u;
    int32_t v = (int32_t)address.v;
    int32_t radial = (int32_t)address.radial;
    assert(fwrite(&address.bodyId, sizeof(address.bodyId), 1, file) == 1);
    assert(fwrite(&face, sizeof(face), 1, file) == 1);
    assert(fwrite(&u, sizeof(u), 1, file) == 1);
    assert(fwrite(&v, sizeof(v), 1, file) == 1);
    assert(fwrite(&radial, sizeof(radial), 1, file) == 1);
}

static FILE *WriteSchemaOneTrailer(
    SurfaceAddress playerAddress, const SurfaceAddress *editAddresses,
    uint32_t editCount)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    uint32_t schema = 1u;
    uint8_t hasPlayer = 1u;
    assert(fwrite(&schema, sizeof(schema), 1, file) == 1);
    assert(fwrite(&hasPlayer, sizeof(hasPlayer), 1, file) == 1);
    WriteAddress(file, playerAddress);
    assert(fwrite(&editCount, sizeof(editCount), 1, file) == 1);
    for (uint32_t index = 0u; index < editCount; index++) {
        WriteAddress(file, editAddresses[index]);
    }
    rewind(file);
    return file;
}

static void TestSchemaOnePlanetMigration(void)
{
    currentPlanetId = 77u;
    currentPlanetOriginX = 1000;
    currentPlanetOriginZ = 4090;
    Player player = {
        .position = { 32.25f, 70.8f, 20.75f },
        .yaw = 0.4f
    };
    BlockEdit edits[2] = {
        { 5, 12, -9, BLOCK_STONE },
        { SURFACE_EQUATOR_BLOCKS + 3, 4, -2, BLOCK_GLASS }
    };
    uint32_t dimensions[2] = { 77u, 0u };
    SurfaceAddress editAddresses[2] = {
        SurfaceAddressFromMapCoordinates(
            77u, (float)(currentPlanetOriginX + edits[0].x),
            (float)(currentPlanetOriginZ + edits[0].z), edits[0].y),
        SurfaceAddressFromMapCoordinates(
            0u, (float)edits[1].x, (float)edits[1].z, edits[1].y)
    };
    SurfaceAddress playerAddress = SurfaceAddressFromMapCoordinates(
        77u, (float)currentPlanetOriginX + player.position.x,
        (float)currentPlanetOriginZ + player.position.z,
        (int)floorf(player.position.y));
    FILE *file = WriteSchemaOneTrailer(
        playerAddress, editAddresses, 2u);

    SurfaceAddress *loadedAddresses = NULL;
    SurfaceMapCell *loadedCells = NULL;
    assert(GameSaveTestLoadSphericalTrailer(
        file, WORLD_DIMENSION_PLANET, &player, edits, dimensions, 2,
        &loadedAddresses, &loadedCells));
    Vector2 expectedPlayer = SurfaceCanonicalMapPosition(
        1032.25f, 4110.75f, NULL);
    assert(Near(player.position.x, expectedPlayer.x));
    assert(Near(player.position.z, expectedPlayer.y));
    assert(Near(player.yaw, atan2f(sinf(0.4f), -cosf(0.4f))));
    assert(edits[0].x == 1005 && edits[0].z == 4081);
    assert(edits[1].x == 3 && edits[1].z == -2);
    assert(loadedCells[0].x == edits[0].x &&
           loadedCells[0].z == edits[0].z);
    assert(SurfaceAddressEqual(loadedAddresses[0], editAddresses[0]));
    free(loadedAddresses);
    free(loadedCells);
    fclose(file);
}

static FILE *WriteSchemaTwoTrailer(
    SurfaceAddress playerAddress, const SurfaceAddress *editAddresses,
    const SurfaceMapCell *editCells, uint32_t editCount)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(SurfaceSaveWriteTrailer(
        file, true, playerAddress, editAddresses, editCells, editCount));
    rewind(file);
    return file;
}

static void TestSchemaTwoGlobalCoordinates(void)
{
    currentPlanetId = 91u;
    Player player = {
        .position = {
            (float)SURFACE_EQUATOR_BLOCKS + 100.25f, 50.0f, -200.5f
        },
        .yaw = -0.25f
    };
    BlockEdit edit = {
        SURFACE_EQUATOR_BLOCKS + 73, 8, -211, BLOCK_BRICK
    };
    uint32_t dimension = 91u;
    SurfaceMapCell cell = SurfaceCanonicalMapCell(
        (float)edit.x, (float)edit.z);
    SurfaceAddress editAddress = SurfaceAddressFromMapCoordinates(
        91u, (float)cell.x, (float)cell.z, edit.y);
    SurfaceAddress playerAddress = SurfaceAddressFromMapCoordinates(
        91u, player.position.x, player.position.z,
        (int)floorf(player.position.y));
    FILE *file = WriteSchemaTwoTrailer(
        playerAddress, &editAddress, &cell, 1u);

    SurfaceAddress *loadedAddresses = NULL;
    SurfaceMapCell *loadedCells = NULL;
    assert(GameSaveTestLoadSphericalTrailer(
        file, WORLD_DIMENSION_PLANET, &player, &edit, &dimension, 1,
        &loadedAddresses, &loadedCells));
    assert(Near(player.position.x, 100.25f));
    assert(Near(player.position.z, -200.5f));
    assert(Near(player.yaw, -0.25f));
    assert(edit.x == 73 && edit.z == -211);
    assert(loadedCells[0].x == 73 && loadedCells[0].z == -211);
    free(loadedAddresses);
    free(loadedCells);
    fclose(file);
}

static void TestRejectsMismatchedEditBodyTransactionally(void)
{
    currentPlanetId = 77u;
    Player player = { .position = { 4.0f, 20.0f, 8.0f } };
    BlockEdit edit = { 9, 3, 12, BLOCK_STONE };
    uint32_t wrongDimension = 88u;
    SurfaceMapCell cell = SurfaceCanonicalMapCell(
        (float)edit.x, (float)edit.z);
    SurfaceAddress editAddress = SurfaceAddressFromMapCoordinates(
        77u, (float)cell.x, (float)cell.z, edit.y);
    SurfaceAddress playerAddress = SurfaceAddressFromMapCoordinates(
        77u, player.position.x, player.position.z,
        (int)floorf(player.position.y));
    FILE *file = WriteSchemaTwoTrailer(
        playerAddress, &editAddress, &cell, 1u);

    SurfaceAddress *loadedAddresses = (SurfaceAddress *)1;
    SurfaceMapCell *loadedCells = (SurfaceMapCell *)1;
    assert(!GameSaveTestLoadSphericalTrailer(
        file, WORLD_DIMENSION_PLANET, &player, &edit, &wrongDimension, 1,
        &loadedAddresses, &loadedCells));
    assert(loadedAddresses == NULL);
    assert(loadedCells == NULL);
    assert(Near(player.position.x, 4.0f));
    assert(Near(player.position.z, 8.0f));
    fclose(file);
}

int main(void)
{
    TestSchemaOnePlanetMigration();
    TestSchemaTwoGlobalCoordinates();
    TestRejectsMismatchedEditBodyTransactionally();
    puts("game spherical save tests passed");
    return 0;
}
