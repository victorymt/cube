# Voxelcraft plugins

Plugins are optional shared objects (`.so`) loaded with
`--plugin-dir DIRECTORY`. The host visits files in lexical order, validates
the ABI, then activates them by priority (lower numbers first) and id. A
plugin must export:

```c
const VoxelcraftPluginDescriptor *VoxelcraftPluginGetDescriptor(void);
```

The stable interface is [`src/core/plugin.h`](../src/core/plugin.h). It only
contains lifecycle callbacks, an opaque host pointer, and a frame snapshot;
plugins do not include `app/` or reach into mutable domain globals. This keeps
world, ecology, presentation, and tooling extensions independently buildable.

Plugins that need to cooperate can publish a versioned read-only service from
`on_load` with `context->provide_service(context, &service)`. Another plugin
can resolve it with `context->lookup_service(context, id, abi_version)`.
Service IDs are unique per host, and registrations are cleared automatically
when the host shuts down. A service interface should therefore be a small
function table or immutable value type whose lifetime is owned by its provider.
The callbacks are injected into the context so a shared object never needs to
link against symbols from the main executable.

The game publishes `VOXELCRAFT_PLUGIN_GAME_SERVICE_ID` (service ABI version
`VOXELCRAFT_PLUGIN_GAME_SERVICE_ABI_VERSION`) before plugin activation. Its
`VoxelcraftPluginGameService` table exposes read-only play/pause, frame-index,
and world-seed queries plus the bounded `post_notice` notification hook.

Build the sample plugin with:

```sh
make plugins
build/normal/voxelcraft --plugin-dir build/normal/plugins
```

Add another independently buildable extension by appending a
`define_plugin(name, source.c, extra_ldlibs)` line to `manifest.mk`. Targets
are ordinary Make dependencies and can be built concurrently with `make -j`.

The sample `telemetry` plugin demonstrates allocation and reset/update
lifecycle without changing gameplay. The directory is optional; when
`--plugin-dir` is omitted, no shared objects are loaded.
