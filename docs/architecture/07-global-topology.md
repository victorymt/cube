# Architecture: Global Planetary Topology

The spherical surface work canonicalizes longitude with wraparound and
reflects latitude at the poles while carrying a north-direction sign. Terrain,
weather, ecology, fluid edits, markers, and saves use the canonical surface
address; local chunk coordinates remain a rendering/storage detail. This
prevents duplicate climate or ecology cells when a player crosses a seam.

The implementation is recorded in commit `5b6c02b feat: add global spherical
world topology`, with integration and migration coverage in `3db802d feat:
integrate spherical weather and ecology identity`. The retained DSL route
exercises longitude wrap, north-pole reflection, south-pole reflection,
canonical block edits, marker persistence, and save/load replay.

Known limits are documented in `todo/07-global-planetary-topology.md`: the
surface is a canonicalized local projection rather than a full cube-sphere
renderer, and spatial keys remain bounded by the current save format.
