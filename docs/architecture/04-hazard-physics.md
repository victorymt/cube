# Architecture: Hazard Physics

`hazard_physics` supplies deterministic, off-screen state transitions for two
high-impact hazards. Fire cells track fuel, moisture, temperature, and smoke.
Burning is gated by ignition temperature and a fuel/moisture extinction ratio;
wind changes the burn rate, rain adds moisture, evaporation consumes latent
heat, and cooling couples the cell back to air temperature. Smoke production is
derived from burned fuel and smoke removal is returned as an explicit flux.

Supercell cells track a convective energy reservoir, radius, angular velocity,
pressure deficit, position, and steering velocity. Surface/derpoint buoyancy
and vertical shear add energy, dissipation removes it, and formation requires
both a CAPE-like energy threshold and minimum shear. The pressure deficit is
derived from tangential velocity (`q = 0.5 rho v^2`) and is bounded for stable
coupling to damage and debris systems. The ambient wind steers the footprint,
so an unloaded region can advance without spawning render entities.

Both APIs reject invalid durations, clamp environmental inputs, and are free of
global state. Existing `wildfire_model` and `tornado_model` remain compatible;
an adapter can translate their visual/event records to these physical states
when persistence and debris coupling are ready. Tests cover fuel/smoke
accounting, moisture extinction, distance attenuation, deterministic formation,
pressure generation, steering, and dissipation.
