# Keep this file declarative: one define_plugin call per independently
# buildable extension.  Downstream projects can replace this manifest or add
# entries without editing the core Makefile.

$(eval $(call define_plugin,telemetry,plugins/telemetry/plugin.c,))
