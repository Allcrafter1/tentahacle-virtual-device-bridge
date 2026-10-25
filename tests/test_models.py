# SPDX-License-Identifier: MIT OR Apache-2.0
"""Tests for transport-independent device configuration."""

import pytest

from custom_components.tentahacle.const import DeviceProfile
from custom_components.tentahacle.models import (
    VirtualDevice,
    bridge_details_from_discovery,
    endpoint_uid_from_token,
    profile_for_domain,
    validate_bridge_id,
    validate_fritz_url,
)


def test_endpoint_uid_is_stable_and_native_length() -> None:
    uid = endpoint_uid_from_token("fixed-random-token")
    assert uid == endpoint_uid_from_token("fixed-random-token")
    assert uid.startswith("TVB")
    assert len(uid) == 19


def test_bridge_discovery_round_trip() -> None:
    assert bridge_details_from_discovery(
        "tentahacle/lab-7530/bridge/info",
        b'{"schema_version":1,"bridge_id":"lab-7530"}',
    ) == ("lab-7530", "tentahacle")


@pytest.mark.parametrize(
    ("topic", "payload"),
    [
        (
            "tentahacle/lab-7530/info",
            '{"schema_version":1,"bridge_id":"lab-7530"}',
        ),
        ("tentahacle/lab-7530/bridge/info", "not json"),
        (
            "tentahacle/lab-7530/bridge/info",
            '{"schema_version":2,"bridge_id":"lab-7530"}',
        ),
        (
            "tentahacle/lab-7530/bridge/info",
            '{"schema_version":1,"bridge_id":"other"}',
        ),
    ],
)
def test_bridge_discovery_rejects_invalid(topic: str, payload: str) -> None:
    with pytest.raises(ValueError):
        bridge_details_from_discovery(topic, payload)


@pytest.mark.parametrize(
    ("domain", "color_temperature", "expected"),
    [
        ("switch", False, DeviceProfile.SWITCH),
        ("input_boolean", False, DeviceProfile.SWITCH),
        ("light", False, DeviceProfile.DIMMABLE_LIGHT),
        ("light", True, DeviceProfile.COLOR_TEMPERATURE_LIGHT),
        ("cover", False, DeviceProfile.COVER),
        ("climate", False, DeviceProfile.THERMOSTAT),
    ],
)
def test_profile_for_domain(
    domain: str, color_temperature: bool, expected: DeviceProfile
) -> None:
    assert profile_for_domain(domain, color_temperature=color_temperature) is expected


def test_management_payload_has_no_home_assistant_entity_id_on_wire() -> None:
    device = VirtualDevice(
        endpoint_uid="TVB0123456789ABCDEF",
        profile=DeviceProfile.SWITCH,
        name="Coffee machine",
        source_entity_id="switch.coffee_machine",
    )
    payload = device.management_payload("request-1")
    assert payload["schema_version"] == 1
    assert payload["device"]["uid"] == "TVB0123456789ABCDEF"  # type: ignore[index]
    assert "source_entity_id" not in payload["device"]  # type: ignore[operator]


def test_validation_rejects_topic_injection_and_long_names() -> None:
    with pytest.raises(ValueError):
        validate_bridge_id("bridge/other")
    with pytest.raises(ValueError):
        VirtualDevice(
            endpoint_uid="TVB0123456789ABCDEF",
            profile=DeviceProfile.SWITCH,
            name="x" * 80,
            source_entity_id="switch.test",
        )


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        ("http://fritz.box/", "http://fritz.box"),
        (" https://192.168.178.1/ui/ ", "https://192.168.178.1/ui"),
    ],
)
def test_fritz_url_validation(value: str, expected: str) -> None:
    assert validate_fritz_url(value) == expected


@pytest.mark.parametrize(
    "value",
    [
        "fritz.box",
        "ftp://fritz.box",
        "http://user:secret@fritz.box",
        "http://fritz.box/?sid=secret",
        "http://fritz.box/#smart-home",
    ],
)
def test_fritz_url_rejects_unsafe_or_ambiguous_values(value: str) -> None:
    with pytest.raises(ValueError):
        validate_fritz_url(value)


def test_discovery_rejects_excessive_nesting_as_validation_error() -> None:
    depth = 10000
    with pytest.raises(ValueError):
        bridge_details_from_discovery(
            "tentahacle/lab-7530/bridge/info", "[" * depth + "]" * depth
        )
