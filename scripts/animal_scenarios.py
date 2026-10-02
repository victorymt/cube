#!/usr/bin/env -S uv run --no-project --offline --script
"""Interactive animal decision/pose checks using the game's debug DSL only."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import math
import os
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time


ROOT = Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class Scenario:
    name: str
    label: str
    values: str
    expected: str


# Energy, hydration, fatigue, stress, local food/water/shelter, storm, danger.
# Expectations are assertions only: the game chooses the action from these inputs.
SCENARIOS = (
    Scenario("calm", "安全、需求充足", "1 1 0 0 1 1 1 0 0", "idle"),
    Scenario("hungry", "饥饿、身边有食物", "0.1 1 0 0 1 1 1 0 0", "forage"),
    Scenario("food-search", "饥饿、需要寻找食物", "0.1 1 0 0 0 1 1 0 0", "seek_food"),
    Scenario("thirsty", "口渴、身边有水", "1 0.1 0 0 1 1 1 0 0", "drink"),
    Scenario("water-search", "口渴、需要寻找水源", "1 0.1 0 0 1 0 1 0 0", "seek_water"),
    Scenario("tired", "安全、疲劳", "1 1 0.9 0 1 1 1 0 0", "rest"),
    Scenario("storm", "风暴、缺少遮蔽", "1 1 0.1 0.4 1 1 0 1 0", "seek_shelter"),
    Scenario("sheltered", "风暴、已有遮蔽且疲劳", "1 1 0.8 0.2 1 1 1 1 0", "rest"),
    Scenario("alert", "饥饿时察觉威胁", "0.1 1 0 0.3 1 1 1 0 1", "alert"),
    Scenario("flee", "饥饿时遇险", "0.1 1 0 0.8 1 1 1 0 2", "flee"),
    Scenario("needs-conflict", "同时饥饿、严重口渴", "0.3 0.05 0 0 1 1 1 0 0", "drink"),
    Scenario("exhausted-threat", "极度疲劳时遇险", "1 1 1 0.9 1 1 1 0 2", "flee"),
)
POSES = ("idle", "wander", "seek_habitat", "forage", "seek_food", "drink",
         "seek_water", "rest", "seek_shelter", "alert", "flee", "hunt",
         "scavenge", "mate", "nest", "dead")
MOVING = {"wander", "seek_habitat", "seek_food", "seek_water", "seek_shelter", "flee"}


class DebugSession:
    """Drain output even while the menu waits, and await every terminal reply."""

    def __init__(self, binary: Path, directory: Path, resolution: str):
        self.directory = directory
        self.events: queue.Queue[str | None] = queue.Queue()
        self.log = (directory / "replies.log").open("w", encoding="utf-8")
        self.replay = (directory / "replay.dsl").open("w", encoding="utf-8")
        self.closed = False
        try:
            self.process = subprocess.Popen(
                [str(binary), "--debug-stdin", "--debug-resolution", resolution],
                cwd=directory, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace",
                bufsize=1,
            )
        except BaseException:
            self.log.close()
            self.replay.close()
            raise
        self.reader = threading.Thread(target=self._drain, daemon=True)
        self.reader.start()

    def _drain(self):
        try:
            for line in self.process.stdout:
                self.log.write(line)
                self.log.flush()
                if line.startswith(("DEBUG_CONTROL ", "DEBUG_SCRIPT ")):
                    self.events.put(line.rstrip("\n"))
        finally:
            self.events.put(None)

    def _next(self, deadline: float) -> str:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("等待游戏回执超时，请检查 replies.log")
        try:
            line = self.events.get(timeout=remaining)
        except queue.Empty as exc:
            raise RuntimeError("等待游戏回执超时，请检查 replies.log") from exc
        if line is None:
            raise RuntimeError(f"游戏输出已关闭，退出状态：{self.process.poll()}")
        return line

    def wait(self, prefix: str, timeout: float = 120) -> str:
        deadline = time.monotonic() + timeout
        while True:
            line = self._next(deadline)
            if line.startswith(prefix):
                return line
            if line.startswith("DEBUG_SCRIPT error "):
                raise RuntimeError(line)

    def command(self, source: str, *, expect_error: bool = False) -> list[str]:
        self.replay.write(source + "\n")
        self.replay.flush()
        try:
            self.process.stdin.write(source + "\n")
            self.process.stdin.flush()
        except (BrokenPipeError, OSError) as exc:
            raise RuntimeError("游戏控制管道已关闭") from exc
        deadline = time.monotonic() + 120
        replies = []
        while True:
            line = self._next(deadline)
            replies.append(line)
            if line.startswith("DEBUG_SCRIPT error "):
                if expect_error:
                    return replies
                raise RuntimeError(f"命令失败：{source}\n{line}")
            if line == "DEBUG_SCRIPT complete source=stdin" or line.startswith("DEBUG_SCRIPT exit code="):
                if expect_error:
                    raise RuntimeError(f"本应被拒绝的命令执行成功：{source}")
                return replies

    def screenshot(self, name: str) -> Path:
        replies = self.command("screenshot")
        prefix = "DEBUG_CONTROL capture ok png="
        # Capture may be deferred until after the block completion, or arrive before it.
        line = next((item for item in replies if item.startswith(prefix)), None)
        if line is None:
            line = self.wait(prefix)
        match = re.fullmatch(r"DEBUG_CONTROL capture ok png=(.+) report=(.+)", line)
        if not match:
            raise RuntimeError(f"无法识别截图回执：{line}")
        target = self.directory / f"{name}.png"
        for original, destination in (
            (match[1], target), (match[2], self.directory / f"{name}.txt")
        ):
            source = Path(original)
            if not source.is_absolute():
                source = self.directory / source
            shutil.copy2(source, destination)
        print(f"截图：{target}")
        return target

    def close(self):
        if self.closed:
            return
        self.closed = True
        if self.process.poll() is None:
            try:
                self.process.stdin.write("exit 0\n")
                self.process.stdin.flush()
                self.process.wait(timeout=5)
            except (BrokenPipeError, OSError, subprocess.TimeoutExpired):
                self.process.terminate()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=3)
        self.process.stdin.close()
        self.reader.join(timeout=3)
        self.process.stdout.close()
        self.log.close()
        self.replay.close()


class ScenarioMenu:
    def __init__(self, session: DebugSession, plan: str, seed: int):
        self.session = session
        self.plan = plan
        self.seed = seed
        self.scenario: Scenario | None = SCENARIOS[0]
        self.pose = "idle"
        self.frozen = False
        self.capture_count = 0

    def preview(self):
        self.session.command(f"evolution preview {self.plan} {self.seed} 100 100 100 0")
        self.session.command('assert !evolution.scenario_valid && evolution.scenario_action == "none"')

    def select(self, scenario: Scenario, *, play: bool = True):
        replies = self.session.command(f"evolution scenario {scenario.values}")
        if scenario.expected:
            self.session.command(
                f'assert evolution.scenario_valid && evolution.scenario_action == "{scenario.expected}"'
                f' && evolution.preview_action == "{scenario.expected}" && evolution.preview_frozen'
            )
        else:
            self.session.command("assert evolution.scenario_valid && evolution.preview_frozen")
        self.scenario = scenario
        self.frozen = True
        print(f"\n[{self.plan}] {scenario.label}")
        for line in replies:
            if line.startswith("DEBUG_CONTROL evolution scenario ok "):
                print(line.removeprefix("DEBUG_CONTROL evolution scenario ok "))
        if play:
            self.play()

    def play(self):
        self.session.command("evolution pose live")
        self.session.command("assert !evolution.preview_frozen")
        self.frozen = False

    def freeze(self):
        self.session.command("evolution pose freeze")
        self.session.command("assert evolution.preview_frozen")
        self.frozen = True

    def set_pose(self, action: str):
        speed = 2 if action == "flee" else 1 if action in MOVING else 0
        self.session.command(f"evolution pose {action} {speed} 0.48 0")
        self.pose = action
        self.scenario = None
        self.play()
        print(f"姿态预览：{action}（不验证情境触发或接触结果）")

    def capture(self):
        self.capture_count += 1
        name = self.scenario.name if self.scenario else f"pose-{self.pose}"
        self.session.screenshot(f"{self.capture_count:03d}-{self.plan}-{name}")

    def check_plan(self):
        for scenario in SCENARIOS:
            self.select(scenario, play=False)
            self.capture()
        # Exercise reversibility on one specimen, not just isolated starting states.
        for index in (1, 8, 9, 1, 5, 11, 5):
            self.select(SCENARIOS[index], play=False)
        print(f"通过：{self.plan} 的 12 个情境，以及警戒／逃跑解除后的恢复决策。")

    def run_checks(self):
        for plan in ("ground", "flight", "aquatic"):
            self.plan = plan
            self.preview()
            self.check_plan()
        self.select(SCENARIOS[9])
        self.session.command("let live_phase = evolution.preview_phase")
        self.session.command("wait until evolution.preview_phase != live_phase timeout 120")
        self.freeze()
        self.session.command("let frozen_phase = evolution.preview_phase")
        self.capture()
        self.session.command("assert evolution.preview_phase == frozen_phase && evolution.scenario_valid")
        self.session.command("evolution pose mate 0 0.48 0")
        self.session.command("assert !evolution.scenario_valid")
        self.session.command("exit 0")
        if self.session.process.wait(timeout=10) != 0:
            raise RuntimeError("游戏退出状态非零")
        print("通过：36 个情境截图、恢复决策、连续播放、原位暂停和姿态切换。")

    @staticmethod
    def help():
        print("\n选择情境（数字）后，游戏会计算行为并播放动作：")
        for i, scenario in enumerate(SCENARIOS, 1):
            print(f"  {i:2}. {scenario.label:<16} → {scenario.expected}")
        print("\ng 陆生 / f 飞行 / w 水生    p 暂停或播放    [ / ] 相位 -/+0.1")
        print("v 速度 / t 转向    c 自定义条件    o 单独预览姿态    r 重置当前情境")
        print("s 截图    a 巡检当前身体类型的全部情境    i 当前配置    h 菜单    q 退出")
        print("条件保持固定；危险等级直接输入。查找资源时假设附近存在更好的资源点。")

    def edit_number(self, kind: str):
        value = float(input("速度 [0,8]：" if kind == "v" else "转向 [-1,1]："))
        low, high = (0, 8) if kind == "v" else (-1, 1)
        if not math.isfinite(value) or not low <= value <= high:
            raise ValueError("数值超出范围")
        speed = str(value) if kind == "v" else "${evolution.preview_speed}"
        turn = str(value) if kind == "t" else "${evolution.preview_turn}"
        was_frozen = self.frozen
        self.session.command(f"evolution pose ${{evolution.preview_action}} {speed} ${{evolution.preview_phase}} {turn}")
        self.frozen = True
        if not was_frozen:
            self.play()

    def custom(self):
        print("输入 9 个数：能量 水分 疲劳 压力 食物 水源 遮蔽 风暴 危险等级")
        print("前 8 项范围 [0,1]；危险等级：0 安全、1 警戒、2 遇险。")
        words = input("条件：").split()
        if len(words) != 9:
            raise ValueError("需要恰好 9 个数")
        values = [float(word) for word in words[:8]]
        if any(not math.isfinite(v) or not 0 <= v <= 1 for v in values) or words[8] not in ("0", "1", "2"):
            raise ValueError("条件数值无效")
        text = " ".join(f"{value:.6g}" for value in values) + " " + words[8]
        self.select(Scenario("custom", "自定义条件", text, ""))

    def interactive(self):
        self.preview()
        self.select(SCENARIOS[0])
        self.help()
        while True:
            try:
                choice = input(f"\n{self.plan} / {'暂停' if self.frozen else '播放'} > ").strip().lower()
                if choice == "q":
                    return
                if choice in ("h", "?"):
                    self.help()
                elif choice.isdecimal() and 1 <= int(choice) <= len(SCENARIOS):
                    self.select(SCENARIOS[int(choice) - 1])
                elif choice in ("g", "f", "w", "r"):
                    self.plan = {"g": "ground", "f": "flight", "w": "aquatic"}.get(choice, self.plan)
                    self.preview()
                    if self.scenario:
                        self.select(self.scenario)
                    else:
                        self.set_pose(self.pose)
                elif choice == "p":
                    self.play() if self.frozen else self.freeze()
                elif choice in ("[", "]"):
                    delta = "+ 0.1" if choice == "]" else "+ 0.9"
                    self.session.command(
                        "evolution pose ${evolution.preview_action} ${evolution.preview_speed} "
                        f"${{(evolution.preview_phase {delta}) % 1}} ${{evolution.preview_turn}}"
                    )
                    self.frozen = True
                elif choice in ("v", "t"):
                    self.edit_number(choice)
                elif choice == "c":
                    self.custom()
                elif choice == "o":
                    print("可选姿态：" + ", ".join(POSES))
                    action = input("动作：").strip()
                    if action not in POSES:
                        raise ValueError("未知动作")
                    # Clear any previous scenario, including when the action is unchanged.
                    self.preview()
                    self.set_pose(action)
                elif choice == "s":
                    self.capture()
                elif choice == "a":
                    self.check_plan()
                elif choice == "i":
                    print(f"身体：{self.plan}，种子：{self.seed}")
                    print(f"情境：{self.scenario.label} / {self.scenario.values}" if self.scenario else f"姿态预览：{self.pose}")
                    replies = self.session.command(
                        "evolution pose freeze" if self.frozen else "evolution pose live"
                    )
                    print("\n".join(line for line in replies if line.startswith("DEBUG_CONTROL ")))
                elif choice:
                    print("无此选项，输入 h 查看菜单。")
            except ValueError as exc:
                print(f"输入无效：{exc}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/normal/voxelcraft")
    parser.add_argument("--no-build", action="store_true", help="use an already built binary")
    parser.add_argument("--check", action="store_true", help="assert all scenarios and capture all three body plans")
    parser.add_argument("--list", action="store_true", help="show the menu without starting a game")
    parser.add_argument("--plan", choices=("ground", "flight", "aquatic"), default="ground")
    parser.add_argument("--seed", type=int, default=123)
    parser.add_argument("--resolution", default="1600x1200")
    args = parser.parse_args()
    if args.list:
        ScenarioMenu.help()
        return 0
    if not 0 <= args.seed <= 0xFFFFFFFF:
        parser.error("seed must be in [0, 4294967295]")
    dimensions = re.fullmatch(r"([0-9]+)x([0-9]+)", args.resolution)
    if not dimensions or not (320 <= int(dimensions[1]) <= 7680 and 240 <= int(dimensions[2]) <= 4320):
        parser.error("resolution must be WIDTHxHEIGHT, within 320x240–7680x4320")
    if not os.environ.get("HYPRLAND_INSTANCE_SIGNATURE") or not os.environ.get("WAYLAND_DISPLAY"):
        parser.error("需要在活动的 Hyprland/Wayland 会话中运行")
    socket = Path(os.environ.get("XDG_RUNTIME_DIR", "/nonexistent")) / os.environ["WAYLAND_DISPLAY"]
    if not socket.is_socket():
        parser.error(f"Wayland socket 不可用：{socket}")
    if not args.no_build:
        try:
            subprocess.run(["make", "-j4"], cwd=ROOT, check=True)
        except (OSError, subprocess.SubprocessError) as exc:
            print(f"构建失败：{exc}", file=sys.stderr)
            return 1
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f"游戏程序不存在：{binary}")
    directory = Path(tempfile.mkdtemp(prefix="voxelcraft-animal-scenarios-"))
    (directory / "assets").symlink_to(ROOT / "assets", target_is_directory=True)
    print(f"日志、截图、可重放 DSL：{directory}", flush=True)
    session = None
    try:
        session = DebugSession(binary, directory, args.resolution)
        session.wait("DEBUG_CONTROL ready ")
        session.command("start")
        session.command('wait until game.screen == "playing" timeout 7200')
        menu = ScenarioMenu(session, args.plan, args.seed)
        if args.check:
            menu.run_checks()
        else:
            menu.interactive()
        return 0
    except (EOFError, KeyboardInterrupt):
        print("\n已结束交互。")
        return 0
    except (RuntimeError, OSError, subprocess.SubprocessError) as exc:
        print(f"调试失败：{exc}", file=sys.stderr)
        return 1
    finally:
        if session:
            session.close()
        print(f"保留调试产物：{directory}")


if __name__ == "__main__":
    sys.exit(main())
