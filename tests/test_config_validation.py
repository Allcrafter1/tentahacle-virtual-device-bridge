# SPDX-License-Identifier: MIT OR Apache-2.0
"""Validate configuration boundaries through the actual flow methods."""

import ast
import asyncio
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

import pytest

from custom_components.tentahacle import models
from custom_components.tentahacle.const import (
    CONF_BRIDGE_ID,
    CONF_FRITZ_URL,
    CONF_TOPIC_ROOT,
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
