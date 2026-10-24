# Documentation rules

The mechanical subset of these rules is enforced by `make check-docs`, which runs
`scripts/check-docs.mjs` over `README.md`, `AGENTS.md`, `docs/`, `todo/` and
`.dsh/skills/`. CI runs the same target. A rule that a script cannot check lives
here as prose and belongs to review.

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

## Do not measure what changes

A claim such as "`docs/debug-dsl.md` is 1686 lines" or "47 scenes exist" rots the <!-- volatile-counts:allow -->
moment somebody edits a file, and nothing will notice. Describe how to find the
number instead:

```text
wc -l docs/debug-dsl.md
ls tests/scenes/*.dsl | wc -l
```

The checker reports these as `volatile count` warnings. A dated record whose
subject is a measurement (an audit, a health review, a progress log) is exempt by
path in `docs-check.config.json`; a single line can be exempted with
`<!-- volatile-counts:allow -->` when the number is genuinely the evidence.

Line-count limits enforced by `tests/test_architecture.sh` are contracts, not
measurements, so they stay in the checker's exception list and are stated as
limits ("the 2100-line limit"), never as current sizes.

## Write each paragraph once

The checker fails when a paragraph of 25 words or more appears verbatim in two <!-- volatile-counts:allow -->
files. Cross-reference instead of copying: a duplicated explanation drifts apart,
and the reader has no way to know which copy is authoritative.

## Link, and make the link resolve

Relative links must resolve, including `#fragment` anchors on local Markdown. A
dead link is an error. When a heading is renamed, rename its anchors too; prefer
a stable heading over a link that will break.

## One physical line per paragraph

Prose paragraphs occupy one physical line so a diff shows the sentence that
changed instead of a reflowed block. Tables and lists are exempt. `wrap` is `off`
in this repository because the existing documents predate the rule; do not
reflow a paragraph you are not otherwise editing.

## Commit history stays in git

The checker flags a commit identifier that exists in this repository when it
appears in prose. Cite what a commit established ("the fixed-step clock run"),
not the hash; `git log` is the index. Dated stage records may name the date and
the command instead.

## Adding a document

1. Decide which row of the ownership table it belongs to.
2. Link it from an index: `docs/architecture/README.md` for a stage record,
   `todo/00-roadmap.md` for planned work, `README.md` for user-facing material.
3. Run `make check-docs` before committing.

A document nobody links to is a document nobody reads.
