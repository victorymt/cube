# Declarative ownership manifest. Each entry expands to one independently
# rebuildable archive and one component-* convenience target.

COMPONENT_NAMES := core world space ecology gameplay presentation app

MODULE_ARCHIVES :=

define define_component
COMPONENT_$1_SRC := $$($2)
COMPONENT_$1_OBJ := $$(patsubst src/%.c,$$(OBJ_DIR)/%.o,$$(COMPONENT_$1_SRC))
MODULE_ARCHIVE_$1 := $$(MODULE_BUILD_DIR)/$1.a
MODULE_ARCHIVES += $$(MODULE_ARCHIVE_$1)

.PHONY: component-$1
component-$1: $$(MODULE_ARCHIVE_$1)

$$(MODULE_ARCHIVE_$1): $$(COMPONENT_$1_OBJ) $$(BUILD_CONFIG) | $$(MODULE_BUILD_DIR)
	@rm -f $$@
	$$(AR) rcs $$@ $$(filter %.o,$$^)
endef

$(eval $(call define_component,core,CORE_SRC))
$(eval $(call define_component,world,WORLD_SRC))
$(eval $(call define_component,space,SPACE_SRC))
$(eval $(call define_component,ecology,ECOLOGY_SRC))
$(eval $(call define_component,gameplay,GAMEPLAY_SRC))
$(eval $(call define_component,presentation,PRESENTATION_SRC))
$(eval $(call define_component,app,APP_SRC))
