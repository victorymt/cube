# Optional shared plugins are declared by plugins/manifest.mk.  Each entry is
# an independent target, so `make -j plugins` can compile unrelated features
# concurrently without pulling their sources into the main executable.

PLUGIN_BUILD_DIR := $(BUILD_DIR)/plugins
PLUGIN_TARGETS :=
PLUGIN_CPPFLAGS ?= -Isrc
PLUGIN_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2 -fPIC -pthread
PLUGIN_LDFLAGS ?= -shared
PLUGIN_LDLIBS ?=
PLUGIN_HOST_LIBS ?= -ldl
PLUGIN_HEADERS := $(sort $(wildcard src/core/plugin.h))

define define_plugin
PLUGIN_TARGETS += $$(PLUGIN_BUILD_DIR)/$1.so
$$(PLUGIN_BUILD_DIR)/$1.so: $2 $$(PLUGIN_HEADERS) $$(BUILD_CONFIG) | $$(PLUGIN_BUILD_DIR)
	$$(CC) $$(PLUGIN_CPPFLAGS) $$(PLUGIN_CFLAGS) $$(PLUGIN_LDFLAGS) -o $$@ $2 $3 $$(PLUGIN_LDLIBS)
endef

-include plugins/manifest.mk
