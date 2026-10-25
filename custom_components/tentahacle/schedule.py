# SPDX-License-Identifier: MIT OR Apache-2.0
"""Adapters for the optional Home Assistant owned thermostat schedule shadow."""

from __future__ import annotations

import re
from dataclasses import dataclass
from datetime import datetime

from .protocol import ProtocolError

_SCHEDULE_ITEM = re.compile(
    r"^(?P<hour>[01][0-9]|2[0-3]):(?P<minute>[0-5][0-9])/"
    r"(?P<temperature>-?[0-9]+(?:\.[0-9]+)?)°C$"
)
_WEEK_MINUTES = 7 * 24 * 60


@dataclass(frozen=True, slots=True)
class ScheduleTransition:
    """One weekly temperature transition in Home Assistant week order."""

    minute: int
    half_degrees: int


def parse_zigbee2mqtt_day_schedule(value: str) -> list[tuple[int, int]]:
    """Parse Zigbee2MQTT's `HH:MM/NN°C` day schedule."""
    result: list[tuple[int, int]] = []
    previous_minute = -1
    for raw_item in value.split():
        match = _SCHEDULE_ITEM.fullmatch(raw_item)
        if match is None:
            raise ProtocolError("invalid Zigbee2MQTT schedule item")
        minute = int(match["hour"]) * 60 + int(match["minute"])
        temperature = float(match["temperature"])
        if not 8 <= temperature <= 28:
            raise ProtocolError("invalid Zigbee2MQTT schedule temperature")
        half_degrees = round(temperature * 2)
        if minute <= previous_minute or abs(temperature * 2 - half_degrees) > 0.01:
            raise ProtocolError("invalid Zigbee2MQTT schedule value")
        result.append((minute, half_degrees))
        previous_minute = minute
    if not result:
        raise ProtocolError("empty Zigbee2MQTT schedule")
    return result


def schedule_shadow_payload(
    now: datetime,
    workdays: str,
    holidays: str,
    *,
    enabled: bool,
) -> str:
    """Return the rolling two-transition FRITZ schedule representation."""
    if not enabled:
        return "disabled"
    workday_transitions = parse_zigbee2mqtt_day_schedule(workdays)
    holiday_transitions = parse_zigbee2mqtt_day_schedule(holidays)
    entries = [
        ScheduleTransition(day * 1440 + minute, half_degrees)
        for day in range(7)
        for minute, half_degrees in (
            workday_transitions if day < 5 else holiday_transitions
        )
    ]
    week_minute = now.weekday() * 1440 + now.hour * 60 + now.minute
    current = entries[-1]
    for entry in entries:
        if entry.minute > week_minute:
            break
        current = entry
    for offset in (0, _WEEK_MINUTES):
        for entry in entries:
            candidate = entry.minute + offset
            if candidate > week_minute and entry.half_degrees != current.half_degrees:
                return (
                    f"active {current.half_degrees} {entry.half_degrees} "
                    f"{current.minute} {entry.minute}"
                )
    return "disabled"
