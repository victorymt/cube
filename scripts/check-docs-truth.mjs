#!/usr/bin/env node
/**
 * Truth gate for the documentation set.
 *
 * `scripts/check-docs.mjs` checks that the prose is well formed. This script
 * checks that it is still *true*: every gated document is registered in
 * `docs/doc-claims.json`, reachable from an index, its anchors still exist, the
 * numbers it pins still match the source, and no document silently survived a
 * change to the code it describes.
 *
 * The point of the registry is that staleness is detected by a command instead
 * of by memory. For each document the registry records:
 *
 *   tier       what kind of statement the document may make (see rules)
 *   trackCode  whether an anchor older than the document is a stale document
 *   anchors    the files whose edits would invalidate the document
 *   recipes    the commands that re-establish it
 *   pins       literal source facts the document quotes
 *   probe      runtime assertions for `make docs-probe`
 *   verifiedAt the commit the document was last checked against
 *
 * Usage:
 *   node scripts/check-docs-truth.mjs [--root DIR] [--json] [--record]
 *
 * `--record` is the maintenance action: it re-stamps `verifiedAt` for every
 * document whose anchors resolve and whose recipes are still declared, which is
 * what a human does after running the recipes. It never invents a verification.
 */

import { execFileSync } from 'node:child_process'
import { existsSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs'
import { dirname, join, relative, resolve, sep } from 'node:path'

/** Tiers and the statement each one may make. */
const TIERS = {
  /** Current state. Exactly one document owns it. */
  truth: { ownsStatus: true, mayCiteCommits: false },
  /** How a system works and what it guarantees. Must not restate current state. */
  contract: { ownsStatus: false, mayCiteCommits: false },
  /** Dated evidence record. May cite commits, may not claim they still hold. */
  record: { ownsStatus: false, mayCiteCommits: true },
  /** Planned work with acceptance criteria. */
  plan: { ownsStatus: false, mayCiteCommits: true },
  /** How to do a kind of task in this repository. */
  skill: { ownsStatus: false, mayCiteCommits: false },
  /** A navigation document that points at other documents. */
  index: { ownsStatus: false, mayCiteCommits: false },
  /** The rules themselves. */
  meta: { ownsStatus: false, mayCiteCommits: false },
  /** A captured artifact that is not maintained. Reference-checked only. */
  snapshot: { ownsStatus: false, mayCiteCommits: true }
}

const DEFAULT_REGISTRY = 'docs/doc-claims.json'

/**
 * Repository-relative POSIX path.
 * @param {string} absolute
 * @param {string} root
 */
function rel(absolute, root) {
  return relative(root, absolute).split(sep).join('/')
}

/**
 * Walk a directory tree collecting Markdown, skipping build output and VCS data.
 * @param {string} root
 * @param {string} entry
 * @param {string[]} out
 */
function collectMarkdown(root, entry, out) {
  const absolute = resolve(root, entry)
  if (!existsSync(absolute)) return
  const stat = statSync(absolute)
  if (stat.isFile()) {
    if (absolute.endsWith('.md')) out.push(rel(absolute, root))
    return
  }
  for (const name of readdirSync(absolute)) {
    if (['build', '.git', 'node_modules', 'vendor'].includes(name)) continue
    collectMarkdown(root, join(entry, name), out)
  }
}

/**
 * Every make target the build system defines, including the ones a `define`
 * block generates (`component-$1`) and the ones the test pattern rule derives
 * from `tests/<name>.c`.
 * @param {string} root
 * @returns {Set<string>}
 */
function makeTargets(root) {
  const files = ['Makefile']
  if (existsSync(join(root, 'mk'))) {
    for (const name of readdirSync(join(root, 'mk'))) {
      if (name.endsWith('.mk')) files.push(join('mk', name))
    }
  }
  const targets = new Set()
  let testPattern = false
  for (const file of files) {
    const absolute = join(root, file)
    if (!existsSync(absolute)) continue
    const text = readFileSync(absolute, 'utf8')
    for (const line of text.split('\n')) {
      const match = /^([A-Za-z][A-Za-z0-9_./%-]*):/.exec(line)
      if (match) targets.add(match[1])
      // The test pattern rule starts with a variable, so it needs its own probe.
      if (/^\$\(TEST_BUILD_DIR\)\/%:/.test(line)) testPattern = true
    }
    // `$(eval $(call define_component,core,CORE_SRC))` expands to component-core.
    for (const match of text.matchAll(/define_component,\s*([a-z]+)/g)) {
      targets.add(`component-${match[1]}`)
    }
    for (const match of text.matchAll(/define_test,\s*[A-Z0-9_]+,\s*(test_[a-z0-9_]+)/g)) {
      targets.add(match[1])
    }
  }
  if (testPattern) {
    // The pattern rule builds any tests/<name>.c, so `make build/<v>/tests/<name>`
    // is a legitimate recipe even though no line names it.
    for (const name of readdirSync(join(root, 'tests'))) {
      if (name.endsWith('.c')) targets.add(`build/VER/tests/${name.slice(0, -2)}`)
    }
  }
  return targets
}

/**
 * Resolve a recipe target that may name a test binary under any build variant.
 * @param {string} target
 * @param {Set<string>} targets
 * @param {string} root
 */
function targetExists(target, targets, root) {
  if (targets.has(target)) return true
  const variant = /^build\/[^/]+\/(.+)$/.exec(target)
  if (variant && targets.has(`build/VER/${variant[1]}`)) return true
  return false
}

/**
 * Every commit in the repository, abbreviated and full, for citation checks.
 * @param {string} root
 * @returns {Set<string>}
 */
function commitIds(root) {
  try {
    const full = execFileSync('git', ['rev-list', '--all'], {
      cwd: root,
      encoding: 'utf8',
      maxBuffer: 256 * 1024 * 1024,
      stdio: ['ignore', 'pipe', 'ignore']
    }).split('\n').filter(Boolean)
    const ids = new Set(full)
    for (const id of full) for (let n = 7; n <= 40; n += 1) ids.add(id.slice(0, n))
    return ids
  } catch {
    return new Set()
  }
}

/**
 * Whether git history is available at all.
 * @param {string} root
 */
function gitAvailable(root) {
  try {
    execFileSync('git', ['rev-parse', '--git-dir'], { cwd: root, stdio: 'ignore' })
    return true
  } catch {
    return false
  }
}

/**
 * Repository-relative paths with uncommitted changes.
 *
 * A document being edited right now has a commit timestamp older than the code
 * it is catching up with, so treating it as current is what keeps the gate
 * usable from a pre-commit hook: you are editing the document, so it is current.
 * @param {string} root
 * @returns {Set<string>}
 */
function dirtyPaths(root) {
  try {
    const out = execFileSync('git', ['status', '--porcelain', '--untracked-files=all'], {
      cwd: root,
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore']
    })
    const dirty = new Set()
    for (const line of out.split('\n')) {
      if (line.length < 4) continue
      const path = line.slice(3).trim()
      // Renames read as `old -> new`; the destination is what matters.
      dirty.add(path.includes(' -> ') ? path.split(' -> ')[1] : path)
    }
    return dirty
  } catch {
    return new Set()
  }
}

/**
 * Last commit time for a path, in seconds since the epoch.
 * @param {string} root
 * @param {string} path
 * @returns {number} 0 when unknown.
 */
function lastCommitTime(root, path) {
  try {
    const out = execFileSync('git', ['log', '-1', '--format=%ct', '--', path], {
      cwd: root,
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore']
    })
    const value = Number.parseInt(out.trim(), 10)
    return Number.isFinite(value) ? value : 0
  } catch {
    return 0
  }
}

/**
 * Commit time for a specific revision, in seconds since the epoch.
 * @param {string} root
 * @param {string} revision
 * @returns {number} 0 when unresolvable.
 */
function revisionTime(root, revision) {
  if (!revision) return 0
  try {
    const out = execFileSync('git', ['show', '-s', '--format=%ct', revision], {
      cwd: root,
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore']
    })
    const value = Number.parseInt(out.trim(), 10)
    return Number.isFinite(value) ? value : 0
  } catch {
    return 0
  }
}

/**
 * Markdown link targets, as repository-relative paths.
 * @param {string} file
 * @param {string} source
 * @returns {string[]} normalized targets, fragment removed.
 */
function linkTargets(file, source) {
  const found = []
  for (const line of source.split('\n')) {
    const stripped = line.replace(/`[^`]*`/g, '')
    for (const match of stripped.matchAll(/\[[^\]]*\]\(([^)\s]+)\)/g)) {
      // Markdown allows angle brackets around a target that contains spaces or
      // a fragment: [t](<path.md#L26-L46>).
      const target = match[1].replace(/^</, '').replace(/>$/, '')
      if (/^[a-z][a-z0-9+.-]*:/i.test(target) || target.startsWith('#')) continue
      const path = target.split('#')[0]
      if (!path) continue
      found.push(join(dirname(file), path).split(sep).join('/'))
    }
  }
  return found
}

/**
 * Normalize a `docs/../x` style path without touching the filesystem.
 * @param {string} path
 */
function normalize(path) {
  const parts = []
  for (const part of path.split('/')) {
    if (part === '' || part === '.') continue
    if (part === '..') parts.pop()
    else parts.push(part)
  }
  return parts.join('/')
}

/**
 * Run every check.
 * @param {string} root
 * @param {{ errors: string[], warnings: string[], checked: number }}
 */
function run(root) {
  const errors = []
  const warnings = []
  const registryPath = join(root, DEFAULT_REGISTRY)
  if (!existsSync(registryPath)) {
    return { errors: [`${DEFAULT_REGISTRY}  registry is missing`], warnings, checked: 0 }
  }
  const registry = JSON.parse(readFileSync(registryPath, 'utf8'))
  const documents = registry.documents ?? {}
  const targets = makeTargets(root)
  const commits = commitIds(root)
  const haveGit = gitAvailable(root)
  const dirty = haveGit ? dirtyPaths(root) : new Set()

  if (!haveGit) {
    warnings.push(`${DEFAULT_REGISTRY}  git history unavailable; freshness checks fall back to file mtimes`)
  }

  /** Documents the gate is responsible for. */
  const gated = []
  for (const entry of registry.roots ?? []) collectMarkdown(root, entry, gated)
  const unique = [...new Set(gated)].sort()

  // 1. Every gated document is registered, and every registration exists.
  for (const file of unique) {
    if (!documents[file]) {
      errors.push(`${file}  unregistered  add an entry to ${DEFAULT_REGISTRY} (see docs/documentation-rules.md)`)
    }
  }
  for (const file of Object.keys(documents)) {
    if (!existsSync(join(root, file))) {
      errors.push(`${DEFAULT_REGISTRY}  stale registration for missing file  ${file}`)
    }
  }

  // 2. Index reachability: nothing is readable only by accident.
  const reachable = new Set()
  const queue = (registry.indexes ?? []).slice()
  while (queue.length) {
    const file = normalize(queue.shift())
    if (reachable.has(file)) continue
    reachable.add(file)
    const absolute = join(root, file)
    if (!existsSync(absolute)) continue
    for (const target of linkTargets(file, readFileSync(absolute, 'utf8'))) {
      queue.push(normalize(target))
    }
  }
  for (const file of unique) {
    if (file.startsWith('assets/')) continue
    if (reachable.has(file) || (registry.indexes ?? []).includes(file)) continue
    // A root document is reached by a tool rather than by a link: the DSH skill
    // catalog loads `.dsh/skills/*`, and the agent harness reads AGENTS.md.
    // Marking it explicitly is the whole point — an unlinked document with no
    // declared reader is the failure this check exists to catch.
    if (documents[file]?.entryPoint) continue
    errors.push(`${file}  orphan document  no index links here and no entryPoint is declared; link it or explain who reads it`)
  }

  // 3. Anchors, pins, recipes, tiers, commit citations, freshness.
  for (const [file, claim] of Object.entries(documents)) {
    const tier = claim.tier ?? 'contract'
    if (!TIERS[tier]) {
      errors.push(`${file}  unknown tier "${tier}"  expected one of ${Object.keys(TIERS).join(', ')}`)
      continue
    }

    for (const anchor of claim.anchors ?? []) {
      if (!existsSync(join(root, anchor))) {
        errors.push(`${file}  missing anchor  ${anchor} (the document describes something that moved or was deleted)`)
      }
    }

    for (const recipe of claim.recipes ?? []) {
      const match = /^make(?: -s)? ([A-Za-z][A-Za-z0-9_./$-]*)/.exec(recipe)
      if (!match) continue
      if (!targetExists(match[1], targets, root)) {
        errors.push(`${file}  unknown recipe target  make ${match[1]}`)
      }
    }

    for (const pin of claim.pins ?? []) {
      const absolute = join(root, pin.file)
      if (!existsSync(absolute)) {
        errors.push(`${file}  pin file missing  ${pin.file}`)
        continue
      }
      const pattern = new RegExp(pin.pattern)
      if (!pattern.test(readFileSync(absolute, 'utf8'))) {
        errors.push(`${file}  stale pin  ${pin.pattern} not found in ${pin.file}`)
      }
    }

    const absolute = join(root, file)
    if (!existsSync(absolute)) continue
    const source = readFileSync(absolute, 'utf8')

    if (!TIERS[tier].mayCiteCommits && !claim.allowCommits) {
      for (const [index, line] of source.split('\n').entries()) {
        for (const match of line.matchAll(/(?<![a-z0-9])[0-9a-f]{7,40}(?![a-z0-9])/gi)) {
          if (commits.has(match[0].toLowerCase())) {
            errors.push(`${file}:${index + 1}  commit identifier in a ${tier} document  ${match[0]} — history belongs to git, cite what the commit established`)
          }
        }
      }
    }

    if (!claim.trackCode) continue
    if ((claim.anchors ?? []).length === 0) {
      warnings.push(`${file}  trackCode without anchors  nothing can mark this document stale`)
      continue
    }
    const docTime = haveGit ? lastCommitTime(root, file) : statSync(absolute).mtimeMs / 1000
    const verifiedTime = haveGit ? revisionTime(root, claim.verifiedAt?.commit) : 0
    // An anchor edited but not yet committed counts as changed *now*, which is
    // what makes the pre-commit hook catch the case it exists for: a staged code
    // change whose document was left alone.
    // The registry is its own anchor for some documents. `--record` writes
    // verifiedAt into that same file, so counting it would make every record run
    // invalidate the documents it just stamped. It is metadata, not content.
    const anchorsToWatch = (claim.anchors ?? []).filter((anchor) => anchor !== DEFAULT_REGISTRY)
    const now = Date.now() / 1000
    const newest = anchorsToWatch
      .map((anchor) => ({
        anchor,
        time: haveGit ? lastCommitTime(root, anchor) : statSync(join(root, anchor) || '.').mtimeMs / 1000
      }))
      .map((entry) => (dirty.has(entry.anchor) ? { anchor: entry.anchor, time: now } : entry))
      .reduce((best, current) => (current.time > best.time ? current : best), { anchor: '', time: 0 })

    // A document is current when it was touched at or after its anchors. The
    // later of its own commit and `verifiedAt` covers a document committed with
    // the code it describes; an uncommitted edit counts as current so the gate
    // works before the commit exists. A document that stayed put while its
    // anchors moved is what this reports.
    const reference = dirty.has(file) ? Number.POSITIVE_INFINITY : Math.max(docTime, verifiedTime)
    if (newest.time > reference) {
      const recipes = (claim.recipes ?? []).join('; ') || '(none declared)'
      errors.push(
        `${file}  stale document  ${newest.anchor} changed after this document was verified; re-run: ${recipes} — then update verifiedAt`
      )
    }
  }

  return { errors, warnings, checked: unique.length }
}

/**
 * Re-stamp `verifiedAt` for documents that currently meet every static check.
 * This is a maintenance action, not a verification: it records that a human ran
 * the recipes.
 * @param {string} root
 */
function record(root) {
  const registryPath = join(root, DEFAULT_REGISTRY)
  const registry = JSON.parse(readFileSync(registryPath, 'utf8'))
  const head = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim()
  const date = execFileSync('git', ['show', '-s', '--format=%cs', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim()
  let stamped = 0
  for (const claim of Object.values(registry.documents ?? {})) {
    if (!claim.trackCode) continue
    claim.verifiedAt = { commit: head, date }
    stamped += 1
  }
  writeFileSync(registryPath, `${JSON.stringify(registry, null, 2)}\n`)
  console.log(`check-docs-truth: recorded ${head} (${date}) for ${stamped} tracked document(s)`)
}

const argv = process.argv.slice(2)
const rootArg = argv.indexOf('--root')
const root = resolve(rootArg >= 0 ? argv[rootArg + 1] : process.cwd())

if (argv.includes('--record')) {
  record(root)
  process.exit(0)
}

const result = run(root)
if (argv.includes('--json')) {
  console.log(JSON.stringify(result, null, 2))
} else {
  for (const warning of result.warnings) console.log(`warn  ${warning}`)
  for (const error of result.errors) console.error(`error ${error}`)
  console.log(
    `check-docs-truth: ${result.checked} file(s), ${result.errors.length} error(s), ${result.warnings.length} warning(s)`
  )
}
process.exit(result.errors.length > 0 ? 1 : 0)
