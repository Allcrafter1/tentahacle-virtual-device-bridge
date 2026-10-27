# SPDX-License-Identifier: MIT OR Apache-2.0
"""Exercise actual bridge timer methods without requiring a Home Assistant install."""

import ast
import asyncio
import math
from datetime import UTC, datetime
from pathlib import Path
from types import MethodType, SimpleNamespace
from unittest.mock import AsyncMock, Mock

import pytest

from custom_components.tentahacle.const import (
    CONF_PROFILE,
    CONF_SOURCE_ENTITY_ID,
    DeviceProfile,
)
from custom_components.tentahacle.protocol import ProtocolError
from custom_components.tentahacle.thermostat import (
    ThermostatOverride,
    timer_request,
)


def bridge_methods() -> dict:
    """Load unchanged method ASTs, isolating Home Assistant import dependencies."""
    source = Path("custom_components/tentahacle/bridge.py")
    tree = ast.parse(source.read_text())
    bridge = next(node for node in tree.body if isinstance(node, ast.ClassDef))
    names = {
        "async_stop",
        "_async_sync_bridge",
        "_async_schedule_tick",
        "_async_publish_schedule",
        "_async_apply_override",
        "_async_handle_timer",
        "_async_finish_override",
        "_async_resume_overrides",
        "_schedule_override",
    }
    methods = [
        node
        for node in bridge.body
        if isinstance(node, (ast.AsyncFunctionDef, ast.FunctionDef))
        and node.name in names
    ]
    module = ast.Module(
        body=[
            ast.ImportFrom(
                module="__future__", names=[ast.alias(name="annotations")], level=0
            ),
            *methods,
        ],
        type_ignores=[],
    )
    namespace = {
        "math": math,
        "_LOGGER": Mock(),
        "ThermostatOverride": ThermostatOverride,
        "timer_request": timer_request,
        "ProtocolError": ProtocolError,
        "CONF_SOURCE_ENTITY_ID": CONF_SOURCE_ENTITY_ID,
        "CONF_PROFILE": CONF_PROFILE,
        "DeviceProfile": DeviceProfile,
        "CONF_SCHEDULE_WEEK_PROFILE": "schedule_week_profile",
        "CONF_SCHEDULE_WORKDAYS": "schedule_workdays",
        "CONF_SCHEDULE_HOLIDAYS": "schedule_holidays",
        "mqtt": SimpleNamespace(async_publish=AsyncMock()),
        "dt_util": SimpleNamespace(
            utcnow=lambda: datetime.fromtimestamp(1000, UTC),
        ),
    }
    # Execute only checked-in method definitions; no user-controlled source.
    exec(compile(ast.fix_missing_locations(module), str(source), "exec"), namespace)  # noqa: S102
    return namespace


def test_missing_schedule_adapter_clears_retained_schedule() -> None:
    methods = bridge_methods()
    bridge = SimpleNamespace(
        hass=SimpleNamespace(states=SimpleNamespace(get=lambda _: None)),
        topic_prefix="tentahacle/lab",
    )
    mapping = {
        CONF_PROFILE: DeviceProfile.THERMOSTAT.value,
        CONF_SOURCE_ENTITY_ID: "climate.room",
    }
    asyncio.run(methods["_async_publish_schedule"](bridge, "uid", mapping))
    methods["mqtt"].async_publish.assert_awaited_once_with(
        bridge.hass,
        "tentahacle/lab/device/uid/schedule/set",
        "disabled",
        qos=1,
        retain=True,
    )


def timer_bridge() -> SimpleNamespace:
    """Provide the dependencies used by the actual timer lifecycle methods."""
    return SimpleNamespace(
        _stopping=False,
        _unsubscribers=[],
        _override_lock=asyncio.Lock(),
        _sync_lock=asyncio.Lock(),
        _overrides={},
        _override_timers={},
        _mappings={"uid": {CONF_SOURCE_ENTITY_ID: "climate.room"}},
        hass=SimpleNamespace(
            states=SimpleNamespace(
                get=lambda _: SimpleNamespace(
                    state="heat", attributes={"temperature": 20}
                )
            ),
            services=SimpleNamespace(async_call=AsyncMock()),
        ),
        _async_save_overrides=AsyncMock(),
        _async_apply_override=AsyncMock(),
        _async_publish_timer=AsyncMock(),
        _schedule_override=Mock(),
    )


@pytest.mark.parametrize(
    "failed_step", ["_async_apply_override", "_async_publish_timer"]
)
def test_new_timer_keeps_expiry_when_action_or_feedback_fails(failed_step: str) -> None:
    bridge = timer_bridge()
    getattr(bridge, failed_step).side_effect = ConnectionError("offline")
    with pytest.raises(ConnectionError):
        asyncio.run(
            bridge_methods()["_async_handle_timer"](
                bridge,
                "uid",
                {CONF_SOURCE_ENTITY_ID: "climate.room"},
                {"timer_action": "boost", "end_time": 1100},
            )
        )
    bridge._schedule_override.assert_called_once_with("uid", 1100)  # noqa: SLF001
    assert bridge._overrides["uid"].previous_temperature == 20  # noqa: SLF001


def test_resumed_timer_keeps_expiry_when_feedback_fails() -> None:
    bridge = timer_bridge()
    bridge._overrides["uid"] = ThermostatOverride(  # noqa: SLF001
        "boost", 1100, "climate.room", "heat", 20
    )
    bridge._async_publish_timer.side_effect = ConnectionError("offline")  # noqa: SLF001
    asyncio.run(bridge_methods()["_async_resume_overrides"](bridge))
    bridge._schedule_override.assert_called_once_with("uid", 1100)  # noqa: SLF001


@pytest.mark.parametrize("failed_service", [1, 2, 3])
def test_expiry_retries_restoration_and_retains_original_state(
    failed_service: int,
) -> None:
    bridge = timer_bridge()
    override = ThermostatOverride("boost", 999, "climate.room", "auto", 20, "schedule")
    bridge._overrides["uid"] = override  # noqa: SLF001
    cancelled = Mock()
    bridge._override_timers["uid"] = cancelled  # noqa: SLF001
    bridge.hass.services.async_call.side_effect = [
        *([None] * (failed_service - 1)),
        ConnectionError("offline"),
    ]
    methods = bridge_methods()
    with pytest.raises(ConnectionError):
        asyncio.run(methods["_async_finish_override"](bridge, "uid"))
    cancelled.assert_called_once()
    bridge._schedule_override.assert_called_once_with("uid", 1060)  # noqa: SLF001
    assert bridge._overrides["uid"] == override  # noqa: SLF001
    bridge._async_publish_timer.assert_not_called()  # noqa: SLF001
    bridge.hass.services.async_call.side_effect = None
    asyncio.run(methods["_async_finish_override"](bridge, "uid"))
    assert not bridge._overrides  # noqa: SLF001
    bridge._async_publish_timer.assert_awaited_once_with("uid", "cancel", 0)  # noqa: SLF001


@pytest.mark.parametrize("temperature", [float("nan"), float("inf"), 10**400])
def test_timer_rejects_unrestorable_source_temperature(temperature: float) -> None:
    bridge = timer_bridge()
    bridge.hass.states.get = lambda _: SimpleNamespace(
        state="heat", attributes={"temperature": temperature}
    )
    with pytest.raises(ProtocolError):
        asyncio.run(
            bridge_methods()["_async_handle_timer"](
                bridge,
                "uid",
                {CONF_SOURCE_ENTITY_ID: "climate.room"},
                {"timer_action": "boost", "end_time": 1100},
            )
        )
    bridge._async_apply_override.assert_not_called()  # noqa: SLF001
    assert not bridge._overrides  # noqa: SLF001


def test_resume_feedback_failure_does_not_skip_later_timer() -> None:
    bridge = timer_bridge()
    bridge._overrides = {  # noqa: SLF001
        uid: ThermostatOverride("boost", 1100, f"climate.{uid}", "heat", 20)
        for uid in ("first", "second")
    }
    bridge._mappings = {  # noqa: SLF001
        uid: {CONF_SOURCE_ENTITY_ID: f"climate.{uid}"} for uid in ("first", "second")
    }
    bridge._async_publish_timer.side_effect = [ConnectionError("offline"), None]  # noqa: SLF001
    asyncio.run(bridge_methods()["_async_resume_overrides"](bridge))
    assert bridge._schedule_override.call_args_list == [  # noqa: SLF001
        (("first", 1100),),
        (("second", 1100),),
    ]


@pytest.mark.parametrize("has_previous", [False, True])
def test_failed_timer_save_preserves_previous_runtime_state(
    *, has_previous: bool
) -> None:
    bridge = timer_bridge()
    previous = ThermostatOverride("cold", 1050, "climate.room", "auto", 21)
    if has_previous:
        bridge._overrides["uid"] = previous  # noqa: SLF001
    old_expiry = Mock()
    bridge._override_timers["uid"] = old_expiry  # noqa: SLF001
    bridge._async_save_overrides.side_effect = OSError("disk full")  # noqa: SLF001
    with pytest.raises(OSError):
        asyncio.run(
            bridge_methods()["_async_handle_timer"](
                bridge,
                "uid",
                {CONF_SOURCE_ENTITY_ID: "climate.room"},
                {"timer_action": "boost", "end_time": 1200},
            )
        )
    assert bridge._overrides.get("uid") == (previous if has_previous else None)  # noqa: SLF001
    assert bridge._override_timers["uid"] is old_expiry  # noqa: SLF001
    bridge._schedule_override.assert_not_called()  # noqa: SLF001
    bridge._async_apply_override.assert_not_called()  # noqa: SLF001


@pytest.mark.parametrize("action", ["boost", "cold"])
@pytest.mark.parametrize("has_previous", [False, True])
def test_successful_timer_preserves_snapshot_and_updates_expiry(
    action: str, *, has_previous: bool
) -> None:
    bridge = timer_bridge()
    previous = ThermostatOverride("cold", 1050, "climate.room", "auto", 21)
    if has_previous:
        bridge._overrides["uid"] = previous  # noqa: SLF001
    asyncio.run(
        bridge_methods()["_async_handle_timer"](
            bridge,
            "uid",
            {CONF_SOURCE_ENTITY_ID: "climate.room"},
            {"timer_action": action, "end_time": 1200},
        )
    )
    saved = bridge._overrides["uid"]  # noqa: SLF001
    assert saved.action == action
    assert saved.end_time == 1200
    assert saved.previous_temperature == (21 if has_previous else 20)
    assert saved.previous_hvac_mode == ("auto" if has_previous else "heat")
    bridge._schedule_override.assert_called_once_with("uid", 1200)  # noqa: SLF001
    bridge._async_publish_timer.assert_awaited_once_with("uid", action, 1200)  # noqa: SLF001


def test_resume_ignores_removed_mapping_and_keeps_other_timer() -> None:
    bridge = timer_bridge()
    bridge._overrides = {  # noqa: SLF001
        uid: ThermostatOverride("cold", 1100, "climate.room", "heat", 20)
        for uid in ("removed", "uid")
    }
    asyncio.run(bridge_methods()["_async_resume_overrides"](bridge))
    assert set(bridge._overrides) == {"uid"}  # noqa: SLF001
    bridge._schedule_override.assert_called_once_with("uid", 1100)  # noqa: SLF001


def scheduled_bridge() -> tuple[SimpleNamespace, list, dict]:
    """Bind actual timer methods and record scheduler callbacks."""
    bridge = timer_bridge()
    callbacks = []
    methods = bridge_methods()

    def schedule(_hass: object, _delay: float, callback: object) -> Mock:
        callbacks.append(callback)
        return Mock()

    methods["async_call_later"] = schedule
    for name in ("_schedule_override", "_async_finish_override"):
        setattr(bridge, name, MethodType(methods[name], bridge))
    return bridge, callbacks, methods


def test_resume_serializes_slow_apply_with_expiry() -> None:
    async def scenario() -> None:
        bridge, callbacks, methods = scheduled_bridge()
        bridge._overrides["uid"] = ThermostatOverride(  # noqa: SLF001
            "boost", 1100, "climate.room", "heat", 20
        )
        entered = asyncio.Event()
        release = asyncio.Event()
        events = []

        async def apply(*_args: object) -> None:
            entered.set()
            await release.wait()
            events.append("boost")

        async def restore(*_args: object, **_kwargs: object) -> None:
            events.append("restore")

        bridge._async_apply_override = apply  # noqa: SLF001
        bridge.hass.services.async_call = restore
        resume = asyncio.create_task(methods["_async_resume_overrides"](bridge))
        await entered.wait()
        expiry = asyncio.create_task(callbacks[0](None))
        await asyncio.sleep(0)
        early = list(events)
        release.set()
        await asyncio.gather(resume, expiry)
        assert early == []
        assert events == ["boost", "restore", "restore"]
        assert not bridge._overrides  # noqa: SLF001

    asyncio.run(scenario())


def test_waiting_old_expiry_cannot_finish_replacement_timer() -> None:
    async def scenario() -> None:
        bridge, callbacks, _methods = scheduled_bridge()
        bridge._async_finish_override = AsyncMock()  # noqa: SLF001
        bridge._schedule_override("uid", 1100)  # noqa: SLF001
        async with bridge._override_lock:  # noqa: SLF001
            old = asyncio.create_task(callbacks[0](None))
            await asyncio.sleep(0)
            bridge._schedule_override("uid", 1200)  # noqa: SLF001
        await old
        bridge._async_finish_override.assert_not_awaited()  # noqa: SLF001
        await callbacks[1](None)
        bridge._async_finish_override.assert_awaited_once_with("uid")  # noqa: SLF001

    asyncio.run(scenario())


@pytest.mark.parametrize("removed", [False, True])
def test_resume_rechecks_override_after_waiting_for_lock(*, removed: bool) -> None:
    async def scenario() -> None:
        bridge = timer_bridge()
        bridge._overrides["uid"] = ThermostatOverride(  # noqa: SLF001
            "boost", 1100, "climate.room", "heat", 20
        )
        async with bridge._override_lock:  # noqa: SLF001
            resume = asyncio.create_task(
                bridge_methods()["_async_resume_overrides"](bridge)
            )
            await asyncio.sleep(0)
            if removed:
                bridge._overrides.pop("uid")  # noqa: SLF001
            else:
                bridge._overrides["uid"] = ThermostatOverride(  # noqa: SLF001
                    "cold", 1200, "climate.room", "heat", 20
                )
        await resume
        if removed:
            bridge._async_apply_override.assert_not_awaited()  # noqa: SLF001
            bridge._schedule_override.assert_not_called()  # noqa: SLF001
        else:
            bridge._schedule_override.assert_called_once_with("uid", 1200)  # noqa: SLF001
            bridge._async_publish_timer.assert_awaited_once_with(  # noqa: SLF001
                "uid", "cold", 1200
            )

    asyncio.run(scenario())


@pytest.mark.parametrize(
    "maximum", [float("nan"), float("inf"), -float("inf"), 10**400]
)
def test_boost_never_sends_nonfinite_or_overflowing_temperature(
    maximum: object,
) -> None:
    bridge = timer_bridge()
    bridge.hass.states.get = lambda _: SimpleNamespace(attributes={"max_temp": maximum})
    asyncio.run(
        bridge_methods()["_async_apply_override"](
            bridge,
            {CONF_SOURCE_ENTITY_ID: "climate.room"},
            ThermostatOverride("boost", 1100, "climate.room", "heat", 20),
        )
    )
    data = bridge.hass.services.async_call.call_args.args[2]
    assert data["temperature"] == 28
    assert math.isfinite(data["temperature"])


@pytest.mark.parametrize("maximum", [25, 30.5, 35])
def test_boost_preserves_valid_source_maximum(maximum: float) -> None:
    bridge = timer_bridge()
    bridge.hass.states.get = lambda _: SimpleNamespace(attributes={"max_temp": maximum})
    asyncio.run(
        bridge_methods()["_async_apply_override"](
            bridge,
            {CONF_SOURCE_ENTITY_ID: "climate.room"},
            ThermostatOverride("boost", 1100, "climate.room", "heat", 20),
        )
    )
    assert bridge.hass.services.async_call.call_args.args[2]["temperature"] == maximum


def test_queued_timer_command_cannot_restart_after_unload() -> None:
    async def scenario() -> None:
        bridge, callbacks, methods = scheduled_bridge()
        async with bridge._override_lock:  # noqa: SLF001
            command = asyncio.create_task(
                methods["_async_handle_timer"](
                    bridge,
                    "uid",
                    {CONF_SOURCE_ENTITY_ID: "climate.room"},
                    {"timer_action": "boost", "end_time": 1100},
                )
            )
            await asyncio.sleep(0)
            stop = asyncio.create_task(methods["async_stop"](bridge))
            await asyncio.sleep(0)
        await asyncio.gather(command, stop)
        assert not callbacks
        assert not bridge._overrides  # noqa: SLF001
        bridge._async_apply_override.assert_not_awaited()  # noqa: SLF001

    asyncio.run(scenario())


def test_unload_waits_for_active_timer_and_preserves_restart_state() -> None:
    async def scenario() -> None:
        bridge, callbacks, methods = scheduled_bridge()
        entered = asyncio.Event()
        release = asyncio.Event()

        async def apply(*_args: object) -> None:
            entered.set()
            await release.wait()

        bridge._async_apply_override.side_effect = apply  # noqa: SLF001
        command = asyncio.create_task(
            methods["_async_handle_timer"](
                bridge,
                "uid",
                {CONF_SOURCE_ENTITY_ID: "climate.room"},
                {"timer_action": "boost", "end_time": 1100},
            )
        )
        await entered.wait()
        cancel = bridge._override_timers["uid"]  # noqa: SLF001
        stop = asyncio.create_task(methods["async_stop"](bridge))
        await asyncio.sleep(0)
        assert not stop.done()
        release.set()
        await asyncio.gather(command, stop)
        cancel.assert_called_once_with()
        assert not bridge._override_timers  # noqa: SLF001
        assert bridge._overrides["uid"].end_time == 1100  # noqa: SLF001
        # A callback already queued before cancellation cannot restore afterward.
        await callbacks[0](None)
        bridge.hass.services.async_call.assert_not_awaited()

    asyncio.run(scenario())


def test_resume_cannot_apply_overrides_after_unload() -> None:
    async def scenario() -> None:
        bridge, callbacks, methods = scheduled_bridge()
        bridge._overrides["uid"] = ThermostatOverride(  # noqa: SLF001
            "boost", 1100, "climate.room", "heat", 20
        )
        await methods["async_stop"](bridge)
        await methods["_async_resume_overrides"](bridge)
        assert not callbacks
        bridge._async_apply_override.assert_not_awaited()  # noqa: SLF001

    asyncio.run(scenario())


@pytest.mark.parametrize("method", ["_async_sync_bridge", "_async_schedule_tick"])
def test_old_runtime_cannot_prune_after_unload(method: str) -> None:
    async def scenario() -> None:
        bridge, _, methods = scheduled_bridge()
        bridge._mappings = {}  # noqa: SLF001
        bridge._async_publish_reconciliation = AsyncMock()  # noqa: SLF001
        await methods["async_stop"](bridge)
        args = (None,) if method == "_async_schedule_tick" else ()
        await methods[method](bridge, *args)
        bridge._async_publish_reconciliation.assert_not_awaited()  # noqa: SLF001

    asyncio.run(scenario())


@pytest.mark.parametrize("method", ["_async_sync_bridge", "_async_schedule_tick"])
def test_unload_drains_management_before_replacement_runtime(method: str) -> None:
    async def scenario() -> None:
        bridge, _, methods = scheduled_bridge()
        bridge._mappings = {}  # noqa: SLF001
        entered = asyncio.Event()
        release = asyncio.Event()

        async def publish() -> None:
            entered.set()
            await release.wait()

        bridge._async_publish_reconciliation = AsyncMock(side_effect=publish)  # noqa: SLF001
        args = (None,) if method == "_async_schedule_tick" else ()
        active = asyncio.create_task(methods[method](bridge, *args))
        await entered.wait()
        queued = asyncio.create_task(methods[method](bridge, *args))
        await asyncio.sleep(0)
        stop = asyncio.create_task(methods["async_stop"](bridge))
        await asyncio.sleep(0)
        assert not stop.done()
        release.set()
        await asyncio.gather(active, queued, stop)
        bridge._async_publish_reconciliation.assert_awaited_once()  # noqa: SLF001

    asyncio.run(scenario())
