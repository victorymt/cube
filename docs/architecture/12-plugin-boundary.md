# Optional plugin boundary

## Goal

Keep the seven built-in modules statically linked for predictable startup,
while allowing independent features to be developed and shipped as shared
objects. This gives contributors a parallel build seam without weakening the
public-state boundaries enforced by `tests/test_architecture.sh`.

## Contract

`src/core/plugin.h` defines ABI version 2. A shared object exports
`VoxelcraftPluginGetDescriptor`, which returns an immutable descriptor with an
identifier, semantic version, priority, and optional `on_load`, `on_reset`,
`on_update`, `on_render`, and `on_unload` callbacks. Callbacks receive an
opaque host context and a small frame value; they never need to include an
`app/` header or access a mutable global from another domain.

The context also supports a versioned service registry. A plugin can publish a
read-only function table or value with `context->provide_service`, and a
consumer can resolve it by service ID and ABI version using
`context->lookup_service`. The host rejects duplicate IDs and clears all
registrations during teardown, so service contracts remain explicit and do not
turn into cross-domain global state. These callbacks are injected into the
context, so shared objects do not require unresolved symbols from the game
executable.

The app publishes a neutral `voxelcraft.game` service before activation. Its
`VoxelcraftPluginGameService` interface supplies read-only play/pause,
frame-index, and world-seed queries, plus a bounded notice callback. This gives
tooling and telemetry extensions useful runtime context without handing them a
`GameRuntime *` or any domain-global pointer.

The host (`src/core/plugin.c`) validates IDs and descriptor sizes, rejects
duplicates, loads only `.so` files, and visits directory entries in lexical
order. Activation then sorts by priority and ID, making callback order
deterministic. A failed descriptor or callback unwinds every already-loaded
library. Shutdown runs unload callbacks in reverse activation order and closes
all handles.

## Build and runtime

`mk/plugins.mk` evaluates the declarative entries in `plugins/manifest.mk`.
Each entry is an independent shared-library target under the active build
variant, so `make -j plugins` can compile unrelated extensions concurrently.
The game loads them only when `--plugin-dir DIRECTORY` is present. The sample
`plugins/telemetry/plugin.c` demonstrates the lifecycle while leaving normal
gameplay unchanged.

The host is intentionally an opt-in extension point. Existing gameplay and
tests continue to use the built-in module archives; a future plugin may be
promoted into a built-in module only after its API and persistence needs are
stable.

## Verification

- `tests/test_plugin.c` covers registration validation, deterministic priority
  ordering, service publication/lookup, every lifecycle callback, and teardown.
- `make plugins` verifies an independently compiled shared object.
- Debug `ready` and `status` replies expose the loaded plugin count, allowing
  the existing DSL/E2E harness to assert which extension set is active.
