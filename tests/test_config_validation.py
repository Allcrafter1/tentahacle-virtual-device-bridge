# SPDX-License-Identifier: MIT OR Apache-2.0
"""Validate configuration boundaries through the actual flow methods."""

import ast
import asyncio
from pathlib import Path
from types import MethodType, SimpleNamespace
from unittest.mock import AsyncMock, Mock

import pytest

from custom_components.tentahacle import models
from custom_components.tentahacle.const import (
    CONF_BRIDGE_ID,
    CONF_FRITZ_URL,
    CONF_SOURCE_ENTITY_ID,
    CONF_TOPIC_ROOT,
    MAX_VIRTUAL_DEVICES,
)

SOURCE = Path("custom_components/tentahacle/config_flow.py")


def flow_method(step: str, source: Path = SOURCE):
    tree = ast.parse(source.read_text())
    flow = next(node for node in tree.body if isinstance(node, ast.ClassDef))
    method = next(
        node for node in flow.body if getattr(node, "name", "") == f"async_step_{step}"
    )
    method.decorator_list = []
    namespace = {
        **vars(models),
        "CONF_BRIDGE_ID": CONF_BRIDGE_ID,
        "CONF_FRITZ_URL": CONF_FRITZ_URL,
        "CONF_TOPIC_ROOT": CONF_TOPIC_ROOT,
        "DEFAULT_TOPIC_ROOT": "tentahacle",
        "DEFAULT_FRITZ_URL": "http://fritz.box",
        "probatio": SimpleNamespace(Schema=lambda x: x, Required=lambda key, **_: key),
        "TextSelector": lambda *_: None,
        "TextSelectorConfig": lambda **kw: kw,
        "_async_notify_bridge_ready": AsyncMock(),
    }
    module = ast.Module(body=[method], type_ignores=[])
    # Strip annotations via future import to isolate Home Assistant imports.
    module.body.insert(
        0,
        ast.ImportFrom(
            module="__future__", names=[ast.alias(name="annotations")], level=0
        ),
    )
    exec(compile(ast.fix_missing_locations(module), str(source), "exec"), namespace)  # noqa: S102
    return namespace[f"async_step_{step}"]


def run_flow(step: str, root: str, source: Path = SOURCE):
    entry = SimpleNamespace(unique_id="lab", data={CONF_BRIDGE_ID: "lab"})
    flow = SimpleNamespace(
        hass=object(),
        async_set_unique_id=AsyncMock(),
        _abort_if_unique_id_configured=Mock(),
        _abort_if_unique_id_mismatch=Mock(),
        _get_reconfigure_entry=lambda: entry,
        async_create_entry=Mock(return_value={"created": True}),
        async_update_and_abort=Mock(return_value={"created": True}),
        async_show_form=lambda **kw: kw,
    )
    return asyncio.run(
        flow_method(step, source)(
            flow,
            {
                CONF_BRIDGE_ID: "lab",
                CONF_FRITZ_URL: "http://fritz.box",
                CONF_TOPIC_ROOT: root,
            },
        )
    )


@pytest.mark.parametrize("step", ["user", "reconfigure"])
@pytest.mark.parametrize(
    "root", ["", "/", "test/+", "test/#", "test//room", "bad root", "x" * 253]
)
def test_flow_rejects_unusable_topic_root(step: str, root: str) -> None:
    assert run_flow(step, root)["errors"][CONF_TOPIC_ROOT] == "invalid_topic_root"


@pytest.mark.parametrize("step", ["user", "reconfigure"])
def test_flow_keeps_valid_normalized_topic_root(step: str) -> None:
    assert run_flow(step, "/tentahacle/room/") == {"created": True}


@pytest.mark.parametrize(
    "name", ["a\nb", "a\tb", "a\x00b", "a\x7fb", "a\x85b", "a\ud800b"]
)
def test_name_validation_matches_provider(name: str) -> None:
    with pytest.raises(ValueError):
        models.VirtualDevice(
            "TVB0123456789ABCDEF", models.DeviceProfile.SWITCH, name, "switch.room"
        )


def test_utf8_byte_boundaries_and_valid_names() -> None:
    assert models.validate_device_name("Küche 💡") == "Küche 💡"
    assert models.validate_device_name("é" * 39 + "x")
    with pytest.raises(ValueError):
        models.validate_device_name("é" * 40)
    assert models.validate_topic_root("x" * 251, "lab")
    with pytest.raises(ValueError):
        models.validate_topic_root("x" * 252, "lab")


def _subentry_user_methods() -> dict[str, object]:
    tree = ast.parse(SOURCE.read_text())
    flow = next(
        node
        for node in tree.body
        if isinstance(node, ast.ClassDef) and node.name == "VirtualDeviceSubentryFlow"
    )
    methods = [
        node
        for node in flow.body
        if isinstance(node, (ast.AsyncFunctionDef, ast.FunctionDef))
        and node.name
        in {
            "async_step_user",
            "_device_count",
            "_thermostat_source_supported",
            "_cover_source_supported",
        }
    ]
    for method in methods:
        method.decorator_list = []
    namespace = {
        "MAX_VIRTUAL_DEVICES": MAX_VIRTUAL_DEVICES,
        "SUBENTRY_TYPE_DEVICE": "device",
        "CONF_SOURCE_ENTITY_ID": CONF_SOURCE_ENTITY_ID,
        "climate_entity_supported": models.climate_entity_supported,
        "cover_entity_supported": models.cover_entity_supported,
        "probatio": SimpleNamespace(
            Schema=lambda value: value, Required=lambda key: key
        ),
        "EntitySelector": lambda value: value,
        "EntitySelectorConfig": lambda **values: values,
    }
    module = ast.Module(
        body=[
            ast.ImportFrom(
                module="__future__", names=[ast.alias(name="annotations")], level=0
            ),
            *methods,
        ],
        type_ignores=[],
    )
    exec(compile(ast.fix_missing_locations(module), str(SOURCE), "exec"), namespace)  # noqa: S102
    return namespace


@pytest.mark.parametrize(
    ("count", "limited"),
    [(MAX_VIRTUAL_DEVICES - 1, False), (MAX_VIRTUAL_DEVICES, True)],
)
def test_virtual_device_wizard_enforces_native_capacity(
    count: int, *, limited: bool
) -> None:
    methods = _subentry_user_methods()
    entry = SimpleNamespace(
        subentries={
            str(index): SimpleNamespace(subentry_type="device")
            for index in range(count)
        }
    )
    flow = SimpleNamespace(
        _get_entry=lambda: entry,
        async_abort=lambda **values: {"type": "abort", **values},
        async_show_form=lambda **values: {"type": "form", **values},
    )
    flow._device_count = MethodType(methods["_device_count"], flow)  # noqa: SLF001
    result = asyncio.run(methods["async_step_user"](flow, None))
    assert result["type"] == ("abort" if limited else "form")
    if limited:
        assert result["reason"] == "device_limit_reached"


def test_virtual_device_wizard_rejects_incompatible_climate() -> None:
    methods = _subentry_user_methods()
    source_state = SimpleNamespace(
        attributes={
            "supported_features": 0,
            "hvac_modes": ["off", "heat"],
            "min_temp": 5,
            "max_temp": 35,
        }
    )
    flow = SimpleNamespace(
        _get_entry=lambda: SimpleNamespace(subentries={}),
        _source_in_use=lambda _: False,
        hass=SimpleNamespace(
            states=SimpleNamespace(get=lambda _: source_state),
            config=SimpleNamespace(units=SimpleNamespace(temperature_unit="°C")),
        ),
        async_abort=lambda **values: {"type": "abort", **values},
        async_show_form=lambda **values: {"type": "form", **values},
        async_step_details=AsyncMock(),
    )
    flow._device_count = MethodType(methods["_device_count"], flow)  # noqa: SLF001
    flow._thermostat_source_supported = MethodType(  # noqa: SLF001
        methods["_thermostat_source_supported"], flow
    )
    flow._cover_source_supported = MethodType(  # noqa: SLF001
        methods["_cover_source_supported"], flow
    )
    result = asyncio.run(
        methods["async_step_user"](
            flow, {CONF_SOURCE_ENTITY_ID: "climate.unsupported"}
        )
    )
    assert result["errors"][CONF_SOURCE_ENTITY_ID] == "incompatible_thermostat"
    flow.async_step_details.assert_not_awaited()


def test_virtual_device_wizard_rejects_read_only_cover() -> None:
    methods = _subentry_user_methods()
    source_state = SimpleNamespace(attributes={"supported_features": 0})
    flow = SimpleNamespace(
        _get_entry=lambda: SimpleNamespace(subentries={}),
        _source_in_use=lambda _: False,
        hass=SimpleNamespace(
            states=SimpleNamespace(get=lambda _: source_state),
            config=SimpleNamespace(units=SimpleNamespace(temperature_unit="°C")),
        ),
        async_abort=lambda **values: {"type": "abort", **values},
        async_show_form=lambda **values: {"type": "form", **values},
        async_step_details=AsyncMock(),
    )
    flow._device_count = MethodType(methods["_device_count"], flow)  # noqa: SLF001
    flow._thermostat_source_supported = MethodType(  # noqa: SLF001
        methods["_thermostat_source_supported"], flow
    )
    flow._cover_source_supported = MethodType(  # noqa: SLF001
        methods["_cover_source_supported"], flow
    )
    result = asyncio.run(
        methods["async_step_user"](
            flow, {CONF_SOURCE_ENTITY_ID: "cover.read_only"}
        )
    )
    assert result["errors"][CONF_SOURCE_ENTITY_ID] == "incompatible_cover"
    flow.async_step_details.assert_not_awaited()
