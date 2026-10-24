# Documentation rules

Two gates enforce the mechanical subset of these rules. `make check-docs` runs
`scripts/check-docs.mjs` (the prose gate) and `scripts/check-docs-truth.mjs` (the
truth gate) over `README.md`, `STATUS.md`, `AGENTS.md`, `docs/`, `todo/`,
`plugins/`, `assets/` and `.dsh/skills/`. CI runs the same target. `make
docs-probe` adds the runtime gate for the debug DSL. A rule that no script can
check lives here as prose and belongs to review.

## Current state has exactly one home

[STATUS.md](../STATUS.md) owns every statement about what is true right now: the
active work, what is open, what the activation target is, and what has been
verified. No other document restates it.

Any other document that needs a status heading links to `STATUS.md` instead of
carrying claims. The checker allows a status section up to 40 words of signpost; <!-- volatile-counts:allow -->
more than that is a second home and fails the run.

This split exists because duplicated status is the most common way this
repository went stale: a blocker would be closed in one document and stay open in
three others, and a reader could not tell which was current.

## Each document owns one kind of statement

| Document | Owns | Must not contain |
| --- | --- | --- |
| `STATUS.md` | What is true today, what is open, what was verified and when | Implementation narrative |
| `README.md` | What the project is, how to build and run it, feature inventory | Current status, blockers |
| `docs/*.md` | How a system works, its contracts, its design boundaries | Which parts are currently broken |
| `docs/architecture/*.md` | Stage records: boundary, tests, dated evidence, known limits | Claims about today's state |
| `todo/*.md` | Planned and accepted work, acceptance criteria, completed scope | Restated blockers |
| `.dsh/skills/*` | How to do a kind of task in this repository | Product documentation |

A dated stage record may describe its own snapshot ("on 2026-09-19 the route
passed"). It must not claim that the snapshot still holds; that claim belongs to
`STATUS.md`.

## Every document is registered, and the registry is what makes trust checkable

`docs/doc-claims.json` is the truth registry. Every gated document has one entry:

| Field | Meaning |
| --- | --- |
| `tier` | `truth`, `contract`, `record`, `plan`, `skill`, `index`, `meta`, or `snapshot` |
| `trackCode` | `true` when an edit to an `anchors` file must invalidate the document |
| `anchors` | The files whose change would make this document wrong |
| `recipes` | The commands that re-establish it after such a change |
| `pins` | Literal source facts the document quotes, as `{file, pattern}` |
| `probe` | Runtime assertions `make docs-probe` executes |
| `verifiedAt` | The commit and date this document was last checked against |
| `entryPoint` | Why a document is not linked (a skill loaded by name, `AGENTS.md`) |
| `allowCommits` | Set only when citing commits is the document's job, such as the architecture index |

The tier decides what a document may assert. `truth` is the single owner of
current state. `contract` describes behaviour that must not silently change.
`record` is dated evidence and may cite commits. `plan` is future work. `skill`
describes a workflow. `index` navigates. `meta` is this file. `snapshot` is a
captured artifact that is reference-checked but not maintained.

The truth gate fails when:

- a gated Markdown file has no registry entry, or an entry names a file that is gone;
- a document is neither reachable by link from an index nor declared as an
  `entryPoint` — an orphan document is one nobody reads, and this check is what
  found `REVIEW.md` and two stale design notes;
- an `anchors` path no longer exists, so the document describes something that moved;
- a `recipes` target is not a real make target, so the documented way to
  re-verify the document does not run;
- a `pins` pattern no longer matches, so a quoted constant was changed under it;
- a `contract`, `skill`, `index`, `meta`, or `truth` document cites a commit
  identifier that exists in this repository;
- **a `trackCode` document has an anchor newer than its `verifiedAt`.** This is
  the staleness detector: code moved, the document did not.

### When the truth gate reports a stale document

The message names the anchor that moved and the recipes to run. The repair is
mechanical:

```sh
make docs-probe            # or whichever recipe the message printed
make docs-record           # re-stamps verifiedAt with HEAD
```

`docs-record` is not a rubber stamp: it records that a human ran the recipes. It
never edits prose, and running it without reading the change is how this gate
stops being useful.

## Runtime names are checked against the runtime

`make docs-probe` reads every field name out of the `## Runtime namespaces`
tables in `docs/debug-dsl.md`, feeds each one to the real binary through
`--debug-script`, and fails if any name stops resolving. It also compares the
documented `DEBUG_CONTROL ready` line against the line the binary prints, which
is generated by `DebugControlCommandGroups()` and therefore cannot drift on its
own, and it runs the command smoke tests declared in the registry.

The probe reports `skipped` and exits zero without a Wayland session, because
resolving a runtime field needs a real frame loop. Pass `--require-display` to
turn that into a failure where skipping would hide a problem. It never reports
`passed` for a run it could not perform.

## Do not measure what changes

A claim such as "`docs/debug-dsl.md` is 1686 lines" or "47 scenes exist" rots the <!-- volatile-counts:allow -->
moment somebody edits a file, and nothing will notice. Describe how to find the
number instead:

```text
wc -l docs/debug-dsl.md
ls tests/scenes/*.dsl | wc -l
```

The prose gate reports these as `volatile count` warnings. A dated record whose
subject is a measurement (an audit, a health review, a progress log) is exempt by
path in `docs-check.config.json`; a single line can be exempted with
`<!-- volatile-counts:allow -->` when the number is genuinely the evidence.

Line-count limits enforced by `tests/test_architecture.sh` are contracts, not
measurements, so they stay in the checker's exception list and are stated as
limits ("the 2100-line limit"), never as current sizes.

## Write each paragraph once

The prose gate fails when a paragraph of 25 words or more appears verbatim in two <!-- volatile-counts:allow -->
files. Cross-reference instead of copying: a duplicated explanation drifts apart,
and the reader has no way to know which copy is authoritative.

## Link, and make the link resolve

Relative links must resolve, including `#fragment` anchors on local Markdown. A
dead link is an error. When a heading is renamed, rename its anchors too; prefer
a stable heading over a link that will break.

A link to a dated record may use a heading anchor, never a line range: line
numbers move whenever anyone edits the file above them, and a `#L37-L51` link
silently points at the wrong paragraph.

## One physical line per paragraph

Prose paragraphs occupy one physical line so a diff shows the sentence that
changed instead of a reflowed block. Tables and lists are exempt. `wrap` is `off`
in this repository because the existing documents predate the rule; do not
reflow a paragraph you are not otherwise editing.

## Commit history stays in git

A `truth`, `contract`, `skill`, `index`, or `meta` document must not contain a
commit identifier that exists in this repository. Cite what a commit established
("the fixed-step clock run"), not the hash; `git log` is the index.

A `record` or `plan` document may name a hash as an evidence pointer, because
tracing a dated measurement back to its commit is the point of a record. The
distinction is enforced: the truth gate knows each document's tier.

## Adding a document

1. Decide which row of the ownership table it belongs to.
2. Link it from an index: `docs/architecture/README.md` for a stage record,
   `todo/00-roadmap.md` for planned work, `README.md` for user-facing material.
   If a tool reads it instead of a link (a skill, `AGENTS.md`), declare
   `entryPoint` in the registry.
3. Register it in `docs/doc-claims.json`: tier, anchors, recipes, and `pins` for
   any constant it quotes.
4. Run `make check-docs` before committing, and `make docs-probe` if it documents
   runtime names.

A document nobody links to and nothing reads is a document that will be wrong
within a month.
