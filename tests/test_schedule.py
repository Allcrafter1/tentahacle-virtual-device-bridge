# SPDX-License-Identifier: MIT OR Apache-2.0
"""Tests for the Zigbee2MQTT 5+2 thermostat schedule adapter."""

from datetime import UTC, datetime

import pytest

from custom_components.tentahacle.protocol import ProtocolError
from custom_components.tentahacle.schedule import (
    parse_zigbee2mqtt_day_schedule,
    schedule_shadow_payload,
)

WORK = "06:00/22°C 08:00/18°C 16:00/22°C 23:00/17°C"
HOLIDAY = "06:00/22°C 22:00/17°C"


def test_parser_keeps_exact_minutes_and_half_degrees() -> None:
    assert parse_zigbee2mqtt_day_schedule("06:07/21.5°C 23:23/17°C") == [
        (367, 43),
        (1403, 34),
    ]


def test_shadow_returns_current_and_next_real_change() -> None:
    monday_at_noon = datetime(2026, 10, 5, 12, 0, tzinfo=UTC)
    assert (
        schedule_shadow_payload(monday_at_noon, WORK, HOLIDAY, enabled=True)
        == "active 36 44 480 960"
    )


def test_shadow_wraps_from_sunday_to_monday() -> None:
    sunday_late = datetime(2026, 10, 11, 23, 0, tzinfo=UTC)
    assert (
        schedule_shadow_payload(sunday_late, WORK, HOLIDAY, enabled=True)
        == "active 34 44 9960 360"
    )


def test_shadow_skips_duplicate_temperatures() -> None:
    now = datetime(2026, 10, 5, 7, 0, tzinfo=UTC)
    assert (
        schedule_shadow_payload(
            now,
            "06:00/22°C 08:00/22°C 09:00/18°C",
            HOLIDAY,
            enabled=True,
        )
        == "active 44 36 360 540"
    )


def test_disabled_shadow_does_not_parse_values() -> None:
    assert (
        schedule_shadow_payload(
            datetime(2026, 10, 5, tzinfo=UTC), "invalid", "invalid", enabled=False
        )
        == "disabled"
    )


@pytest.mark.parametrize(
    "value",
    ["", "6:00/22°C", "24:00/22°C", "06:00/22.2°C", "08:00/18°C 06:00/22°C"],
)
def test_parser_rejects_ambiguous_or_unsupported_values(value: str) -> None:
    with pytest.raises(ProtocolError):
        parse_zigbee2mqtt_day_schedule(value)


def test_schedule_rejects_overflow_temperature() -> None:
    with pytest.raises(ProtocolError):
        parse_zigbee2mqtt_day_schedule("00:00/" + "9" * 400 + "°C")
