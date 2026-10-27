# SPDX-License-Identifier: MIT OR Apache-2.0
"""Tests for transport-independent device configuration."""

import pytest

from custom_components.tentahacle.const import DeviceProfile
from custom_components.tentahacle.models import (
    VirtualDevice,
    bridge_details_from_discovery,
    climate_entity_supported,
    cover_entity_supported,
    endpoint_uid_from_token,
    fritz_smart_home_url,
    full_color_supported,
    profile_for_domain,
    profiles_for_entity,
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
    ("domain", "supported_color_modes", "expected"),
    [
        ("switch", [], DeviceProfile.SWITCH),
        ("input_boolean", [], DeviceProfile.SWITCH),
        ("light", ["onoff"], DeviceProfile.SWITCH),
        ("light", ["brightness"], DeviceProfile.DIMMABLE_LIGHT),
        ("light", ["color_temp"], DeviceProfile.COLOR_TEMPERATURE_LIGHT),
        ("light", ["rgb"], DeviceProfile.COLOR_TEMPERATURE_LIGHT),
        ("cover", [], DeviceProfile.COVER),
        ("climate", [], DeviceProfile.THERMOSTAT),
    ],
)
def test_profile_for_domain(
    domain: str, supported_color_modes: list[str], expected: DeviceProfile
) -> None:
    assert (
        profile_for_domain(domain, supported_color_modes=supported_color_modes)
        is expected
    )


@pytest.mark.parametrize(
    ("modes", "expected"),
    [(["color_temp"], False), (["color_temp", "hs"], True), (["rgb"], True)],
)
def test_full_color_capability_detection(modes: list[str], expected: bool) -> None:
    assert full_color_supported(modes) is expected


@pytest.mark.parametrize(
    ("modes", "expected"),
    [
        (["onoff"], ["switch"]),
        (["brightness"], ["switch", "dimmable_light"]),
        (
            ["color_temp"],
            ["switch", "dimmable_light", "color_temperature_light"],
        ),
        (["rgb"], ["switch", "dimmable_light", "color_temperature_light"]),
    ],
)
def test_light_profiles_follow_home_assistant_color_modes(
    modes: list[str], expected: list[str]
) -> None:
    assert profiles_for_entity("light", modes) == expected


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


def test_full_color_capability_is_part_of_color_device_management() -> None:
    device = VirtualDevice(
        endpoint_uid="TVB0123456789ABCDEF",
        profile=DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        name="Colour light",
        source_entity_id="light.colour",
        color_full=True,
    )
    assert device.management_payload("request-1")["device"]["color_full"] is True  # type: ignore[index]


def test_full_color_capability_is_rejected_for_other_profiles() -> None:
    with pytest.raises(ValueError, match="full color"):
        VirtualDevice(
            endpoint_uid="TVB0123456789ABCDEF",
            profile=DeviceProfile.SWITCH,
            name="Not a colour light",
            source_entity_id="switch.test",
            color_full=True,
        )


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
    ("value", "expected"),
    [
        ("http://fritz.box", "http://fritz.box/?lp=sh_dev"),
        (
            "https://192.168.178.38:8443/proxy/",
            "https://192.168.178.38:8443/proxy/?lp=sh_dev",
        ),
    ],
)
def test_fritz_smart_home_link(value: str, expected: str) -> None:
    assert fritz_smart_home_url(value) == expected


@pytest.mark.parametrize(
    "attributes",
    [
        {
            "supported_features": 1,
            "hvac_modes": ["off", "heat"],
            "min_temp": 5,
            "max_temp": 35,
            "target_temp_step": 0.5,
            "temperature_unit": "°C",
        },
        {
            "supported_features": 1,
            "hvac_modes": ["heat", "off", "auto"],
            "min_temp": 41,
            "max_temp": 95,
            "target_temp_step": 0.1,
            "temperature_unit": "°F",
        },
    ],
)
def test_compatible_climate_entities_cover_fritz_controls(
    attributes: dict[str, object],
) -> None:
    assert climate_entity_supported(attributes)


def test_climate_validation_uses_home_assistant_unit_fallback() -> None:
    assert climate_entity_supported(
        {
            "supported_features": 1,
            "hvac_modes": ["off", "heat"],
            "min_temp": 41,
            "max_temp": 95,
            "target_temp_step": 0.5,
        },
        fallback_temperature_unit="°F",
    )


@pytest.mark.parametrize(
    ("supported_features", "supported"),
    [
        (1 | 2, True),  # direct open and close
        (4, True),  # both endpoints through numeric positioning
        (1 | 4, True),  # close can fall back to position 0
        (2 | 4, True),  # open can fall back to position 100
        (0, False),
        (1, False),
        (2, False),
        (16 | 32 | 128, False),  # tilt-only cover
        (True, False),
    ],
)
def test_cover_validation_requires_both_fritz_endpoints(
    supported_features: int, *, supported: bool
) -> None:
    assert (
        cover_entity_supported({"supported_features": supported_features})
        is supported
    )


@pytest.mark.parametrize(
    "changes",
    [
        {"supported_features": 0},
        {"supported_features": True},
        {"hvac_modes": ["heat"]},
        {"hvac_modes": ["off", "cool"]},
        {"min_temp": 10},
        {"max_temp": 27},
        {"target_temp_step": 1},
        {"target_temp_step": 0.2},
        {"temperature_unit": "K"},
        {"min_temp": float("nan")},
        {"max_temp": 10**400},
    ],
)
def test_incompatible_climate_entities_are_rejected(
    changes: dict[str, object],
) -> None:
    attributes: dict[str, object] = {
        "supported_features": 1,
        "hvac_modes": ["off", "heat"],
        "min_temp": 5,
        "max_temp": 35,
        "target_temp_step": 0.5,
        "temperature_unit": "°C",
    }
    attributes.update(changes)
    assert not climate_entity_supported(attributes)


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
