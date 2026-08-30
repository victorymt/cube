# Architecture: Cross-system Surface Identity

Weather, ecology, wildfire, fluids, evolution, and surface persistence share
the `SurfaceCanonicalMapCell` boundary. Queries first map raw local positions to
the same body/longitude/latitude cell; rendering may retain a local frame only
after that identity is resolved. This keeps aliases from creating duplicate
population regions, impact sites, or edits.

The integration was delivered in commit `3db802d feat: integrate spherical
weather and ecology identity`, following topology commit `5b6c02b`. Existing
focused weather/ecology/save tests and the DSL topology route verify alias
identity, pole sign, deterministic sampling, and save/load behavior.

The current project still uses compatibility weather/fluid solvers and a
bounded active-entity set. New conservation modules in architecture records
01-05 are deliberately isolated adapters until a runtime persistence migration
can be made without changing old saves.
