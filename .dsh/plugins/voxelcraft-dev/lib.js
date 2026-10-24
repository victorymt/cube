// Voxelcraft scene runner for DSH.
//
// This module deliberately imports only Node built-ins. The bundle is loaded
// from a local directory in the profile, so avoiding Harness package imports
// keeps activation independent of the profile's module resolution. The tool is
// registered as a plain ToolDefinition by index.js.
//
// The tool is a thin adapter over <repo>/scripts/run-scene.sh: the script owns
// the runtime isolation and the project conventions, while this module owns
// argument validation, process cancellation, artifact discovery and reporting.

import { spawn, execFile } from "node:child_process";
import { promises as fs, constants as fsConstants } from "node:fs";
import path from "node:path";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const DEFAULT_RESOLUTION = "1280x720";
const DEFAULT_TIMEOUT_SECONDS = 180;
const DEFAULT_BINARY = "build/normal/voxelcraft";
const RUN_HISTORY_DIR = path.join("screenshots", "runs");
const MAX_LOG_TAIL = 6000;
const MAX_STDERR_TAIL = 4000;

function nowStamp() {
    const d = new Date();
    const p = (n) => String(n).padStart(2, "0");
    return `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`;
}

function slug(value) {
    return value
        .replace(/[^A-Za-z0-9._-]+/g, "-")
        .replace(/^-+|-+$/g, "")
        .slice(0, 80) || "scene";
}

function tail(text, limit = MAX_LOG_TAIL) {
    if (!text) return "";
    return text.length <= limit ? text : text.slice(text.length - limit);
}

async function pathExists(target) {
    try {
        await fs.access(target);
        return true;
    } catch {
        return false;
    }
}

async function readTextIfExists(target) {
    try {
        return await fs.readFile(target, "utf8");
    } catch {
        return "";
    }
}

export function parseReport(text) {
    const fields = new Map();
    for (const line of text.split(/\r?\n/)) {
        const separator = line.indexOf("=");
        if (separator > 0) fields.set(line.slice(0, separator), line.slice(separator + 1));
    }
    const number = (key) => {
        const raw = fields.get(key);
        if (raw === undefined) return undefined;
        const value = Number(raw);
        return Number.isFinite(value) ? value : undefined;
    };
    const string = (key) => fields.get(key);
    const boolean = (key) => {
        const raw = fields.get(key);
        if (raw === "true") return true;
        if (raw === "false") return false;
        return undefined;
    };
    const vector = (key) => {
        const raw = string(key);
        if (!raw) return undefined;
        const parts = raw.split(",").map(Number);
        return parts.length === 3 && parts.every(Number.isFinite) ? parts : undefined;
    };
    return {
        seed: number("world.seed"),
        dimension: string("world.dimension"),
        surfaceTopology: string("world.surface_topology"),
        position: vector("player.position"),
        yaw: number("player.yaw_radians"),
        pitch: number("player.pitch_radians"),
        weather: string("weather.name"),
        surfaceReady: boolean("streaming.surface_ready") ?? boolean("stream.surface_ready"),
        frameId: number("capture.frame_id"),
        captureId: number("capture.id"),
        formatVersion: number("format.version"),
    };
}

async function parseCaptures(runDir) {
    const screenshots = path.join(runDir, "screenshots");
    let names;
    try {
        names = await fs.readdir(screenshots);
    } catch {
        return [];
    }
    const reports = names.filter((name) => name.endsWith(".txt")).sort();
    const captures = [];
    for (const reportName of reports) {
        const reportPath = path.join(screenshots, reportName);
        const reportText = await readTextIfExists(reportPath);
        if (!reportText) continue;
        const parsed = parseReport(reportText);
        const pngPath = path.join(screenshots, reportName.replace(/\.txt$/, ".png"));
        const capture = { report: reportPath };
        if (await pathExists(pngPath)) capture.png = pngPath;
        if (parsed.seed !== undefined) capture.seed = parsed.seed;
        if (parsed.dimension !== undefined) capture.dimension = parsed.dimension;
        if (parsed.surfaceTopology !== undefined) capture.surfaceTopology = parsed.surfaceTopology;
        if (parsed.position !== undefined) capture.position = parsed.position;
        if (parsed.yaw !== undefined) capture.yaw = parsed.yaw;
        if (parsed.pitch !== undefined) capture.pitch = parsed.pitch;
        if (parsed.weather !== undefined) capture.weather = parsed.weather;
        if (parsed.surfaceReady !== undefined) capture.surfaceReady = parsed.surfaceReady;
        if (parsed.frameId !== undefined) capture.frameId = parsed.frameId;
        if (parsed.captureId !== undefined) capture.captureId = parsed.captureId;
        if (parsed.formatVersion !== undefined) capture.formatVersion = parsed.formatVersion;
        captures.push(capture);
    }
    return captures;
}

function killGroup(child, signal) {
    if (child.pid === undefined) return;
    try {
        process.kill(-child.pid, signal);
    } catch {
        try {
            child.kill(signal);
        } catch {
            // The process already exited.
        }
    }
}

// Run the runner script with a process-group watchdog. The script has its own
// `timeout`, so the watchdog only covers a stuck script or an ignored SIGTERM.
function runRunnerScript(script, args, repo, timeoutSeconds, signal) {
    return new Promise((resolve) => {
        let child;
        try {
            child = spawn(script, args, {
                cwd: repo,
                env: process.env,
                detached: true,
                stdio: ["ignore", "pipe", "pipe"],
            });
        } catch (error) {
            resolve({ code: null, signal: null, stdout: "", stderr: String(error), timedOut: false });
            return;
        }

        const MAX_OUTPUT = 1024 * 1024;
        let stdout = "";
        let stderr = "";
        let timedOut = false;
        let settled = false;
        const append = (current, chunk) => {
            if (current.length >= MAX_OUTPUT) return current;
            return current + chunk.toString("utf8").slice(0, MAX_OUTPUT - current.length);
        };
        child.stdout?.on("data", (chunk) => {
            stdout = append(stdout, chunk);
        });
        child.stderr?.on("data", (chunk) => {
            stderr = append(stderr, chunk);
        });

        const finish = (code, exitSignal) => {
            if (settled) return;
            settled = true;
            clearTimeout(watchdog);
            signal.removeEventListener("abort", onAbort);
            resolve({ code, signal: exitSignal, stdout, stderr, timedOut });
        };

        const watchdog = setTimeout(() => {
            timedOut = true;
            killGroup(child, "SIGTERM");
            setTimeout(() => {
                killGroup(child, "SIGKILL");
                finish(null, "SIGKILL");
            }, 5000).unref();
        }, Math.max(1, timeoutSeconds) * 1000 + 60_000);

        const onAbort = () => {
            killGroup(child, "SIGTERM");
            setTimeout(() => killGroup(child, "SIGKILL"), 5000).unref();
        };
        if (signal.aborted) onAbort();
        else signal.addEventListener("abort", onAbort, { once: true });

        child.on("error", (error) => {
            stderr += `\nrunner spawn error: ${String(error)}`;
            finish(null, null);
        });
        child.on("close", (code, exitSignal) => finish(code, exitSignal));
    });
}

async function gitInfo(repo, signal) {
    try {
        const [headResult, statusResult] = await Promise.all([
            execFileAsync("git", ["-C", repo, "rev-parse", "--short=12", "HEAD"], {
                signal,
                timeout: 5000,
            }),
            execFileAsync("git", ["-C", repo, "status", "--porcelain", "--untracked-files=no"], {
                signal,
                timeout: 5000,
            }),
        ]);
        return {
            head: headResult.stdout.trim() || undefined,
            dirty: statusResult.stdout.trim().length > 0,
        };
    } catch {
        return { head: undefined, dirty: undefined };
    }
}

// The runner owns the pass/fail decision: it knows whether the scene declares a
// screenshot statement, so a benchmark or history replay without a capture is a
// pass. Prefer its manifest and only fall back to local inference when the
// manifest is unreadable (for example a startup failure before the run began).
function failureReason({ timedOut, code, captures, dslErrorLines, manifest }) {
    if (timedOut) return "voxelcraft_run watchdog expired";
    if (manifest && manifest.ok === false) {
        return manifest.failure
            ? `run-scene: ${manifest.failure}`
            : "run-scene reported failure";
    }
    if (manifest && manifest.ok === true) return undefined;
    if (dslErrorLines.length > 0) return `DSL error: ${dslErrorLines[0]}`;
    if (code !== 0) return `game exited with status ${code ?? "unknown"}`;
    if (captures.length === 0) return "no screenshot report produced";
    return undefined;
}

function parseManifest(text) {
    const fields = new Map();
    for (const line of text.split(/\r?\n/)) {
        const separator = line.indexOf("=");
        if (separator > 0) fields.set(line.slice(0, separator), line.slice(separator + 1));
    }
    const number = (key) => {
        const raw = fields.get(key);
        if (raw === undefined || raw === "") return undefined;
        const value = Number(raw);
        return Number.isFinite(value) ? value : undefined;
    };
    const boolean = (key) => {
        const raw = fields.get(key);
        if (raw === "true") return true;
        if (raw === "false") return false;
        return undefined;
    };
    if (!fields.has("format") || fields.get("format") !== "voxelcraft-run") return undefined;
    return {
        ok: boolean("ok"),
        failure: fields.get("failure") || undefined,
        status: number("status"),
        captureCount: number("capture_count"),
        reportCount: number("report_count"),
        expectsCapture: boolean("expects_capture"),
    };
}

function resolveScene(repo, scene) {
    const scenesDir = path.resolve(repo, "tests", "scenes");
    let candidate;
    if (scene.includes("/") || scene.includes(path.sep)) {
        candidate = path.resolve(repo, scene);
    } else if (scene.endsWith(".dsl")) {
        candidate = path.join(scenesDir, scene);
    } else {
        candidate = path.join(scenesDir, `${scene}.dsl`);
    }
    const resolved = path.resolve(candidate);
    if (!resolved.startsWith(scenesDir + path.sep) || !resolved.endsWith(".dsl")) {
        throw new Error(`scene must be a .dsl file under ${scenesDir}: ${scene}`);
    }
    return resolved;
}

function renderResult(value) {
    const captures = Array.isArray(value.captures) ? value.captures : [];
    const lines = [
        `voxelcraft_run ${value.ok ? "ok" : "failed"}  scene=${value.scene}  exit=${value.exitCode ?? "?"}  captures=${captures.length}`,
        `run dir: ${value.runDir}`,
    ];
    if (value.failure) lines.push(`failure: ${value.failure}`);
    for (const capture of captures) {
        const bits = [capture.png ?? capture.report];
        if (capture.seed !== undefined) bits.push(`seed=${capture.seed}`);
        if (capture.position) bits.push(`pos=${capture.position.join(",")}`);
        if (capture.weather) bits.push(`weather=${capture.weather}`);
        lines.push(`capture: ${bits.join("  ")}`);
    }
    if (value.logTail) lines.push("", "game.log tail:", value.logTail.trimEnd());
    if (value.stderrTail) lines.push("", "runner stderr tail:", value.stderrTail.trimEnd());
    lines.push("", "```json", JSON.stringify(value, null, 2), "```");
    return [{ type: "text", text: lines.join("\n") }];
}

/**
 * Create the voxelcraft_run ToolDefinition bound to a Host context.
 * @param {{ workingDirectory: { ensure(agent: unknown, signal?: AbortSignal): Promise<string> } }} ctx
 * @returns {object} registry-ready ToolDefinition
 */
export function createVoxelcraftRunTool(ctx) {
    return {
        name: "voxelcraft_run",
        description:
            "Run a Voxelcraft debug-DSL scene from tests/scenes in an isolated runtime directory and return screenshots plus parsed key=value report fields. Use for CGA, landform, volcano or other visual checks, and for reproducing a bug screenshot. Requires an active Hyprland/Wayland session and build/normal/voxelcraft. Batch mode appends `exit 0`; use a larger timeoutSeconds for slow streaming scenes.",
        parameters: {
            type: "object",
            properties: {
                scene: {
                    type: "string",
                    description:
                        "Scene name under tests/scenes (e.g. cga_classical_hall), or a .dsl path inside that directory.",
                },
                resolution: {
                    type: "string",
                    description: `Window size as WxH (default ${DEFAULT_RESOLUTION}).`,
                },
                timeoutSeconds: {
                    type: "integer",
                    description: `Wall-clock timeout in seconds (default ${DEFAULT_TIMEOUT_SECONDS}, range 1-3600).`,
                },
                trace: {
                    type: "boolean",
                    description: "Write trace.jsonl (default true).",
                },
                extraDsl: {
                    type: "string",
                    description: "Extra DSL lines appended after the scene and before the batch exit 0.",
                },
                binary: {
                    type: "string",
                    description: `Game binary relative to the repository (default ${DEFAULT_BINARY}).`,
                },
                runLabel: {
                    type: "string",
                    description: "Optional label added to the run directory name.",
                },
            },
            required: ["scene"],
        },
        output: {
            schema: {
                type: "object",
                additionalProperties: false,
                properties: {
                    ok: { type: "boolean" },
                    scene: { type: "string" },
                    runDir: { type: "string" },
                    exitCode: { type: "integer" },
                    signal: { type: "string" },
                    captures: {
                        type: "array",
                        items: {
                            type: "object",
                            additionalProperties: false,
                            properties: {
                                png: { type: "string" },
                                report: { type: "string" },
                                seed: { type: "integer" },
                                dimension: { type: "string" },
                                surfaceTopology: { type: "string" },
                                position: {
                                    type: "array",
                                    items: { type: "number" },
                                },
                                yaw: { type: "number" },
                                pitch: { type: "number" },
                                weather: { type: "string" },
                                surfaceReady: { type: "boolean" },
                                frameId: { type: "integer" },
                                captureId: { type: "integer" },
                                formatVersion: { type: "number" },
                            },
                            required: ["report"],
                        },
                    },
                    failure: { type: "string" },
                    manifestPath: { type: "string" },
                    logPath: { type: "string" },
                    tracePath: { type: "string" },
                    gitHead: { type: "string" },
                    gitDirty: { type: "boolean" },
                    expectsCapture: { type: "boolean" },
                    durationMs: { type: "integer" },
                    logTail: { type: "string" },
                    stderrTail: { type: "string" },
                },
                required: ["ok", "scene", "runDir", "captures", "durationMs"],
            },
            render: (_args, value) => renderResult(value),
        },
        async execute(args, exec) {
            if (exec.agent === undefined) throw new Error("voxelcraft_run requires a calling agent");
            if (args === null || typeof args !== "object" || Array.isArray(args)) {
                throw new Error("voxelcraft_run arguments must be an object");
            }

            const sceneArgument = typeof args.scene === "string" ? args.scene.trim() : "";
            if (!sceneArgument) throw new Error("scene must be a non-empty string");

            const repo = await ctx.workingDirectory.ensure(exec.agent, exec.signal);
            const script = path.join(repo, "scripts", "run-scene.sh");
            if (!(await pathExists(script))) {
                throw new Error(`not a Voxelcraft workspace or scripts/run-scene.sh is missing: ${repo}`);
            }
            const scenesDir = path.join(repo, "tests", "scenes");
            if (!(await pathExists(scenesDir))) {
                throw new Error(`tests/scenes is missing under ${repo}`);
            }
            const scenePath = resolveScene(repo, sceneArgument);
            if (!(await pathExists(scenePath))) {
                throw new Error(`scene not found: ${scenePath}`);
            }

            const resolution = args.resolution === undefined ? DEFAULT_RESOLUTION : String(args.resolution);
            if (!/^[0-9]+x[0-9]+$/.test(resolution)) {
                throw new Error("resolution must be WxH, e.g. 1280x720");
            }
            const [width, height] = resolution.split("x").map(Number);
            if (width < 320 || width > 7680 || height < 240 || height > 4320) {
                throw new Error("resolution out of range (320-7680 x 240-4320)");
            }

            const timeoutSeconds = args.timeoutSeconds === undefined ? DEFAULT_TIMEOUT_SECONDS : args.timeoutSeconds;
            if (!Number.isInteger(timeoutSeconds) || timeoutSeconds < 1 || timeoutSeconds > 3600) {
                throw new Error("timeoutSeconds must be an integer from 1 to 3600");
            }

            const binaryArgument = args.binary === undefined ? DEFAULT_BINARY : args.binary;
            if (typeof binaryArgument !== "string" || binaryArgument.trim() === "") {
                throw new Error("binary must be a non-empty string");
            }
            const binary = path.isAbsolute(binaryArgument)
                ? path.resolve(binaryArgument)
                : path.resolve(repo, binaryArgument);
            if (binary !== repo && !binary.startsWith(repo + path.sep)) {
                throw new Error(`binary must stay inside the repository: ${binary}`);
            }
            try {
                await fs.access(binary, fsConstants.X_OK);
            } catch {
                throw new Error(`game binary is not executable: ${binary} (run make first)`);
            }

            const sceneName = path.basename(scenePath, ".dsl");
            const runLabel = args.runLabel === undefined ? undefined : String(args.runLabel).trim();
            if (args.extraDsl !== undefined && typeof args.extraDsl !== "string") {
                throw new Error("extraDsl must be a string");
            }

            const baseName = [
                nowStamp(),
                slug(sceneName),
                runLabel ? slug(runLabel) : undefined,
            ].filter(Boolean).join("-");
            const history = path.join(repo, RUN_HISTORY_DIR);
            await fs.mkdir(history, { recursive: true });
            let runDir = path.join(history, baseName);
            for (let suffix = 2; await pathExists(runDir); suffix += 1) {
                runDir = path.join(history, `${baseName}-${suffix}`);
            }
            await fs.mkdir(runDir);

            const scriptArgs = [
                "--binary", binary,
                "--resolution", resolution,
                "--timeout", String(timeoutSeconds),
                "--run-dir", runDir,
                scenePath,
            ];
            if (args.trace === false) scriptArgs.push("--no-trace");
            if (args.extraDsl !== undefined && args.extraDsl.trim() !== "") {
                scriptArgs.push("--append-dsl", args.extraDsl.trim());
            }
            if (exec.signal.aborted) throw new Error("voxelcraft_run aborted before start");

            const startedAt = Date.now();
            const scriptResult = await runRunnerScript(script, scriptArgs, repo, timeoutSeconds, exec.signal);
            const durationMs = Date.now() - startedAt;
            const captures = await parseCaptures(runDir);
            const logPath = path.join(runDir, "game.log");
            const gameLog = await readTextIfExists(logPath);
            const dslErrorLines = gameLog
                .split(/\r?\n/)
                .filter((line) => line.startsWith("DEBUG_SCRIPT error"));
            const tracePath = path.join(runDir, "trace.jsonl");
            const manifestPath = path.join(runDir, "manifest.txt");
            const manifest = parseManifest(await readTextIfExists(manifestPath));
            const git = await gitInfo(repo, exec.signal);
            const failure = failureReason({
                timedOut: scriptResult.timedOut,
                code: scriptResult.code,
                captures,
                dslErrorLines,
                manifest,
            });
            const ok = failure === undefined;

            const result = {
                ok,
                scene: sceneName,
                runDir,
                captures,
                durationMs,
            };
            if (scriptResult.code !== null) result.exitCode = scriptResult.code;
            if (scriptResult.signal !== null) result.signal = scriptResult.signal;
            if (failure !== undefined) result.failure = failure;
            if (await pathExists(manifestPath)) result.manifestPath = manifestPath;
            if (await pathExists(logPath)) result.logPath = logPath;
            if (await pathExists(tracePath)) result.tracePath = tracePath;
            if (git.head !== undefined) result.gitHead = git.head;
            if (git.dirty !== undefined) result.gitDirty = git.dirty;
            if (manifest) result.expectsCapture = manifest.expectsCapture;
            if (gameLog) result.logTail = tail(gameLog);
            if (scriptResult.stderr.trim()) result.stderrTail = tail(scriptResult.stderr, MAX_STDERR_TAIL);
            return result;
        },
    };
}
