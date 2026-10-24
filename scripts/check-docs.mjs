#!/usr/bin/env node
/**
 * Dependency-free documentation gate for an agent-collaboration repository.
 *
 * Enforces the mechanically checkable subset of the rules in
 * `agent-collaboration-playbook/README.md`: link resolution, no commit
 * identifiers in prose, one home per status section, no duplicated paragraphs,
 * and word budgets. Every finding names a file and line so an agent can fix it
 * without interpreting the rule.
 *
 * Usage:
 *   node kit/bin/check-docs.mjs [--config PATH] [--root DIR] [--json]
 *
 * Exit code is 0 when there are no errors. Warnings never fail the run.
 */

import { execFileSync } from 'node:child_process'
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs'
import { dirname, join, relative, resolve, sep } from 'node:path'

/** Directories never descended into. */
const DEFAULT_IGNORE_DIRS = ['.git', 'node_modules', 'build', 'vendor', 'dist', 'out', '.dsh/cache']

/** Default configuration; a project config file overrides any key. */
const DEFAULT_CONFIG = {
  /** Glob-ish list of directories scanned for Markdown. */
  include: ['README.md', 'AGENTS.md', 'docs', 'todo', '.agents'],
  /** Paths or path prefixes excluded from every check. */
  ignore: [],
  /** One or more paths allowed to own a status section; empty disables the check. */
  statusOwners: [],
  /** Status headings that count as a second home for current state. */
  statusHeadings: ['current status', 'current work', 'current state', 'status', '当前状态', '当前工作'],
  /** Word ceilings; the run fails when a file exceeds its ceiling. */
  budgets: {},
  /** `off`, `warn`, or `error` for paragraphs split over several lines. */
  wrap: 'warn',
  /** Words a non-owner status section may carry and still count as a signpost. */
  statusLinkingWords: 40,
  /** Minimum word count for a paragraph to count as duplicated. */
  duplicateMinWords: 25,
  /** Set false to allow commit identifiers in prose (not recommended). */
  banCommitIds: true,
  /** `off`, `warn`, or `error` for claims about how many X currently exist. */
  volatileCounts: 'warn',
  /**
   * Paths or path prefixes exempt from the volatile-count check. A dated
   * review record's measurements ARE its content, so a rule that forbids
   * measuring would forbid the record; list those documents here instead of
   * leaving permanent noise in the run.
   */
  volatileCountIgnore: [],
  /**
   * Numbers that have no owner: file, line, scene, or test counts. A leading
   * digit boundary excludes stage and file prefixes such as `Stage 05 commit`.
   * Version numbers, dates, durations, and protocol constants never match.
   */
  volatileCountPattern:
    /(?:(?<![\w.\-/])(?<![Ss]tage\s)\d{2,}(?:,\d{3})*\+?\s*(?:lines?|files?|scenes?|tests?|packages?|commits?|words?)\b|(?<![\w.\-/])(?<![Ss]tage\s)\d{2,}(?:,\d{3})*\+?\s*(?:行|个场景|个测试|个文件|条|项))/i,
}

/** Marker that suppresses the volatile-count check on one line. */
const VOLATILE_ALLOW = /<!--\s*volatile-counts:allow\s*-->/

/**
 * Count words, treating each CJK character as one word so thresholds behave for
 * space-free scripts instead of counting a whole paragraph as a single token.
 * @param {string} text
 */
function countWords(text) {
  const cjk = text.match(/[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]/g) ?? []
  const spaced = text
    .replace(/[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]/g, ' ')
    .split(/\s+/)
    .filter(Boolean)
  return cjk.length + spaced.length
}

/**
 * Read a JSON config, falling back to defaults for absent keys.
 * @param {string} root - repository root.
 * @param {string | undefined} explicit - config path override.
 * @returns {typeof DEFAULT_CONFIG & { configPath: string | undefined }}
 */
function loadConfig(root, explicit) {
  const candidate = explicit ? resolve(root, explicit) : resolve(root, 'docs-check.config.json')
  if (!existsSync(candidate)) return { ...DEFAULT_CONFIG, configPath: undefined }
  const parsed = JSON.parse(readFileSync(candidate, 'utf8'))
  return { ...DEFAULT_CONFIG, ...parsed, configPath: candidate }
}

/**
 * Normalize a path to forward slashes so config entries match on every platform.
 * @param {string} p - absolute or relative path.
 * @param {string} root - repository root.
 * @returns {string} repository-relative path with forward slashes.
 */
function rel(p, root) {
  return relative(root, p).split(sep).join('/')
}

/**
 * Collect Markdown files under the configured include roots.
 * @param {string} root - repository root.
 * @param {typeof DEFAULT_CONFIG} config - resolved configuration.
 * @returns {string[]} absolute file paths, sorted.
 */
function collect(root, config) {
  const out = []
  const ignored = (p) => config.ignore.some((entry) => p === entry || p.startsWith(entry.replace(/\/$/, '') + '/'))
  const walk = (abs) => {
    const path = rel(abs, root)
    if (ignored(path)) return
    const stat = statSync(abs)
    if (stat.isFile()) {
      if (abs.endsWith('.md') && !abs.endsWith('.zh.md')) out.push(abs)
      return
    }
    for (const name of readdirSync(abs)) {
      if (DEFAULT_IGNORE_DIRS.includes(name)) continue
      walk(join(abs, name))
    }
  }
  for (const entry of config.include) {
    const abs = resolve(root, entry)
    if (existsSync(abs)) walk(abs)
  }
  return [...new Set(out)].sort()
}

/**
 * Split a document into prose blocks, dropping fenced code so that snippets are
 * never mistaken for prose or links. Headings and table rows are structural, so
 * each is its own block and never counts as a hard-wrapped paragraph.
 * @param {string} source - file contents.
 * @returns {{ line: number, text: string, physicalLines: number, prose: boolean }[]}
 */
function proseBlocks(source) {
  const lines = source.split('\n')
  const blocks = []
  let fence = null
  for (const [index, raw] of lines.entries()) {
    const trimmed = raw.trim()
    if (trimmed.startsWith('```') || trimmed.startsWith('~~~')) {
      fence = fence ? null : trimmed.slice(0, 3)
      continue
    }
    if (fence || trimmed === '') continue
    if (/^#{1,6}\s/.test(trimmed) || trimmed.startsWith('|') || /^([-*+]|\d+\.)\s/.test(trimmed)) {
      blocks.push({ line: index + 1, text: trimmed, physicalLines: 1, prose: false })
      continue
    }
    const previous = blocks.at(-1)
    if (
      previous &&
      previous.prose &&
      !/^#{1,6}\s/.test(previous.text) &&
      previous.line + previous.physicalLines === index + 1
    ) {
      previous.text += ' ' + trimmed
      previous.physicalLines += 1
    } else {
      blocks.push({ line: index + 1, text: trimmed, physicalLines: 1, prose: true })
    }
  }
  return blocks
}

/**
 * Extract relative link targets from a document's prose.
 * @param {string} source - file contents.
 * @returns {{ line: number, target: string }[]}
 */
function relativeLinks(source) {
  const found = []
  let fence = null
  for (const [index, line] of source.split('\n').entries()) {
    const trimmed = line.trim()
    if (trimmed.startsWith('```') || trimmed.startsWith('~~~')) {
      fence = fence ? null : trimmed.slice(0, 3)
      continue
    }
    if (fence) continue
    // Inline code is literal text, so a link written inside backticks is not a link.
    const stripped = line.replace(/`[^`]*`/g, '')
    for (const match of stripped.matchAll(/\[[^\]]*\]\(([^)\s]+)\)/g)) {
      const target = match[1]
      if (/^[a-z][a-z0-9+.-]*:/i.test(target)) continue
      if (target.startsWith('#') || target.startsWith('<')) continue
      found.push({ line: index + 1, target })
    }
  }
  return found
}

/**
 * Build the set of commit identifiers that exist in this repository.
 * @param {string} root - repository root.
 * @returns {Set<string>} abbreviated and full lowercase identifiers.
 */
function commitIds(root) {
  try {
    execFileSync('git', ['rev-parse', '--git-dir'], { cwd: root, stdio: 'ignore' })
  } catch {
    return new Set()
  }
  try {
    const full = execFileSync('git', ['rev-list', '--all'], {
      cwd: root,
      encoding: 'utf8',
      maxBuffer: 256 * 1024 * 1024,
    })
      .split('\n')
      .filter(Boolean)
    const ids = new Set(full)
    for (const id of full) for (let n = 7; n <= 40; n += 1) ids.add(id.slice(0, n))
    return ids
  } catch {
    return new Set()
  }
}

/**
 * Run every check and return findings.
 * @param {string} root - repository root.
 * @param {typeof DEFAULT_CONFIG} config - resolved configuration.
 * @returns {{ errors: string[], warnings: string[], checked: number }}
 */
function run(root, config) {
  const errors = []
  const warnings = []
  const files = collect(root, config)
  const commits = config.banCommitIds ? commitIds(root) : new Set()
  /** @type {Map<string, { file: string, line: number }[]>} */
  const paragraphs = new Map()

  for (const abs of files) {
    const file = rel(abs, root)
    const source = readFileSync(abs, 'utf8')

    // 1. Relative links resolve, including #fragment on local Markdown.
    for (const { line, target } of relativeLinks(source)) {
      const [pathPart, fragment] = target.split('#')
      const targetAbs = pathPart ? resolve(dirname(abs), pathPart) : abs
      if (!existsSync(targetAbs)) {
        errors.push(`${file}:${line}  dead link  -> ${target}`)
        continue
      }
      // GitHub line anchors (`#L12` / `#L12-L20`) are host-rendered, not headings.
      if (fragment && targetAbs.endsWith('.md') && !/^L\d+(-L\d+)?$/.test(fragment)) {
        const heading = readFileSync(targetAbs, 'utf8')
        const slug = (text) =>
          text
            .toLowerCase()
            .replace(/[`*_[\]()]/g, '')
            .trim()
            .replace(/[^\p{L}\p{N}\s-]/gu, '')
            .replace(/\s+/g, '-')
        const anchors = new Set(
          readFileSync(targetAbs, 'utf8')
            .split('\n')
            .filter((l) => /^#{1,6}\s/.test(l))
            .map((l) => slug(l.replace(/^#{1,6}\s+/, ''))),
        )
        for (const match of heading.matchAll(/<a\s+id="([^"]+)"/g)) anchors.add(match[1])
        if (!anchors.has(fragment)) errors.push(`${file}:${line}  dead anchor  -> ${target}`)
      }
    }

    // 2. No commit identifiers in prose: history belongs to git.
    if (config.banCommitIds) {
      for (const [index, line] of source.split('\n').entries()) {
        for (const match of line.matchAll(/(?<![a-z0-9])[0-9a-f]{7,40}(?![a-z0-9])/gi)) {
          if (commits.has(match[0].toLowerCase())) {
            errors.push(`${file}:${index + 1}  commit identifier in prose  -> ${match[0]} (git owns history)`)
          }
        }
      }
    }

    // 3. Current state has exactly one owner. A heading that only links to the
    // owner is a signpost, not a second home, so it is allowed; the error fires
    // when the section carries claims of its own.
    if (config.statusOwners.length > 0) {
      const ownsStatus = config.statusOwners.some((owner) => file === owner || file.startsWith(owner.replace(/\/$/, '') + '/'))
      const lines = source.split('\n')
      for (const [index, line] of lines.entries()) {
        const match = /^(#{1,6})\s+(.+)$/.exec(line)
        if (!match) continue
        const title = match[2].toLowerCase().replace(/[（(].*[）)]/g, '').trim()
        if (!config.statusHeadings.includes(title)) continue
        if (ownsStatus) continue
        const level = match[1].length
        const body = []
        for (let i = index + 1; i < lines.length; i += 1) {
          const next = /^(#{1,6})\s/.exec(lines[i])
          if (next && next[1].length <= level) break
          // A blockquote states a rule the document owns (for example which
          // source wins on conflict); it is not a restatement of current state.
          if (!/^\s*>/.test(lines[i])) body.push(lines[i])
        }
        const text = body
          .join(' ')
          .replace(/<!--[\s\S]*?-->/g, ' ')
          .replace(/\[([^\]]*)\]\([^)]*\)/g, '$1')
          .replace(/[\s*_>|`-]+/g, ' ')
          .trim()
        if (countWords(text) > config.statusLinkingWords) {
          errors.push(
            `${file}:${index + 1}  second home for current state  -> "${match[2]}" carries ${countWords(text)} words of its own (owner: ${config.statusOwners.join(', ')}; link to it instead)`,
          )
        }
      }
    }

    // 4. Paragraphs are written once.
    for (const block of proseBlocks(source)) {
      if (!block.prose) continue
      const normalized = block.text
        .replace(/\[([^\]]*)\]\([^)]*\)/g, '$1')
        .replace(/[`*_#>|-]/g, ' ')
        .toLowerCase()
        .replace(/\s+/g, ' ')
        .trim()
      if (countWords(normalized) < config.duplicateMinWords) continue
      const list = paragraphs.get(normalized) ?? []
      list.push({ file, line: block.line })
      paragraphs.set(normalized, list)
    }

    // 4b. Volatile counts: a claim about how large something currently is has no
    // owner and silently rots. Contractually pinned numbers (save formats,
    // protocol constants) are excluded by the pattern, not by convention.
    const exemptFromCounts = config.volatileCountIgnore.some(
      (entry) => file === entry || file.startsWith(entry.replace(/\/$/, '') + '/'),
    )
    if (config.volatileCounts !== 'off' && !exemptFromCounts) {
      const pattern = new RegExp(config.volatileCountPattern, 'gi')
      for (const [index, line] of source.split('\n').entries()) {
        if (VOLATILE_ALLOW.test(line)) continue
        for (const match of line.matchAll(pattern)) {
          const message = `${file}:${index + 1}  volatile count  -> ${match[0].trim()} (describe how to find it, not how big it is)`
          if (config.volatileCounts === 'error') errors.push(message)
          else warnings.push(message)
        }
      }
    }

    // 5. Word budgets.
    const ceiling = config.budgets[file]
    if (typeof ceiling === 'number') {
      const words = countWords(source)
      if (words > ceiling) errors.push(`${file}  over budget  ${words} / ${ceiling} words`)
    }

    // 6. One physical line per paragraph keeps diffs reviewable.
    if (config.wrap !== 'off') {
      for (const block of proseBlocks(source)) {
        if (block.physicalLines < 2) continue
        const message = `${file}:${block.line}  paragraph spans ${block.physicalLines} lines (soft-wrap instead)`
        if (config.wrap === 'error') errors.push(message)
        else warnings.push(message)
      }
    }
  }

  for (const [text, places] of paragraphs) {
    if (places.length < 2) continue
    const where = places.map((p) => `${p.file}:${p.line}`).join(', ')
    errors.push(`duplicated paragraph in ${places.length} files  -> ${where}  |  "${text.slice(0, 70)}..."`)
  }

  return { errors, warnings, checked: files.length }
}

const argv = process.argv.slice(2)
const rootArg = argv.indexOf('--root')
const configArg = argv.indexOf('--config')
const root = resolve(rootArg >= 0 ? argv[rootArg + 1] : process.cwd())
const config = loadConfig(root, configArg >= 0 ? argv[configArg + 1] : undefined)
const result = run(root, config)

if (argv.includes('--json')) {
  console.log(JSON.stringify(result, null, 2))
} else {
  for (const warning of result.warnings) console.log(`warn  ${warning}`)
  for (const error of result.errors) console.error(`error ${error}`)
  const suffix = config.configPath ? ` (config: ${rel(config.configPath, root)})` : ' (defaults)'
  console.log(
    `check-docs: ${result.checked} file(s), ${result.errors.length} error(s), ${result.warnings.length} warning(s)${suffix}`,
  )
}
process.exit(result.errors.length > 0 ? 1 : 0)
