#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

static int modelLoadCalls;
static int modelUnloadCalls;
static int shaderLoadCalls;
static int shaderUnloadCalls;
static int failModelLoads;
static int failShaderLoadCall;
static Mesh storedMesh;

#include "../src/presentation/planet_renderer.c"

void UploadMesh(Mesh *mesh, bool dynamic)
{
    (void)mesh;
    (void)dynamic;
}

Model LoadModelFromMesh(Mesh mesh)
{
    modelLoadCalls++;
    if (failModelLoads > 0) {
        failModelLoads--;
        free(mesh.vertices);
        free(mesh.normals);
        free(mesh.texcoords);
        free(mesh.indices);
        return (Model){ 0 };
    }

    storedMesh = mesh;
    static Material material;
    return (Model){
        .meshCount = 1,
        .materialCount = 1,
        .meshes = &storedMesh,
        .materials = &material
    };
}

Shader LoadShaderFromMemory(const char *vsCode, const char *fsCode)
{
    (void)vsCode;
    (void)fsCode;
    shaderLoadCalls++;
    if (failShaderLoadCall == shaderLoadCalls) return (Shader){ 0 };
    return (Shader){ .id = (unsigned int)shaderLoadCalls };
}

int GetShaderLocation(Shader shader, const char *uniformName)
{
    (void)uniformName;
    return shader.id == 0 ? -1 : 0;
}

void UnloadModel(Model model)
{
    modelUnloadCalls++;
    if (model.meshes == &storedMesh) {
        free(storedMesh.vertices);
        free(storedMesh.normals);
        free(storedMesh.texcoords);
        free(storedMesh.indices);
        storedMesh = (Mesh){ 0 };
    }
}

void UnloadShader(Shader shader)
{
    if (shader.id != 0) shaderUnloadCalls++;
}

static void ResetRendererMocks(void)
{
    PlanetRendererShutdown();
    modelLoadCalls = 0;
    modelUnloadCalls = 0;
    shaderLoadCalls = 0;
    shaderUnloadCalls = 0;
    failModelLoads = 0;
    failShaderLoadCall = 0;
}

static void TestModelFailureRollsBackAndRetries(void)
{
    ResetRendererMocks();
    failModelLoads = 1;

    PlanetRendererEnsureResources();
    assert(!renderer.initialized);
    assert(modelLoadCalls == 1);
    assert(shaderLoadCalls == 2);
    assert(modelUnloadCalls == 0);
    assert(shaderUnloadCalls == 2);

    PlanetRendererEnsureResources();
    assert(renderer.initialized);
    assert(modelLoadCalls == 2);
    assert(shaderLoadCalls == 4);
    PlanetRendererEnsureResources();
    assert(modelLoadCalls == 2);
    assert(shaderLoadCalls == 4);

    PlanetRendererShutdown();
    assert(modelUnloadCalls == 1);
    assert(shaderUnloadCalls == 4);
    PlanetRendererShutdown();
    assert(modelUnloadCalls == 1);
    assert(shaderUnloadCalls == 4);
}

static void TestShaderFailureRollsBackAndRetries(void)
{
    ResetRendererMocks();
    failShaderLoadCall = 2;

    PlanetRendererEnsureResources();
    assert(!renderer.initialized);
    assert(modelLoadCalls == 1);
    assert(modelUnloadCalls == 1);
    assert(shaderUnloadCalls == 1);

    PlanetRendererEnsureResources();
    assert(renderer.initialized);
    assert(modelLoadCalls == 2);
    assert(shaderLoadCalls == 4);
    PlanetRendererShutdown();
    assert(modelUnloadCalls == 2);
    assert(shaderUnloadCalls == 3);
}

int main(void)
{
    TestModelFailureRollsBackAndRetries();
    TestShaderFailureRollsBackAndRetries();
    puts("planet renderer resource tests passed");
    return 0;
}
