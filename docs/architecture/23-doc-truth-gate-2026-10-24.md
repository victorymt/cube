# Documentation truth gate (2026-10-24)

- Nature: **process record**. It describes the documentation gate and the audit that produced it.
- Current state of any open documentation problem belongs to [STATUS.md](../../STATUS.md), not here.
- Gate location: `scripts/check-docs.mjs` (prose), `scripts/check-docs-truth.mjs` (truth), `scripts/docs-probe.mjs` (runtime).

## Why this exists

The prose gate was green while the documentation was wrong in ways it structurally could not see. Three examples were found in one afternoon:

| Document | Claim | What was actually true |
| --- | --- | --- |
| `docs/debug-dsl.md` | the ready line lists the accepted command groups | the copy was missing `hydrology`, `geology`, `cga`, `landform`, and `terrain` |
| `docs/debug-dsl.md` | a table of `stream.cube.*` fields | a prose paragraph had been inserted mid-table, splitting it; several rows rendered as body text |
| `docs/architecture/09-performance-optimization.md` | "the dedicated CI performance job runs the same benchmark" | `make benchmark-chunks` did not link and had not for weeks |

The gate checked links, anchors, duplicated paragraphs and volatile counts. None of those three defects is any of those things. A document can be internally consistent, correctly linked, and still describe software that no longer exists.

The second gap was reachability. `REVIEW.md` and a cube design note had no inbound link from anywhere, so nothing in the process would ever revisit them, and both had drifted.

## What was measured before designing anything

Every documented debug DSL name was resolved through a real process before changing the gate. 264 names in the `## Runtime namespaces` tables, all resolvable. That measurement is now the probe, so the number does not need to be remembered.

`make benchmark-chunks` was rebuilt by hand with the correct source closure before the manifest was edited, and the four semantic invariants were confirmed unchanged: `mesh_snapshot_bytes=8192`, `solid_vertices_per_chunk=2187.8`, `solid_vertex_reduction_percent=68.5`, `sync_rebuilds=0`.

## The design

Four decisions, each answering a failure that actually happened:

**A registry, not a script per document.** `docs/doc-claims.json` holds one entry per gated document with `tier`, `anchors`, `recipes`, `pins`, `verifiedAt`. The checker is generic; adding a document means adding data, not code.

**Staleness is a timestamp comparison, not a guess.** For a `trackCode` document, the checker compares the last commit that touched each anchor against the commit recorded in `verifiedAt`. If code moved and the document did not, that is staleness, and the report names the anchor and prints the recipes to re-run. No heuristic required.

**Tiers decide what a document may assert.** A dated record may cite a commit hash because tracing evidence back to its commit is the point of a record. A contract may not, because a hash is not a contract. The earlier rules said hashes were always banned while the config allowed them everywhere; the tier split makes the rule say what the repository actually does.

**Runtime names are checked by the runtime.** Static analysis of `strcmp`/`LOD_DSL_NUMBER` patterns was attempted first and rejected: it produced about 579 derivable names against 264 documented, because several namespaces dispatch through prefix matching that a regex reads too broadly. Feeding the documented names to a real binary is both narrower and stronger: it answers the only question that matters, which is whether the name still resolves.

## Costs

| Gate | Time | Needs a display |
| --- | --- | --- |
| `make check-docs` (prose + truth) | under 1 s | no |
| `make docs-probe` (264 fields, ready line, command smoke) | about 90 s | yes |

The probe skips itself without a Wayland session and exits zero, printing `skipped`. It exits non-zero on `--require-display`. It never reports `passed` for a run it did not perform, because a gate that lies about having run is worse than no gate.

## Known limits

- The probe only covers names reachable from a flat homeworld surface at play time. A field that resolves only on a planet or only after a specific transition is not exercised; the registry allows `skipFields` with a stated reason instead of loosening the assertion.
- `pins` are literal regexes. They catch a changed constant, not a changed meaning.
- The truth gate cannot tell whether prose is *correct*, only whether the things it references still exist and whether the code it describes has moved since it was checked. Semantic review is still a human job.
- `verifiedAt` is a claim that someone ran the recipes. `make docs-record` makes that claim cheap to file and impossible to verify, which is a deliberate trade: the alternative is a gate nobody can satisfy.

## What it would have caught

Each of the three defects above now fails a command:

- a missing or extra command group changes the ready line, which `make docs-probe` compares verbatim;
- a documented field that stops resolving is reported with its line number;
- a document whose anchors were edited after its `verifiedAt` is reported as `stale document`, with the recipe that re-establishes it.

The benchmark defect is different: it was a code bug, not a documentation bug, and the honest fix was to repair the build so the claim became true again.
