# SPDX-License-Identifier: MIT OR Apache-2.0
"""Tests for Home Assistant/protocol translation."""

import pytest

from custom_components.tentahacle.const import DeviceProfile
from custom_components.tentahacle.protocol import (
    ClimateCapabilities,
    CoverCapabilities,
    LightCapabilities,
    ProtocolError,
    feedback_for_state,
    parse_command,
    service_call_for_command,
)

UID = "TVB0123456789ABCDEF"


def command(body: str):
    """Parse a test command."""
    return parse_command(body, UID)


def test_light_command_maps_level_to_brightness_percentage() -> None:
    parsed = command(
        '{"event":"command","endpoint":"TVB0123456789ABCDEF",'
        '"state":1,"level":42,"sequence":7}'
    )
    assert service_call_for_command(
        DeviceProfile.DIMMABLE_LIGHT, "light.desk", parsed
    ) == ("light", "turn_on", {"entity_id": "light.desk", "brightness_pct": 42})


@pytest.mark.parametrize(
    "entity_id", ["switch.socket", "input_boolean.scene", "light.basic"]
)
@pytest.mark.parametrize(("value", "service"), [(1, "turn_on"), (0, "turn_off")])
def test_switch_profile_calls_source_domain(
    entity_id: str, value: int, service: str
) -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","state":{value},"sequence":7}}'
    )
    assert service_call_for_command(DeviceProfile.SWITCH, entity_id, parsed) == (
        entity_id.partition(".")[0],
        service,
        {"entity_id": entity_id},
    )


@pytest.mark.parametrize(("state", "expected"), [("on", "ON"), ("off", "OFF")])
def test_switch_profile_reports_confirmed_external_state(
    state: str, expected: str
) -> None:
    assert feedback_for_state(DeviceProfile.SWITCH, state, {}) == {
        "state": expected
    }


def test_color_temperature_uses_current_kelvin_action() -> None:
    parsed = command(
        '{"event":"command","endpoint":"TVB0123456789ABCDEF",'
        '"state":1,"color_temperature":4200,"sequence":7}'
    )
    assert service_call_for_command(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT, "light.desk", parsed
    ) == (
        "light",
        "turn_on",
        {"entity_id": "light.desk", "color_temp_kelvin": 4200},
    )


@pytest.mark.parametrize(
    ("requested", "minimum", "maximum", "expected"),
    [
        (2700, 3000, 6500, 3000),
        (4200, 2000, 6535, 4200),
        (6500, 2000, 5000, 5000),
    ],
)
def test_color_temperature_respects_source_kelvin_range(
    requested: int, minimum: float, maximum: float, expected: int
) -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","state":1,'
        f'"color_temperature":{requested},"sequence":7}}'
    )
    assert service_call_for_command(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        "light.desk",
        parsed,
        entity_capabilities=LightCapabilities(
            frozenset({"color_temp"}), minimum, maximum
        ),
    ) == (
        "light",
        "turn_on",
        {"entity_id": "light.desk", "color_temp_kelvin": expected},
    )


@pytest.mark.parametrize(
    ("minimum", "maximum"),
    [(4000, 3000), (float("inf"), 900), (10001, None)],
)
def test_color_temperature_rejects_empty_source_kelvin_range(
    minimum: float | None, maximum: float | None
) -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","state":1,'
        '"color_temperature":4200,"sequence":7}'
    )
    with pytest.raises(ProtocolError, match="range"):
        service_call_for_command(
            DeviceProfile.COLOR_TEMPERATURE_LIGHT,
            "light.desk",
            parsed,
            entity_capabilities=LightCapabilities(
                frozenset({"color_temp"}), minimum, maximum
            ),
        )


@pytest.mark.parametrize(
    ("profile", "value", "expected"),
    [
        (
            DeviceProfile.DIMMABLE_LIGHT,
            '"level":42',
            {"entity_id": "light.desk", "brightness_pct": 42},
        ),
        (
            DeviceProfile.COLOR_TEMPERATURE_LIGHT,
            '"color_temperature":2700',
            {"entity_id": "light.desk", "color_temp_kelvin": 2700},
        ),
    ],
)
def test_light_value_command_is_not_lost_while_current_state_is_off(
    profile: DeviceProfile, value: str, expected: dict[str, object]
) -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}",'
        f'"state":0,{value},"sequence":7}}'
    )
    assert service_call_for_command(profile, "light.desk", parsed) == (
        "light",
        "turn_on",
        expected,
    )


def test_light_state_only_off_command_still_turns_light_off() -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","state":0,"sequence":7}}'
    )
    assert service_call_for_command(
        DeviceProfile.DIMMABLE_LIGHT, "light.desk", parsed
    ) == ("light", "turn_off", {"entity_id": "light.desk"})


def test_color_temperature_feedback_uses_confirmed_value() -> None:
    assert feedback_for_state(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        "on",
        {"brightness": 128, "color_temp_kelvin": 3003},
    ) == {
        "state": "ON",
        "level": "50",
        "hs": None,
        "color_temperature": "3000",
    }


@pytest.mark.parametrize(
    ("confirmed", "expected"),
    [(3003, "3000"), (6535, "6500"), (2232, "2700"), (3200, "3000")],
)
def test_color_temperature_feedback_uses_native_440_palette(
    confirmed: float, expected: str
) -> None:
    assert feedback_for_state(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        "on",
        {"color_temp_kelvin": confirmed},
    )["color_temperature"] == expected


def test_color_temperature_feedback_respects_source_range() -> None:
    assert feedback_for_state(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        "on",
        {"color_temp_kelvin": 3003},
        light_capabilities=LightCapabilities(
            frozenset({"color_temp"}), 3200, 4500
        ),
    )["color_temperature"] == "3400"


def test_color_temperature_feedback_omits_value_when_source_misses_palette() -> None:
    assert feedback_for_state(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        "on",
        {"color_temp_kelvin": 2400},
        light_capabilities=LightCapabilities(
            frozenset({"color_temp"}), 2000, 2500
        ),
    ) == {"state": "ON", "hs": None}


def test_full_color_command_maps_fritz_hs_to_home_assistant_scale() -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","state":0,'
        '"hue":120,"saturation":128,"sequence":7}'
    )
    assert service_call_for_command(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        "light.desk",
        parsed,
        color_full=True,
    ) == (
        "light",
        "turn_on",
        {"entity_id": "light.desk", "hs_color": (120, 128 * 100 / 255)},
    )


def test_full_color_feedback_uses_active_mode_and_clears_stale_temperature() -> None:
    assert feedback_for_state(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        "on",
        {
            "brightness": 255,
            "color_mode": "rgb",
            "color_temp_kelvin": 3000,
            "hs_color": (359.8, 50),
        },
        color_full=True,
    ) == {
        "state": "ON",
        "level": "100",
        "color_temperature": None,
        "hs": "359 128",
    }


def test_full_color_temperature_feedback_clears_stale_hs() -> None:
    assert feedback_for_state(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        "on",
        {
            "color_mode": "color_temp",
            "color_temp_kelvin": 4200,
            "hs_color": (20, 40),
        },
        color_full=True,
    ) == {
        "state": "ON",
        "hs": None,
        "color_temperature": "4200",
    }


def test_missing_color_mode_uses_kelvin_only_for_ct_only_source() -> None:
    assert feedback_for_state(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        "on",
        {
            "color_temp_kelvin": 3000,
            "hs_color": (20, 40),
        },
        color_full=True,
        light_capabilities=LightCapabilities(frozenset({"color_temp"})),
    ) == {
        "state": "ON",
        "hs": None,
        "color_temperature": "3000",
    }


def test_missing_color_mode_uses_hs_for_rgb_only_source() -> None:
    assert feedback_for_state(
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        "on",
        {
            "color_temp_kelvin": 3000,
            "hs_color": (20, 40),
        },
        color_full=True,
        light_capabilities=LightCapabilities(frozenset({"rgb"})),
    ) == {
        "state": "ON",
        "color_temperature": None,
        "hs": "20 102",
    }


def test_ct_only_profile_rejects_hs_command() -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","state":1,'
        '"hue":120,"saturation":128,"sequence":7}'
    )
    with pytest.raises(ProtocolError, match="full color"):
        service_call_for_command(
            DeviceProfile.COLOR_TEMPERATURE_LIGHT, "light.desk", parsed
        )


@pytest.mark.parametrize(
    "values",
    [
        '"hue":360,"saturation":255',
        '"hue":120,"saturation":256',
        '"hue":120',
        '"saturation":128',
        '"hue":120,"saturation":128,"color_temperature":3000',
    ],
)
def test_full_color_command_rejects_invalid_or_ambiguous_values(values: str) -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","state":1,'
        f'{values},"sequence":7}}'
    )
    with pytest.raises(ProtocolError):
        service_call_for_command(
            DeviceProfile.COLOR_TEMPERATURE_LIGHT,
            "light.desk",
            parsed,
            color_full=True,
        )


@pytest.mark.parametrize(
    ("action", "service"),
    [("open", "open_cover"), ("close", "close_cover"), ("stop", "stop_cover")],
)
def test_cover_actions(action: str, service: str) -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","action":"{action}","sequence":8}}'
    )
    assert (
        service_call_for_command(DeviceProfile.COVER, "cover.blind", parsed)[1]
        == service
    )


@pytest.mark.parametrize(
    ("position", "service"),
    [(0, "close_cover"), (100, "open_cover")],
)
def test_binary_cover_maps_end_positions_to_supported_actions(
    position: int, service: str
) -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}",'
        f'"position":{position},"sequence":8}}'
    )
    assert service_call_for_command(
        DeviceProfile.COVER,
        "cover.blind",
        parsed,
        entity_capabilities=CoverCapabilities(
            open=True, close=True, stop=False, set_position=False
        ),
    ) == ("cover", service, {"entity_id": "cover.blind"})


def test_binary_cover_rejects_unsupported_intermediate_position() -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","position":50,"sequence":8}}'
    )
    with pytest.raises(ProtocolError, match="intermediate positions"):
        service_call_for_command(
            DeviceProfile.COVER,
            "cover.blind",
            parsed,
            entity_capabilities=CoverCapabilities(
                open=True, close=True, stop=False, set_position=False
            ),
        )


def test_cover_rejects_unsupported_stop() -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","action":"stop","sequence":8}}'
    )
    with pytest.raises(ProtocolError, match="support stop"):
        service_call_for_command(
            DeviceProfile.COVER,
            "cover.blind",
            parsed,
            entity_capabilities=CoverCapabilities(stop=False),
        )


@pytest.mark.parametrize(
    ("action", "position"), [("open", 100), ("close", 0)]
)
def test_position_only_cover_maps_actions_to_end_positions(
    action: str, position: int
) -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}",'
        f'"action":"{action}","sequence":8}}'
    )
    assert service_call_for_command(
        DeviceProfile.COVER,
        "cover.blind",
        parsed,
        entity_capabilities=CoverCapabilities(
            open=False, close=False, stop=False, set_position=True
        ),
    ) == (
        "cover",
        "set_cover_position",
        {"entity_id": "cover.blind", "position": position},
    )


@pytest.mark.parametrize(
    ("state", "attributes", "expected"),
    [
        ("open", {}, {"position": "100"}),
        ("closed", {}, {"position": "0"}),
        ("opening", {}, {}),
        ("closing", {}, {}),
        ("opening", {"current_position": 34}, {"position": "34"}),
        ("closing", {"current_position": 66}, {"position": "66"}),
        ("open", {"current_position": 0.0}, {"position": "0"}),
        ("open", {"current_position": 50.0}, {"position": "50"}),
        ("closed", {"current_position": 100.0}, {"position": "100"}),
        ("opening", {"current_position": 49.49}, {"position": "49"}),
        ("opening", {"current_position": 49.5}, {"position": "50"}),
    ],
)
def test_cover_feedback_handles_binary_and_position_covers(
    state: str, attributes: dict[str, object], expected: dict[str, str]
) -> None:
    assert feedback_for_state(DeviceProfile.COVER, state, attributes) == expected


@pytest.mark.parametrize(
    "position", [True, False, float("nan"), float("inf"), float("-inf"), -0.1, 100.1]
)
def test_cover_feedback_rejects_invalid_numeric_positions(position: object) -> None:
    assert feedback_for_state(
        DeviceProfile.COVER,
        "opening",
        {"current_position": position},
    ) == {}


def test_state_feedback_rounds_climate_to_half_degrees() -> None:
    assert feedback_for_state(
        DeviceProfile.THERMOSTAT, "heat", {"temperature": 20.26}
    ) == {
        "target_temperature": "20.5",
        "current_temperature": "unknown",
        "hvac_mode": "heat",
    }


def test_thermostat_max_command_uses_source_maximum() -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","hvac_mode":"heat",'
        '"target_temperature":"max","sequence":7}'
    )
    assert service_call_for_command(
        DeviceProfile.THERMOSTAT,
        "climate.room",
        parsed,
        entity_capabilities=ClimateCapabilities(max_temperature=35.0),
    ) == (
        "climate",
        "set_temperature",
        {"entity_id": "climate.room", "temperature": 35.0, "hvac_mode": "heat"},
    )


def test_thermostat_feedback_preserves_max_and_actual_temperature() -> None:
    assert feedback_for_state(
        DeviceProfile.THERMOSTAT,
        "heat",
        {"temperature": 35.0, "max_temp": 35.0, "current_temperature": 20.34},
    ) == {
        "target_temperature": "max",
        "current_temperature": "20.3",
        "hvac_mode": "heat",
    }


def test_thermostat_feedback_preserves_max_while_off() -> None:
    feedback = feedback_for_state(
        DeviceProfile.THERMOSTAT,
        "off",
        {"temperature": 35.0, "max_temp": 35.0, "current_temperature": 19.8},
    )
    assert feedback["target_temperature"] == "max"
    assert feedback["hvac_mode"] == "off"


@pytest.mark.parametrize(
    "temperature", [float("nan"), float("inf"), -float("inf"), 10**400, -51, 101]
)
def test_invalid_actual_temperature_is_explicitly_unknown(
    temperature: float,
) -> None:
    assert feedback_for_state(
        DeviceProfile.THERMOSTAT,
        "heat",
        {"temperature": 20, "current_temperature": temperature},
    )["current_temperature"] == "unknown"


def test_thermostat_max_command_requires_source_maximum() -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","hvac_mode":"heat",'
        '"target_temperature":"max","sequence":7}'
    )
    with pytest.raises(ProtocolError):
        service_call_for_command(DeviceProfile.THERMOSTAT, "climate.room", parsed)


def test_fahrenheit_climate_command_converts_fritz_celsius() -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","hvac_mode":"heat",'
        '"target_temperature":20.0,"sequence":7}'
    )
    assert service_call_for_command(
        DeviceProfile.THERMOSTAT,
        "climate.room",
        parsed,
        entity_capabilities=ClimateCapabilities(
            max_temperature=95.0, temperature_unit="°F"
        ),
    ) == (
        "climate",
        "set_temperature",
        {"entity_id": "climate.room", "temperature": 68.0, "hvac_mode": "heat"},
    )


def test_fahrenheit_climate_feedback_converts_to_fritz_celsius() -> None:
    assert feedback_for_state(
        DeviceProfile.THERMOSTAT,
        "heat",
        {"temperature": 68.0, "max_temp": 95.0, "current_temperature": 72.0},
        temperature_unit="°F",
    ) == {
        "target_temperature": "20.0",
        "current_temperature": "22.2",
        "hvac_mode": "heat",
    }


def test_fahrenheit_max_feedback_and_command_keep_native_maximum() -> None:
    feedback = feedback_for_state(
        DeviceProfile.THERMOSTAT,
        "heat",
        {"temperature": 95.0, "max_temp": 95.0, "current_temperature": 68.0},
        temperature_unit="°F",
    )
    assert feedback["target_temperature"] == "max"
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","hvac_mode":"heat",'
        '"target_temperature":"max","sequence":7}'
    )
    assert service_call_for_command(
        DeviceProfile.THERMOSTAT,
        "climate.room",
        parsed,
        entity_capabilities=ClimateCapabilities(95.0, "°F"),
    )[2]["temperature"] == 95.0


@pytest.mark.parametrize("state", ["unknown", "unavailable"])
def test_unavailable_thermostat_clears_actual_temperature(state: str) -> None:
    assert feedback_for_state(DeviceProfile.THERMOSTAT, state, {}) == {
        "current_temperature": "unknown"
    }


def test_parser_rejects_topic_payload_endpoint_mismatch() -> None:
    with pytest.raises(ProtocolError):
        parse_command(
            '{"event":"command","endpoint":"TVBFFFFFFFFFFFFFFFF","state":1,"sequence":1}',
            UID,
        )


@pytest.mark.parametrize(
    "temperature", [float("nan"), float("inf"), -float("inf"), 10**400]
)
def test_feedback_ignores_invalid_temperature(temperature: float) -> None:
    assert feedback_for_state(
        DeviceProfile.THERMOSTAT, "heat", {"temperature": temperature}
    ) == {"current_temperature": "unknown", "hvac_mode": "heat"}


@pytest.mark.parametrize("state", ["[]", "{}", "true"])
def test_binary_command_rejects_invalid_json_types(state: str) -> None:
    parsed = command(
        f'{{"event":"command","endpoint":"{UID}","state":{state},"sequence":1}}'
    )
    with pytest.raises(ProtocolError):
        service_call_for_command(DeviceProfile.SWITCH, "switch.room", parsed)


def test_thermostat_feedback_publishes_mode_after_target() -> None:
    feedback = feedback_for_state(DeviceProfile.THERMOSTAT, "off", {"temperature": 21})
    assert list(feedback.items()) == [
        ("target_temperature", "21.0"),
        ("current_temperature", "unknown"),
        ("hvac_mode", "off"),
    ]


@pytest.mark.parametrize("brightness", [-1, 256, 10**400])
def test_invalid_brightness_does_not_break_or_escape_feedback(brightness: int) -> None:
    assert feedback_for_state(
        DeviceProfile.DIMMABLE_LIGHT, "on", {"brightness": brightness}
    ) == {"state": "ON"}


@pytest.mark.parametrize("payload", ["[" * 1500 + "]" * 1500, "1" * 5000])
def test_excessive_json_is_rejected_as_protocol_error(payload: str) -> None:
    with pytest.raises(ProtocolError):
        parse_command(payload, UID)
