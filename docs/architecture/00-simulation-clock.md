# Architecture: Unified Simulation Clock

## Purpose

`SimulationClock` is the single wall-clock to simulation-time boundary. It
accumulates real frame duration, consumes bounded fixed-size simulation steps,
and exposes day/year phase helpers. Simulation systems must advance from those
steps instead of integrating directly from render frame duration.

## Runtime policy

`GameRuntime` owns one clock. When gameplay is active,
`GameUpdatePauseAndBiologyAtlas` calls `SimulationClockAdvance`; each consumed
step invokes `SpaceAdvanceTime`. The day/night phase is advanced by the number
of consumed steps, so a variable render rate cannot change celestial time.
Paused menus, the album, maps, landing transitions, and the biology atlas do
not consume simulation steps.

The runtime currently uses a compatibility configuration: one simulation
second per real second, a 1/60 second fixed step, and a 240-second planetary
day (the existing presentation scale). The clock API itself defaults to SI
seconds and a 24-hour day, so later stages can select an Earth-like scale
without changing callers or persistence formats.

## Catch-up and determinism

At most eight fixed steps are consumed per frame. Excess accumulated duration
is discarded and never leaks into a later frame, preventing a spiral of death.
Invalid or non-finite frame durations are ignored. A callback receives the
post-step simulation timestamp and fixed step duration, which makes replay
and deterministic system tests straightforward.

## Reset and save compatibility

Starting a new world resets the clock to zero. Loading a save resets it to the
persisted `SpaceSimulationTime` after the load transaction succeeds. The
existing save payload is unchanged; the clock remains a derived runtime
boundary and therefore does not require a save version migration.

## Known limits

Weather, fluids, ecology, and player motion still have frame-driven update
paths. They are migrated in subsequent stages, each with its own conservation
and replay tests. The compatibility time scale also remains intentionally
accelerated for current gameplay.
