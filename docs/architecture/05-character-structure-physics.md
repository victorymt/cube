# Architecture: Character, Structure, and Survival Physics

`character_structure_physics` defines the next boundary below gameplay input.
Character bodies use semi-implicit integration with gravity, quadratic relative
wind/fluid drag, displaced-volume buoyancy, and contact impulses with
restitution/friction. The contact is supplied by the voxel collision layer,
so the model remains independent of Raylib and can be replayed off-screen.

Structure states convert self-weight, external loads, and wind pressure into a
capacity ratio. Overload accumulates damage over time, displacement follows a
stiffness value, and a fully damaged support collapses. A future block-graph
adapter can distribute loads through supports before calling this scalar
boundary.

The optional survival layer tracks hydration, metabolic energy, core
temperature, sleep debt, health, and alive state. Water loss depends on
exertion, humidity, and heat; insulation and shelter control thermal response;
rest reduces sleep debt. It is deliberately opt-in and does not alter current
gameplay until player needs are enabled in settings.

Tests cover gravity, buoyancy, collision impulses, structural overload and
collapse, survival depletion/recovery, deterministic steps, and invalid input.
