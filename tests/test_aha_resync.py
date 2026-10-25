"""Exercise the exact embedded Lua program against a side-effect-free AHA mock."""

import ast
import re
import shutil
import subprocess
from pathlib import Path

import pytest


def test_scoped_native_screen_resync(tmp_path: Path) -> None:
    interpreter = shutil.which("lua5.1") or shutil.which("lua")
    if not interpreter:
        pytest.skip("Lua 5.1 is needed for the native AHA resync mock")
    root = Path(__file__).resolve().parents[1]
    source = (root / "freetz/package/src/aha_control.c").read_text()
    embedded = source.split("static const char resync_script[] =", 1)[1].split(
        "static int valid_profile", 1
    )[0]
    script = "".join(
        ast.literal_eval(part) for part in re.findall(r'"(?:[^"\\]|\\.)*"', embedded)
    )
    test = tmp_path / "resync.lua"
    test.write_text(
        "local resync_source=[====["
        + script
        + "]====]\n"
        + (root / "tests/aha_resync_test.lua").read_text()
    )
    subprocess.run([interpreter, str(test)], check=True, timeout=5)  # noqa: S603
