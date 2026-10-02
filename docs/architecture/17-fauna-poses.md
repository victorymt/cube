# Animal behavior and procedural poses

The implementation follows three stages: deterministic pose/contact state,
behavior and articulated rendering, then reproducible DSL previews and visual
verification. The first two stages are recorded in `42e10ee` and `8e41086`.

## Ownership and motion

`ecology/fauna_pose` owns a pure pose model. The fixed entity simulation advances
gait cycles from actual horizontal displacement after collision handling. A
blocked animal therefore stops stepping. During steady straight movement, the
stance foot travels backward relative to the body by the same distance the body
travels forward; a Hermite swing and raised arc return it to the next contact.
Bipeds alternate feet, quadrupeds blend four-beat walking into diagonal running,
and six-legged bodies alternate groups of three. Transition weights brake before
feeding or lowering the body and recover quickly when fleeing interrupts rest.

`presentation/creature_pose` derives anatomical rows and sides from limb modules,
independent of the module-array index. It transforms the parent hierarchy, moves
the head and its descendants together, and solves bent knees between the hips
and feet. World rendering samples nearby voxel support separately for each foot;
the atlas supplies a flat floor and never samples world terrain. Rendering does
not mutate entities or consume their random stream. Simple animal models share
the behavior poses and foot curves; chickens have two legs.

Body height, pitch, bank, head direction, tail motion and appendage display mix
locomotion, behavior and fatigue/stress. Feeding and drinking lower the head;
alert animals raise it; rest and nesting lower the body. Flight effort increases
with movement/escape, turns bank the body, resting wings fold backward, and live
flying animals lower their hover target to safe solid ground to rest/feed.
Aquatic animals retain gentle fin motion while resting and increase tail effort
when moving or fleeing. Colony nodes contract under threat and expand to feed.

## Decisions and contact

Temperament sets separate alert and flight distances. Quiet approaches first
produce a stationary, player-facing alert pose. Close approaches, a fast approach
or high stress cause escape; distance hysteresis keeps escape/alert stable near
their boundaries. Immediate danger overrides pregnancy. Pregnant animals seek
nearby shelter when needed, otherwise nest while allowing urgent food/water needs.

Predators pause near their target. An attack cycle lasts 0.8 seconds; only its
forward contact window causes damage. Contact integration accounts for a step
crossing the cycle boundary. The head thrust uses that same persisted phase.
Courtship displays briefly before conception; scavengers bend toward the corpse.

## Persistence and verification

The entity payload now writes version 5, adding finite, range-checked pose phases
and transition weights. Versions 1–4 still load with a neutral initial pose.
Restore is atomic, including invalid pose data. Pose state is authoritative;
previous-frame interpolation history remains transient and is discarded on load,
slot reuse, teleport or surface changes. Wrapped gait phases interpolate through
the seam. The action enum appends `alert` to preserve earlier numeric values.

`test_fauna_pose` checks planted-foot compensation, support groups, braking,
interruption, wrap interpolation and timestep-independent strike contact.
`test_creature_pose` checks 32 generated anatomies, head-child attachment and
independent foot support heights. Entity replay tests include old-save migration,
byte-identical checkpoint continuation, atomic rejection, attack windup and
pregnancy escaping danger. See [the DSL guide](../debug-dsl.md) for frozen action,
speed, phase and turn previews and `make test-animal-pose-e2e`.

Validation on 2026-10-03: the normal suite passed 94 binaries and failed the
pre-existing `test_chunk_streaming` predictive-preload capacity assertion; the
same assertion failed in an isolated checkout of baseline `e7a221f`. The existing
architecture gate also rejects unchanged `render_ui.c` (1576 lines against its
1500-line limit). The five pose/behavior/anatomy/replay/DSL binaries passed with
ASan, UBSan and `-Werror`. The animal DSL route captured 39 poses and the revised
flight route captured 13 more. The existing graphics regression passed after
waiting for hydrology's first step rather than assuming an active runtime had
already completed it; its save/load and pause/map/view/input/ship checks passed.

## Scope

These are kinematic poses with local obstacle avoidance. Feet are compensated
analytically during straight stance, not permanently pinned in world space through
arbitrary sharp turns. The model has no full-body dynamics or ragdoll solver.
Food/water remain ecological availability samples, so a lowered muzzle does not
identify or consume a particular plant or water voxel. Nesting expresses seeking
shelter and settling; it does not place nest blocks. Flight uses terrain-following
hover control rather than aerodynamic lift. A specimen preview is a controlled
pose, not evidence of every corresponding live-world AI sequence.
