# SPDX-License-Identifier: MIT OR Apache-2.0
"""Tests for persistent thermostat override validation."""

import pytest

from custom_components.tentahacle.protocol import ProtocolError
from custom_components.tentahacle.thermostat import (
    ThermostatOverride,
    override_superseded_by_state,
    timer_request,
)


def test_timer_request_accepts_provider_end_time() -> None:
    assert timer_request(
        {"timer_action": "boost", "end_time": 10_900, "duration": 900}, 10_000
    ) == ("boost", 10_900)
    assert timer_request({"timer_action": "cancel"}, 10_000) == ("cancel", 0)


@pytest.mark.parametrize(
    "values",
    [
        {"timer_action": "other", "end_time": 10_900},
        {"timer_action": "boost", "end_time": 9_999},
        {"timer_action": "cold", "end_time": 100_001},
    ],
)
def test_timer_request_rejects_invalid_bounds(values: dict[str, object]) -> None:
    with pytest.raises(ProtocolError):
        timer_request(values, 10_000)


def test_override_storage_round_trip() -> None:
    override = ThermostatOverride(
        action="cold",
        end_time=20_000,
        entity_id="climate.office",
        previous_hvac_mode="auto",
        previous_temperature=21.5,
        previous_preset_mode="schedule",
    )
    assert ThermostatOverride.from_json(override.as_json()) == override


def test_override_storage_rejects_invalid_record() -> None:
    with pytest.raises(ProtocolError):
        ThermostatOverride.from_json(
            {
                "action": "boost",
                "end_time": True,
                "entity_id": "switch.office",
                "previous_hvac_mode": "heat",
                "previous_temperature": 21,
            }
        )


@pytest.mark.parametrize("action", [[], {}])
def test_timer_rejects_unhashable_action(action: object) -> None:
    with pytest.raises(ProtocolError):
        timer_request({"timer_action": action, "end_time": 10_900}, 10_000)
    record = ThermostatOverride("boost", 10_900, "climate.room", "heat", 21).as_json()
    record["action"] = action
    with pytest.raises(ProtocolError):
        ThermostatOverride.from_json(record)


@pytest.mark.parametrize("temperature", [68, 10**400, float("nan"), float("inf")])
def test_override_storage_accepts_fahrenheit_but_rejects_non_finite_values(
    temperature: float,
) -> None:
    record = ThermostatOverride(
        "boost", 10_900, "climate.room", "heat", temperature
    ).as_json()
    if temperature == 68:
        assert ThermostatOverride.from_json(record).previous_temperature == 68
    else:
        with pytest.raises(ProtocolError):
            ThermostatOverride.from_json(record)


@pytest.mark.parametrize(
    ("action", "state", "attributes", "superseded"),
    [
        ("boost", "heat", {"temperature": 35, "max_temp": 35}, False),
        ("boost", "heat", {"temperature": 21, "max_temp": 35}, True),
        ("boost", "off", {"temperature": 35, "max_temp": 35}, True),
        ("boost", "unavailable", {}, False),
        ("cold", "off", {}, False),
        ("cold", "heat", {"temperature": 21}, True),
    ],
)
def test_external_state_change_policy(
    action: str, state: str, attributes: dict[str, object], *, superseded: bool
) -> None:
    override = ThermostatOverride(
        action, 10_900, "climate.room", "heat", 21
    )
    assert override_superseded_by_state(override, state, attributes) is superseded
