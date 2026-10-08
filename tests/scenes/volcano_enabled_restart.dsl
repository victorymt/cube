# Run in the same isolated directory after volcano_enabled.dsl.
assert !geology.enabled
geology enabled on
assert geology.enabled
exit 0
