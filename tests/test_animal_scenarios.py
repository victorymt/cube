"""Exercise the interactive client's pipe protocol without launching graphics."""

from contextlib import redirect_stdout
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from animal_scenarios import DebugSession  # noqa: E402


FAKE_GAME = r'''
from pathlib import Path
import sys

print("DEBUG_CONTROL ready mode=dsl", flush=True)
early = False
for source in sys.stdin:
    source = source.strip()
    if source == "die":
        break
    if source == "exit 0":
        print("DEBUG_SCRIPT exit code=0", flush=True)
        break
    if source == "bad":
        print("DEBUG_SCRIPT error source=stdin line=1 code=syntax message=bad", flush=True)
        continue
    if source == "early":
        early = True
    if source == "noisy":
        # More than a pipe buffer; the reader must drain even between commands.
        print("DEBUG_CONTROL noisy ok", flush=True)
        print("DEBUG_SCRIPT complete source=stdin", flush=True)
        for i in range(2000):
            print("render log " + "x" * 100)
        print("DEBUG_CONTROL drained", flush=True)
        continue
    if source == "screenshot":
        Path("image with spaces.png").write_bytes(b"PNG fixture")
        Path("capture report.txt").write_text("format=voxelcraft-screenshot-debug\n")
        reply = "DEBUG_CONTROL capture ok png=image with spaces.png report=capture report.txt"
        print("DEBUG_CONTROL screenshot scheduled", flush=True)
        if early:
            print(reply, flush=True)
        print("DEBUG_SCRIPT complete source=stdin", flush=True)
        if not early:
            print(reply, flush=True)
    else:
        print("DEBUG_CONTROL " + source + " ok", flush=True)
        print("DEBUG_SCRIPT complete source=stdin", flush=True)
'''


class SessionProtocolTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        binary = self.directory / "fake game"
        binary.write_text(f"#!{sys.executable}\n" + FAKE_GAME, encoding="utf-8")
        binary.chmod(0o700)
        self.session = DebugSession(binary, self.directory, "1600x1200")
        self.session.wait("DEBUG_CONTROL ready ", timeout=5)

    def tearDown(self):
        self.session.close()
        self.assertIsNotNone(self.session.process.poll())
        self.assertFalse(self.session.reader.is_alive())
        self.temporary.cleanup()

    def test_completion_is_consumed_before_next_command(self):
        for source in ("first", "second"):
            replies = self.session.command(source)
            self.assertEqual(replies, [f"DEBUG_CONTROL {source} ok", "DEBUG_SCRIPT complete source=stdin"])

    def test_both_capture_reply_orderings_and_report_format(self):
        for timing in ("late", "early"):
            self.session.command(timing)
            with redirect_stdout(io.StringIO()):
                path = self.session.screenshot(timing)
            self.assertEqual(path.read_bytes(), b"PNG fixture")
            self.assertTrue(path.with_suffix(".txt").read_text().startswith("format="))
            self.assertIn("DEBUG_CONTROL next ok", self.session.command("next"))

    def test_error_recovery(self):
        with self.assertRaisesRegex(RuntimeError, "命令失败"):
            self.session.command("bad")
        self.assertIn("DEBUG_CONTROL recovered ok", self.session.command("recovered"))

    def test_output_drains_while_not_sending_commands(self):
        self.session.command("noisy")
        self.assertEqual(self.session.wait("DEBUG_CONTROL drained", timeout=5), "DEBUG_CONTROL drained")
        self.assertIn("DEBUG_CONTROL next ok", self.session.command("next"))

    def test_early_process_exit_is_reported(self):
        with self.assertRaisesRegex(RuntimeError, "游戏输出已关闭"):
            self.session.command("die")

    def test_close_is_idempotent_and_records_replay(self):
        self.session.command("first")
        self.session.close()
        self.session.close()
        self.assertEqual((self.directory / "replay.dsl").read_text(), "first\n")


if __name__ == "__main__":
    unittest.main()
