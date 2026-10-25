"""Exercise service transitions without starting daemons or touching the router."""

import shlex
import subprocess
from pathlib import Path

SCRIPT = (
    Path(__file__).parents[1] / "freetz/package/files/root/etc/init.d/rc.tentahacle"
)


def transition(action: str, stop_result: int = 0) -> subprocess.CompletedProcess:
    source = SCRIPT.read_text().replace(". /etc/init.d/modlibrc", ":")
    # Model the presence of the virtual-provider socket without opening one.
    source = source.replace(
        "[ -S /var/tmp/aha-virtual-provider.ctl ]", "provider_present"
    )
    definitions, dispatch = source.rsplit('\ncase "$1" in', 1)
    mocks = f"""
stop_watchdog() {{ echo watchdog-stop; return {stop_result}; }}
stop_bridge() {{ echo bridge-stop; }}
start_stock_aha() {{ echo stock-start; }}
start() {{ echo virtual-start; }}
modlib_stop() {{ stop; }}
modlib_start() {{ start; }}
provider_present() {{ return 0; }}
pidof() {{ return 1; }}
wait_for_aha_stop() {{ return 0; }}
"""
    return subprocess.run(  # noqa: S603 - repository script and fixed test arguments
        ["/bin/sh", "-s", "--", action],
        input=definitions + mocks + '\ncase "$1" in' + dispatch,
        text=True,
        capture_output=True,
        check=False,
    )


def test_restart_does_not_start_stock_aha_between_virtual_instances() -> None:
    result = transition("restart")
    assert result.returncode == 0, result.stderr
    assert result.stdout.splitlines() == [
        "watchdog-stop",
        "bridge-stop",
        "virtual-start",
    ]


def test_explicit_stop_restores_stock_aha() -> None:
    result = transition("stop")
    assert result.returncode == 0, result.stderr
    assert result.stdout.splitlines() == ["watchdog-stop", "bridge-stop", "stock-start"]


def test_failed_stop_prevents_starting_a_second_instance() -> None:
    result = transition("restart", stop_result=1)
    assert result.returncode != 0
    assert result.stdout.splitlines() == ["watchdog-stop"]


def test_failed_shutdown_keeps_pid_for_retry(tmp_path: Path) -> None:
    for function, variable, pid_variable, command in (
        ("stop_bridge", "BRIDGE_PID", "bridge_pid", "tentahacle-mqtt-bridge"),
        ("stop_watchdog", "WATCHDOG_PID", "watchdog_pid", "rc.tentahacle watchdog"),
    ):
        source = SCRIPT.read_text()
        start = source.index(function + "() {")
        end = source.index("\n}\n", start) + 3
        body = source[start:end].replace(f"/proc/${pid_variable}", "${TESTPROC}")
        fake_proc = tmp_path / function
        fake_proc.mkdir()
        (fake_proc / "cmdline").write_text(command)
        pid_file = tmp_path / (function + ".pid")
        pid_file.write_text("123\n")
        script = f"""
TESTPROC={shlex.quote(str(fake_proc))}
{variable}={shlex.quote(str(pid_file))}
kill() {{ :; }}
sleep() {{ :; }}
{body}
{function}; echo first:$?
{function}; echo second:$?
rm "$TESTPROC/cmdline"
rmdir "$TESTPROC"
{function}; echo third:$?
"""
        result = subprocess.run(
            ["/bin/sh", "-s"], input=script, text=True, capture_output=True, check=False
        )
        assert result.stdout.splitlines() == ["first:1", "second:1", "third:0"]
        assert not pid_file.exists()


def test_recovery_never_starts_stock_while_old_aha_is_still_alive() -> None:
    source = SCRIPT.read_text()
    start = source.index("recover_virtual_aha() {")
    end = source.index("\n}\n", start) + 3
    script = (
        source[start:end]
        + """
waits=0
pidof() { echo 123; }
kill() { :; }
wait_for_aha_stop() { waits=$((waits+1)); [ "$waits" -eq 1 ]; }
start_virtual_aha() { return 1; }
start_stock_aha() { echo unsafe-stock-start; }
recover_virtual_aha
"""
    )
    result = subprocess.run(
        ["/bin/sh", "-s"], input=script, text=True, capture_output=True, check=False
    )
    assert result.returncode != 0
    assert "unsafe-stock-start" not in result.stdout
