# SPDX-License-Identifier: MIT OR Apache-2.0
"""Exercise actual bridge timer methods without requiring a Home Assistant install."""

import ast
import asyncio
import json
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
from custom_components.tentahacle.protocol import (
    ClimateCapabilities,
    CoverCapabilities,
    LightCapabilities,
    ProtocolError,
    feedback_for_state,
    parse_command,
    service_call_for_command,
)
from custom_components.tentahacle.thermostat import (
    ThermostatOverride,
    override_superseded_by_state,
    timer_request,
)


def bridge_methods() -> dict:
    """Load unchanged method ASTs, isolating Home Assistant import dependencies."""
    source = Path("custom_components/tentahacle/bridge.py")
    tree = ast.parse(source.read_text())
    bridge = next(node for node in tree.body if isinstance(node, ast.ClassDef))
    names = {
        "async_stop",
        "async_remove",
        "_state_listener",
        "_async_sync_bridge",
        "_async_schedule_tick",
        "_async_publish_schedule",
        "_async_publish_authoritative_timer",
        "_async_publish_prune",
        "_async_publish_feedback",
        "_async_command_message",
        "_async_load_overrides",
        "_async_cancel_override_if_source_changed",
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
        "json": json,
        "uuid": SimpleNamespace(uuid4=lambda: "request-id"),
        "PROTOCOL_VERSION": 1,
        "_LOGGER": Mock(),
        "ThermostatOverride": ThermostatOverride,
        "timer_request": timer_request,
        "override_superseded_by_state": override_superseded_by_state,
        "ProtocolError": ProtocolError,
        "ClimateCapabilities": ClimateCapabilities,
        "CoverCapabilities": CoverCapabilities,
        "LightCapabilities": LightCapabilities,
        "CoverEntityFeature": SimpleNamespace(
            OPEN=1,
            CLOSE=2,
            SET_POSITION=4,
            STOP=8,
        ),
        "parse_command": parse_command,
        "service_call_for_command": service_call_for_command,
        "feedback_for_state": feedback_for_state,
        "_diagnostic_values": lambda values: values,
        "_light_capabilities": lambda attributes: LightCapabilities(
            frozenset(attributes.get("supported_color_modes", [])),
            attributes.get("min_color_temp_kelvin"),
            attributes.get("max_color_temp_kelvin"),
        ),
        "monotonic": Mock(side_effect=[1.0, 1.125]),
        "CONF_SOURCE_ENTITY_ID": CONF_SOURCE_ENTITY_ID,
        "CONF_PROFILE": CONF_PROFILE,
        "DeviceProfile": DeviceProfile,
        "CONF_SCHEDULE_WEEK_PROFILE": "schedule_week_profile",
        "CONF_SCHEDULE_WORKDAYS": "schedule_workdays",
        "CONF_SCHEDULE_HOLIDAYS": "schedule_holidays",
        "mqtt": SimpleNamespace(async_publish=AsyncMock()),
        "callback": lambda value: value,
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


def test_hub_removal_restores_all_overrides_then_prunes_native_devices() -> None:
    methods = bridge_methods()
    overrides = {
        "first": ThermostatOverride("boost", 1100, "climate.one", "heat", 20),
        "second": ThermostatOverride("cold", 1100, "climate.two", "heat", 21),
    }
    bridge = SimpleNamespace(
        _stopping=False,
        _overrides={},
        _override_lock=asyncio.Lock(),
        _override_store=SimpleNamespace(async_remove=AsyncMock()),
        _async_load_overrides=AsyncMock(),
        _async_finish_override=AsyncMock(),
        _async_publish_prune=AsyncMock(),
    )

    async def load() -> None:
        bridge._overrides.update(overrides)  # noqa: SLF001

    async def finish(uid: str) -> None:
        bridge._overrides.pop(uid)  # noqa: SLF001

    bridge._async_load_overrides.side_effect = load  # noqa: SLF001
    bridge._async_finish_override.side_effect = finish  # noqa: SLF001
    asyncio.run(methods["async_remove"](bridge))
    assert bridge._stopping is True  # noqa: SLF001
    assert [call.args[0] for call in bridge._async_finish_override.await_args_list] == [  # noqa: SLF001
        "first",
        "second",
    ]
    bridge._async_publish_prune.assert_awaited_once_with([])  # noqa: SLF001
    bridge._override_store.async_remove.assert_awaited_once()  # noqa: SLF001


def test_hub_removal_prunes_after_restore_failure_but_keeps_recovery_store() -> None:
    methods = bridge_methods()
    bridge = SimpleNamespace(
        _stopping=False,
        _overrides={
            "failed": ThermostatOverride(
                "boost", 1100, "climate.missing", "heat", 20
            ),
            "ok": ThermostatOverride("cold", 1100, "climate.room", "heat", 21),
        },
        _override_lock=asyncio.Lock(),
        _override_store=SimpleNamespace(async_remove=AsyncMock()),
        _async_load_overrides=AsyncMock(),
        _async_finish_override=AsyncMock(),
        _async_publish_prune=AsyncMock(),
    )

    async def finish(uid: str) -> None:
        if uid == "failed":
            raise ConnectionError("source unavailable")
        bridge._overrides.pop(uid)  # noqa: SLF001

    bridge._async_finish_override.side_effect = finish  # noqa: SLF001
    with pytest.raises(RuntimeError, match="failed"):
        asyncio.run(methods["async_remove"](bridge))
    bridge._async_publish_prune.assert_awaited_once_with([])  # noqa: SLF001
    bridge._override_store.async_remove.assert_not_awaited()  # noqa: SLF001


def test_empty_prune_payload_removes_every_native_device() -> None:
    methods = bridge_methods()
    bridge = SimpleNamespace(hass=object(), topic_prefix="tentahacle/lab")
    asyncio.run(methods["_async_publish_prune"](bridge, []))
    call = methods["mqtt"].async_publish.await_args
    assert call.args[1] == "tentahacle/lab/management/request"
    assert json.loads(call.args[2]) == {
        "schema_version": 1,
        "request_id": "request-id",
        "operation": "prune_devices",
        "keep_uids": [],
    }
    assert call.kwargs == {"qos": 1, "retain": False}


def test_missing_thermostat_state_clears_retained_actual_temperature() -> None:
    methods = bridge_methods()
    mapping = {
        CONF_PROFILE: DeviceProfile.THERMOSTAT.value,
        CONF_SOURCE_ENTITY_ID: "climate.room",
    }
    bridge = SimpleNamespace(
        _stopping=False,
        _sync_lock=asyncio.Lock(),
        _mappings={"uid": mapping},
        hass=SimpleNamespace(states=SimpleNamespace(get=lambda _: None)),
        _async_publish_management=AsyncMock(),
        _async_publish_feedback=AsyncMock(),
        _async_publish_schedule=AsyncMock(),
        _async_publish_authoritative_timer=AsyncMock(),
        _async_publish_reconciliation=AsyncMock(),
    )
    asyncio.run(methods["_async_sync_bridge"](bridge))
    bridge._async_publish_feedback.assert_awaited_once_with(  # noqa: SLF001
        "uid", mapping, "unavailable", {}
    )


def test_thermostat_command_uses_home_assistant_unit_when_entity_omits_it() -> None:
    methods = bridge_methods()
    mapping = {
        CONF_PROFILE: DeviceProfile.THERMOSTAT.value,
        CONF_SOURCE_ENTITY_ID: "climate.room",
    }
    bridge = SimpleNamespace(
        _mappings={"uid": mapping},
        hass=SimpleNamespace(
            config=SimpleNamespace(units=SimpleNamespace(temperature_unit="°F")),
            states=SimpleNamespace(
                get=lambda _: SimpleNamespace(
                    state="heat", attributes={"max_temp": 95}
                )
            ),
            services=SimpleNamespace(async_call=AsyncMock()),
        ),
        _mapping_color_full=Mock(return_value=False),
        _async_handle_timer=AsyncMock(),
    )
    message = SimpleNamespace(
        topic="tentahacle/lab/device/uid/command",
        payload=(
            '{"event":"command","endpoint":"uid","sequence":1,'
            '"hvac_mode":"heat","target_temperature":20}'
        ),
    )
    asyncio.run(methods["_async_command_message"](bridge, message))
    bridge.hass.services.async_call.assert_awaited_once_with(
        "climate",
        "set_temperature",
        {"entity_id": "climate.room", "temperature": 68.0, "hvac_mode": "heat"},
        blocking=True,
    )


def test_light_command_uses_source_kelvin_range_and_logs_sequence() -> None:
    methods = bridge_methods()
    mapping = {
        CONF_PROFILE: DeviceProfile.COLOR_TEMPERATURE_LIGHT.value,
        CONF_SOURCE_ENTITY_ID: "light.room",
    }
    bridge = SimpleNamespace(
        _mappings={"uid": mapping},
        hass=SimpleNamespace(
            states=SimpleNamespace(
                get=lambda _: SimpleNamespace(
                    state="on",
                    attributes={
                        "supported_color_modes": ["color_temp"],
                        "min_color_temp_kelvin": 2000,
                        "max_color_temp_kelvin": 5000,
                    },
                )
            ),
            services=SimpleNamespace(async_call=AsyncMock()),
        ),
        _mapping_color_full=Mock(return_value=False),
        _async_handle_timer=AsyncMock(),
    )
    message = SimpleNamespace(
        topic="tentahacle/lab/device/uid/command",
        payload=(
            '{"event":"command","endpoint":"uid","sequence":17,'
            '"state":1,"color_temperature":6500}'
        ),
    )
    asyncio.run(methods["_async_command_message"](bridge, message))
    bridge.hass.services.async_call.assert_awaited_once_with(
        "light",
        "turn_on",
        {"entity_id": "light.room", "color_temp_kelvin": 5000},
        blocking=True,
    )
    debug_calls = methods["_LOGGER"].debug.call_args_list
    assert debug_calls[0].args[1:4] == ("uid", 17, "light.room")
    assert debug_calls[1].args[1:3] == ("uid", 17)
    assert debug_calls[1].args[3] == 125.0


def test_command_service_failure_logs_sequence_and_elapsed_then_raises() -> None:
    methods = bridge_methods()
    service_call = AsyncMock(side_effect=RuntimeError("source failed"))
    mapping = {
        CONF_PROFILE: DeviceProfile.SWITCH.value,
        CONF_SOURCE_ENTITY_ID: "switch.room",
    }
    bridge = SimpleNamespace(
        _mappings={"uid": mapping},
        hass=SimpleNamespace(
            states=SimpleNamespace(get=lambda _: None),
            services=SimpleNamespace(async_call=service_call),
        ),
        _mapping_color_full=Mock(return_value=False),
        _async_handle_timer=AsyncMock(),
    )
    message = SimpleNamespace(
        topic="tentahacle/lab/device/uid/command",
        payload=(
            '{"event":"command","endpoint":"uid","sequence":23,'
            '"state":1}'
        ),
    )

    with pytest.raises(RuntimeError, match="source failed"):
        asyncio.run(methods["_async_command_message"](bridge, message))

    service_call.assert_awaited_once_with(
        "switch", "turn_on", {"entity_id": "switch.room"}, blocking=True
    )
    exception_call = methods["_LOGGER"].exception.call_args
    assert exception_call.args[1:3] == ("uid", 23)
    assert exception_call.args[3] == 125.0


def test_light_feedback_logs_and_publishes_native_palette_value() -> None:
    methods = bridge_methods()
    methods["mqtt"].async_publish.reset_mock()
    mapping = {
        CONF_PROFILE: DeviceProfile.COLOR_TEMPERATURE_LIGHT.value,
        CONF_SOURCE_ENTITY_ID: "light.room",
    }
    bridge = SimpleNamespace(
        hass=object(),
        topic_prefix="tentahacle/lab",
        _mapping_color_full=Mock(return_value=False),
    )
    asyncio.run(
        methods["_async_publish_feedback"](
            bridge,
            "uid",
            mapping,
            "on",
            {
                "supported_color_modes": ["color_temp"],
                "min_color_temp_kelvin": 2000,
                "max_color_temp_kelvin": 6535,
                "color_mode": "color_temp",
                "color_temp_kelvin": 3003,
            },
        )
    )
    published = {
        call.args[1].rsplit("/", 2)[-2]: call.args[2]
        for call in methods["mqtt"].async_publish.await_args_list
    }
    assert published == {
        "state": "ON",
        "hs": "",
        "color_temperature": "3000",
    }
    assert "properties=%s" in methods["_LOGGER"].debug.call_args.args[0]


def test_sync_prunes_before_upsert_and_reasserts_authority() -> None:
    methods = bridge_methods()
    calls: list[str] = []
    mapping = {
        CONF_PROFILE: DeviceProfile.SWITCH.value,
        CONF_SOURCE_ENTITY_ID: "switch.room",
    }
    bridge = SimpleNamespace(
        _stopping=False,
        _sync_lock=asyncio.Lock(),
        _mappings={"uid": mapping},
        hass=SimpleNamespace(states=SimpleNamespace(get=lambda _: None)),
        _async_publish_management=AsyncMock(
            side_effect=lambda *_: calls.append("upsert")
        ),
        _async_publish_feedback=AsyncMock(),
        _async_publish_schedule=AsyncMock(
            side_effect=lambda *_: calls.append("schedule")
        ),
        _async_publish_authoritative_timer=AsyncMock(
            side_effect=lambda *_: calls.append("timer")
        ),
        _async_publish_reconciliation=AsyncMock(
            side_effect=lambda: calls.append("prune")
        ),
    )
    asyncio.run(methods["_async_sync_bridge"](bridge))
    assert calls == ["prune", "upsert", "schedule", "timer", "prune"]


def test_periodic_tick_retries_device_upserts_and_timer_feedback() -> None:
    methods = bridge_methods()
    calls: list[str] = []
    mapping = {
        CONF_PROFILE: DeviceProfile.THERMOSTAT.value,
        CONF_SOURCE_ENTITY_ID: "climate.room",
        "schedule_workdays": "schedule.workdays",
    }
    bridge = SimpleNamespace(
        _stopping=False,
        _sync_lock=asyncio.Lock(),
        _mappings={"uid": mapping},
        hass=SimpleNamespace(
            states=SimpleNamespace(
                get=lambda _: SimpleNamespace(state="heat", attributes={})
            )
        ),
        _async_publish_management=AsyncMock(
            side_effect=lambda *_: calls.append("upsert")
        ),
        _async_publish_feedback=AsyncMock(
            side_effect=lambda *_: calls.append("feedback")
        ),
        _async_publish_schedule=AsyncMock(
            side_effect=lambda *_: calls.append("schedule")
        ),
        _async_publish_authoritative_timer=AsyncMock(
            side_effect=lambda *_: calls.append("timer")
        ),
        _async_publish_reconciliation=AsyncMock(
            side_effect=lambda: calls.append("prune")
        ),
    )
    bridge._async_sync_bridge = MethodType(  # noqa: SLF001
        methods["_async_sync_bridge"], bridge
    )
    asyncio.run(methods["_async_schedule_tick"](bridge, None))
    assert calls == [
        "prune",
        "upsert",
        "feedback",
        "schedule",
        "timer",
        "prune",
    ]


@pytest.mark.parametrize("active", [False, True])
def test_authoritative_timer_replays_active_or_cancelled_state(*, active: bool) -> None:
    methods = bridge_methods()
    override = ThermostatOverride("boost", 1100, "climate.room", "heat", 20)
    bridge = SimpleNamespace(
        _override_lock=asyncio.Lock(),
        _overrides={"uid": override} if active else {},
        _async_publish_timer=AsyncMock(),
    )
    mapping = {
        CONF_PROFILE: DeviceProfile.THERMOSTAT.value,
        CONF_SOURCE_ENTITY_ID: "climate.room",
    }
    asyncio.run(
        methods["_async_publish_authoritative_timer"](bridge, "uid", mapping)
    )
    bridge._async_publish_timer.assert_awaited_once_with(  # noqa: SLF001
        "uid", "boost" if active else "cancel", 1100 if active else 0
    )


def test_removed_thermostat_state_clears_actual_and_schedule() -> None:
    methods = bridge_methods()
    mapping = {
        CONF_PROFILE: DeviceProfile.THERMOSTAT.value,
        CONF_SOURCE_ENTITY_ID: "climate.room",
    }
    bridge = SimpleNamespace(
        _async_publish_feedback=AsyncMock(),
        _async_cancel_override_if_source_changed=AsyncMock(),
        _async_publish_schedule=AsyncMock(),
    )
    listener = methods["_state_listener"](bridge, "uid", mapping)
    asyncio.run(listener(SimpleNamespace(data={"new_state": None})))
    bridge._async_publish_feedback.assert_awaited_once_with(  # noqa: SLF001
        "uid", mapping, "unavailable", {}
    )
    bridge._async_publish_schedule.assert_awaited_once_with(  # noqa: SLF001
        "uid", mapping
    )


def test_external_source_change_cancels_without_restoring_old_state() -> None:
    methods = bridge_methods()
    cancel = Mock()
    override = ThermostatOverride("boost", 1100, "climate.room", "heat", 20)
    bridge = SimpleNamespace(
        _override_lock=asyncio.Lock(),
        _overrides={"uid": override},
        _override_timers={"uid": cancel},
        hass=SimpleNamespace(
            states=SimpleNamespace(
                get=lambda _: SimpleNamespace(
                    state="heat", attributes={"temperature": 21, "max_temp": 35}
                )
            )
        ),
        _async_save_overrides=AsyncMock(),
        _async_publish_timer=AsyncMock(),
        _schedule_override=Mock(),
    )
    asyncio.run(methods["_async_cancel_override_if_source_changed"](bridge, "uid"))
    cancel.assert_called_once_with()
    assert not bridge._overrides  # noqa: SLF001
    bridge._async_save_overrides.assert_awaited_once()  # noqa: SLF001
    bridge._async_publish_timer.assert_awaited_once_with(  # noqa: SLF001
        "uid", "cancel", 0
    )
    bridge._schedule_override.assert_not_called()  # noqa: SLF001


def test_expected_boost_echo_does_not_cancel_override() -> None:
    methods = bridge_methods()
    override = ThermostatOverride("boost", 1100, "climate.room", "heat", 20)
    bridge = SimpleNamespace(
        _override_lock=asyncio.Lock(),
        _overrides={"uid": override},
        _override_timers={},
        hass=SimpleNamespace(
            states=SimpleNamespace(
                get=lambda _: SimpleNamespace(
                    state="heat", attributes={"temperature": 35, "max_temp": 35}
                )
            )
        ),
        _async_save_overrides=AsyncMock(),
        _async_publish_timer=AsyncMock(),
    )
    asyncio.run(methods["_async_cancel_override_if_source_changed"](bridge, "uid"))
    assert bridge._overrides == {"uid": override}  # noqa: SLF001
    bridge._async_save_overrides.assert_not_awaited()  # noqa: SLF001


def test_missing_source_state_does_not_abandon_saved_override() -> None:
    methods = bridge_methods()
    override = ThermostatOverride("cold", 1100, "climate.room", "heat", 20)
    bridge = SimpleNamespace(
        _override_lock=asyncio.Lock(),
        _overrides={"uid": override},
        _override_timers={},
        hass=SimpleNamespace(states=SimpleNamespace(get=lambda _: None)),
        _async_save_overrides=AsyncMock(),
        _async_publish_timer=AsyncMock(),
    )
    asyncio.run(methods["_async_cancel_override_if_source_changed"](bridge, "uid"))
    assert bridge._overrides == {"uid": override}  # noqa: SLF001
    bridge._async_save_overrides.assert_not_awaited()  # noqa: SLF001
    bridge._async_publish_timer.assert_not_awaited()  # noqa: SLF001


def timer_bridge() -> SimpleNamespace:
    """Provide the dependencies used by the actual timer lifecycle methods."""
    return SimpleNamespace(
        _stopping=False,
        _unsubscribers=[],
        _override_lock=asyncio.Lock(),
        _sync_lock=asyncio.Lock(),
        _overrides={},
        _override_timers={},
        _mappings={
            "uid": {
                CONF_PROFILE: DeviceProfile.THERMOSTAT.value,
                CONF_SOURCE_ENTITY_ID: "climate.room",
            }
        },
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
        uid: {
            CONF_PROFILE: DeviceProfile.THERMOSTAT.value,
            CONF_SOURCE_ENTITY_ID: f"climate.{uid}",
        }
        for uid in ("first", "second")
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


def test_load_keeps_removed_mapping_override_for_restoration() -> None:
    bridge = SimpleNamespace(
        _override_store=SimpleNamespace(
            async_load=AsyncMock(
                return_value={
                    "overrides": {
                        "removed": ThermostatOverride(
                            "cold", 1100, "climate.old", "heat", 20
                        ).as_json()
                    }
                }
            )
        ),
        _overrides={},
        _mappings={},
    )
    asyncio.run(bridge_methods()["_async_load_overrides"](bridge))
    assert bridge._overrides["removed"].entity_id == "climate.old"  # noqa: SLF001


@pytest.mark.parametrize("mapping", [None, {CONF_SOURCE_ENTITY_ID: "climate.new"}])
def test_resume_restores_removed_or_rebound_mapping_and_keeps_other_timer(
    mapping: dict[str, str] | None,
) -> None:
    bridge = timer_bridge()
    bridge._overrides = {  # noqa: SLF001
        "removed": ThermostatOverride("cold", 1100, "climate.old", "heat", 20),
        "uid": ThermostatOverride("cold", 1100, "climate.room", "heat", 20),
    }
    if mapping is not None:
        mapping[CONF_PROFILE] = DeviceProfile.THERMOSTAT.value
        bridge._mappings["removed"] = mapping  # noqa: SLF001
    bridge._mappings["uid"][CONF_PROFILE] = DeviceProfile.THERMOSTAT.value  # noqa: SLF001
    methods = bridge_methods()
    bridge._async_finish_override = MethodType(  # noqa: SLF001
        methods["_async_finish_override"], bridge
    )
    asyncio.run(methods["_async_resume_overrides"](bridge))
    assert set(bridge._overrides) == {"uid"}  # noqa: SLF001
    bridge._schedule_override.assert_called_once_with("uid", 1100)  # noqa: SLF001
    assert bridge.hass.services.async_call.await_args_list[0].args[2] == {
        "entity_id": "climate.old",
        "temperature": 20,
    }


def test_failed_removed_mapping_restore_does_not_skip_other_timer() -> None:
    bridge = timer_bridge()
    bridge._overrides = {  # noqa: SLF001
        "removed": ThermostatOverride("cold", 1100, "climate.old", "heat", 20),
        "uid": ThermostatOverride("boost", 1100, "climate.room", "heat", 20),
    }
    bridge._async_finish_override = AsyncMock(  # noqa: SLF001
        side_effect=ConnectionError("source unavailable")
    )
    asyncio.run(bridge_methods()["_async_resume_overrides"](bridge))
    bridge._async_finish_override.assert_awaited_once_with("removed")  # noqa: SLF001
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
    for name in ("_schedule_override", "_async_finish_override", "_async_sync_bridge"):
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
        assert bridge._async_publish_reconciliation.await_count == 2  # noqa: SLF001

    asyncio.run(scenario())
