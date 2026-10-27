"""Exercise service transitions without starting daemons or touching the router."""

import shlex
import subprocess
from pathlib import Path

SCRIPT = (
    Path(__file__).parents[1] / "freetz/package/files/root/etc/init.d/rc.tentahacle"
)


def shell_function(name: str) -> str:
    """Return one top-level shell function from the service script."""
    source = SCRIPT.read_text()
    start = source.index(name + "() {")
    end = source.index("\n}\n", start) + 3
    return source[start:end]


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


def test_shutdown_waits_until_each_process_is_reaped(tmp_path: Path) -> None:
    for function, variable, pid_variable, command in (
        ("stop_bridge", "BRIDGE_PID", "bridge_pid", "tentahacle-mqtt-bridge"),
        ("stop_watchdog", "WATCHDOG_PID", "watchdog_pid", "rc.tentahacle watchdog"),
    ):
        body = shell_function(function)
        fake_proc = tmp_path / (function + "-reap")
        fake_proc.mkdir()
        (fake_proc / "cmdline").write_text(command)
        pid_file = tmp_path / (function + "-reap.pid")
        pid_file.write_text("123\n")
        script = f"""
TESTPROC={shlex.quote(str(fake_proc))}
{variable}={shlex.quote(str(pid_file))}
checks=0
kill() {{
    if [ "$1" = -0 ]; then
        checks=$((checks + 1))
        [ "$checks" -lt 3 ]
        return
    fi
    return 0
}}
sleep() {{ :; }}
{body.replace(f'/proc/${pid_variable}', '${TESTPROC}')}
{function}
echo result:$? checks:$checks
"""
        result = subprocess.run(
            ["/bin/sh", "-s"],
            input=script,
            text=True,
            capture_output=True,
            check=False,
        )
        assert result.returncode == 0, result.stderr
        assert result.stdout.splitlines() == ["result:0 checks:3"]
        assert not pid_file.exists()


def test_recovery_never_starts_stock_while_old_aha_is_still_alive() -> None:
    script = (
        shell_function("recover_virtual_aha")
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


def test_watchdog_restarts_bridge_when_provider_is_still_healthy() -> None:
    script = (
        shell_function("watchdog_cycle")
        + """
failures=2
provider_is_healthy() { return 0; }
bridge_is_running() { return 1; }
start_bridge() { echo bridge-start; }
date() { echo now; }
watchdog_cycle
echo failures:$failures
"""
    )
    result = subprocess.run(
        ["/bin/sh", "-s"], input=script, text=True, capture_output=True, check=False
    )
    assert result.returncode == 0
    assert result.stdout.splitlines() == ["bridge-start", "failures:0"]
    assert "MQTT bridge is not running" in result.stderr


def test_watchdog_recovers_provider_then_ensures_bridge_is_running() -> None:
    script = (
        shell_function("watchdog_cycle")
        + """
failures=0
provider_is_healthy() { return 1; }
log_aha_threads() { echo thread-snapshot; }
recover_virtual_aha() { echo provider-recover; return 0; }
bridge_is_running() { return 1; }
start_bridge() { echo bridge-start; }
date() { echo now; }
watchdog_cycle
watchdog_cycle
watchdog_cycle
echo failures:$failures
"""
    )
    result = subprocess.run(
        ["/bin/sh", "-s"], input=script, text=True, capture_output=True, check=False
    )
    assert result.returncode == 0
    assert result.stdout.splitlines() == [
        "thread-snapshot",
        "provider-recover",
        "bridge-start",
        "failures:0",
    ]


def test_watchdog_logs_threads_only_after_third_provider_failure() -> None:
    script = (
        shell_function("watchdog_cycle")
        + """
failures=0
provider_is_healthy() { return 1; }
log_aha_threads() { echo thread-snapshot; }
recover_virtual_aha() { return 0; }
bridge_is_running() { return 0; }
date() { echo now; }
watchdog_cycle
watchdog_cycle
echo before-third
watchdog_cycle
"""
    )
    result = subprocess.run(
        ["/bin/sh", "-s"], input=script, text=True, capture_output=True, check=False
    )
    assert result.returncode == 0
    assert result.stdout.splitlines() == ["before-third", "thread-snapshot"]


def test_initial_provider_failure_still_starts_recovery_watchdog() -> None:
    script = (
        shell_function("start")
        + """
EXPECTED_AHA_SHA256=expected
TENTAHACLE_MQTT_HOST=broker
TENTAHACLE_MQTT_PORT=1883
TENTAHACLE_BRIDGE_ID=bridge
sha256sum() { echo 'expected  /usr/bin/aha'; }
write_mqtt_config() { :; }
recover_virtual_aha() { return 1; }
start_bridge() { echo unexpected-bridge-start; }
start_watchdog() { echo watchdog-start; }
watchdog_is_running() { return 1; }
start
"""
    )
    result = subprocess.run(
        ["/bin/sh", "-s"], input=script, text=True, capture_output=True, check=False
    )
    assert result.returncode == 0
    assert result.stdout.splitlines() == ["watchdog-start"]
    assert "watchdog will retry" in result.stderr


def test_existing_watchdog_prevents_duplicate_recovery_workers() -> None:
    script = (
        shell_function("start")
        + """
EXPECTED_AHA_SHA256=expected
TENTAHACLE_MQTT_HOST=broker
TENTAHACLE_MQTT_PORT=1883
TENTAHACLE_BRIDGE_ID=bridge
sha256sum() { echo 'expected  /usr/bin/aha'; }
write_mqtt_config() { :; }
watchdog_is_running() { return 0; }
recover_virtual_aha() { echo unexpected-recovery; }
start_watchdog() { echo unexpected-watchdog; }
start
"""
    )
    result = subprocess.run(
        ["/bin/sh", "-s"], input=script, text=True, capture_output=True, check=False
    )
    assert result.returncode == 0
    assert not result.stdout


def test_log_cap_truncates_same_file_for_open_append_writers(tmp_path: Path) -> None:
    log_file = tmp_path / "bridge.log"
    log_file.write_text("x" * 20)
    script = f"""
LOG_MAX_BYTES=10
date() {{ echo now; }}
{shell_function("cap_log")}
cap_log {shlex.quote(str(log_file))}
"""
    result = subprocess.run(
        ["/bin/sh", "-s"], input=script, text=True, capture_output=True, check=False
    )
    assert result.returncode == 0
    assert "was truncated" in log_file.read_text()
