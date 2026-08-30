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
