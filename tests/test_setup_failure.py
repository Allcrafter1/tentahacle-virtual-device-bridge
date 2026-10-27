# SPDX-License-Identifier: MIT OR Apache-2.0
"""Check setup cleanup using the actual setup function and injected HA failures."""

import ast
import asyncio
from datetime import timedelta
from pathlib import Path
from types import MethodType, SimpleNamespace
from unittest.mock import AsyncMock, Mock

import pytest


def setup_method(bridge: object, translations: AsyncMock):
    source = Path("custom_components/tentahacle/__init__.py")
    tree = ast.parse(source.read_text())
    method = next(
        node
        for node in tree.body
        if isinstance(node, ast.AsyncFunctionDef) and node.name == "async_setup_entry"
    )
    module = ast.Module(
        body=[
            ast.ImportFrom(
                module="__future__", names=[ast.alias(name="annotations")], level=0
            ),
            method,
        ],
        type_ignores=[],
    )
    namespace = {
        "TentAHAcleBridge": lambda *_: bridge,
        "async_get_translations": translations,
        "CONF_BRIDGE_ID": "bridge_id",
        "CONF_FRITZ_URL": "fritz_url",
        "DEFAULT_FRITZ_URL": "http://fritz.box",
        "DOMAIN": "tentahacle",
        "dr": SimpleNamespace(async_get=lambda _: Mock()),
        "_async_reload_entry": Mock(),
    }
    exec(compile(ast.fix_missing_locations(module), str(source), "exec"), namespace)  # noqa: S102
    return namespace["async_setup_entry"]


@pytest.mark.parametrize("failure", ["start", "translations", "cancel", "none"])
def test_partial_setup_cleans_bridge_before_retry(failure: str) -> None:
    bridge = SimpleNamespace(async_start=AsyncMock(), async_stop=AsyncMock())
    translations = AsyncMock(return_value={})
    error = (
        asyncio.CancelledError() if failure == "cancel" else ConnectionError("offline")
    )
    if failure in {"start", "cancel"}:
        bridge.async_start.side_effect = error
    elif failure == "translations":
        translations.side_effect = error
    entry = SimpleNamespace(
        data={"bridge_id": "lab"},
        title="Custom bridge",
        entry_id="test",
        async_on_unload=Mock(),
        add_update_listener=Mock(),
    )
    hass = SimpleNamespace(config=SimpleNamespace(language="de"))
    method = setup_method(bridge, translations)
    if failure == "none":
        assert asyncio.run(method(hass, entry))
        bridge.async_stop.assert_not_awaited()
        assert entry.runtime_data is bridge
    else:
        with pytest.raises(type(error)):
            asyncio.run(method(hass, entry))
        bridge.async_stop.assert_awaited_once()
        assert not hasattr(entry, "runtime_data")


def test_failed_real_start_releases_created_subscriptions_and_timer() -> None:
    """Exercise real start/stop with failure after all subscriptions exist."""
    source = Path("custom_components/tentahacle/bridge.py")
    tree = ast.parse(source.read_text())
    bridge_class = next(node for node in tree.body if isinstance(node, ast.ClassDef))
    methods = [
        node
        for node in bridge_class.body
        if isinstance(node, ast.AsyncFunctionDef)
        and node.name in {"async_start", "async_stop"}
    ]
    unsubscribers = [Mock(), Mock(), Mock()]
    timer_cancel = Mock()
    namespace = {
        "MAX_VIRTUAL_DEVICES": 32,
        "ConfigEntryError": RuntimeError,
        "timedelta": timedelta,
        "mqtt": SimpleNamespace(
            async_wait_for_mqtt_client=AsyncMock(),
            async_subscribe=AsyncMock(side_effect=unsubscribers[:2]),
        ),
        "async_track_time_interval": Mock(return_value=unsubscribers[2]),
    }
    module = ast.Module(body=methods, type_ignores=[])
    exec(compile(ast.fix_missing_locations(module), str(source), "exec"), namespace)  # noqa: S102
    bridge = SimpleNamespace(
        hass=object(),
        topic_prefix="test/bridge",
        _mappings={},
        _stopping=False,
        _override_lock=asyncio.Lock(),
        _sync_lock=asyncio.Lock(),
        _unsubscribers=[],
        _override_timers={"uid": timer_cancel},
        _async_load_overrides=AsyncMock(),
        _async_command_message=Mock(),
        _async_availability_message=Mock(),
        _async_schedule_tick=Mock(),
        _async_resume_overrides=AsyncMock(),
        _async_sync_bridge=AsyncMock(side_effect=ConnectionError("offline")),
    )
    bridge.async_start = MethodType(namespace["async_start"], bridge)
    bridge.async_stop = MethodType(namespace["async_stop"], bridge)
    with pytest.raises(ConnectionError):
        asyncio.run(setup_method(bridge, AsyncMock())(object(), SimpleNamespace()))
    for unsubscribe in unsubscribers:
        unsubscribe.assert_called_once_with()
    timer_cancel.assert_called_once_with()
    assert not bridge._unsubscribers  # noqa: SLF001
    assert not bridge._override_timers  # noqa: SLF001
