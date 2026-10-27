# SPDX-License-Identifier: MIT OR Apache-2.0
"""Persistent thermostat override models."""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass
from typing import Any

from .protocol import ProtocolError

_MAX_OVERRIDE_SECONDS = 25 * 60 * 60


@dataclass(frozen=True, slots=True)
class ThermostatOverride:
    """State needed to resume or finish a 440 Boost/cold override."""

    action: str
    end_time: int
    entity_id: str
    previous_hvac_mode: str
    previous_temperature: float
    previous_preset_mode: str | None = None

    @classmethod
    def from_json(cls, value: object) -> ThermostatOverride:
        """Validate one record loaded from Home Assistant storage."""
        if not isinstance(value, dict):
            raise ProtocolError("override record is not an object")
        try:
            record = cls(
                action=value["action"],
                end_time=value["end_time"],
                entity_id=value["entity_id"],
                previous_hvac_mode=value["previous_hvac_mode"],
                previous_temperature=value["previous_temperature"],
                previous_preset_mode=value.get("previous_preset_mode"),
            )
        except KeyError as err:
            raise ProtocolError("incomplete override record") from err
        if (
            not isinstance(record.action, str)
            or record.action not in {"boost", "cold"}
            or not isinstance(record.end_time, int)
            or isinstance(record.end_time, bool)
            or not isinstance(record.entity_id, str)
            or not record.entity_id.startswith("climate.")
            or not isinstance(record.previous_hvac_mode, str)
            or not record.previous_hvac_mode
            or not isinstance(record.previous_temperature, (int, float))
            or isinstance(record.previous_temperature, bool)
            or not _stored_temperature_valid(record.previous_temperature)
            or (
                record.previous_preset_mode is not None
                and not isinstance(record.previous_preset_mode, str)
            )
        ):
            raise ProtocolError("invalid override record")
        return record

    def as_json(self) -> dict[str, Any]:
        """Return a Home Assistant storage compatible record."""
        return asdict(self)


def timer_request(values: dict[str, Any], now_timestamp: int) -> tuple[str, int]:
    """Validate a timer command emitted by the provider."""
    action = values.get("timer_action")
    if action == "cancel":
        return action, 0
    end_time = values.get("end_time")
    if (
        not isinstance(action, str)
        or action not in {"boost", "cold"}
        or not isinstance(end_time, int)
        or isinstance(end_time, bool)
        or end_time <= now_timestamp
        or end_time > now_timestamp + _MAX_OVERRIDE_SECONDS
    ):
        raise ProtocolError("invalid thermostat timer command")
    return action, end_time


def override_superseded_by_state(
    override: ThermostatOverride, state: str, attributes: dict[str, Any]
) -> bool:
    """Return whether a later source state should cancel a timed override."""
    if state in {"unknown", "unavailable"}:
        return False
    if override.action == "cold":
        return state != "off"
    target = attributes.get("temperature")
    maximum = attributes.get("max_temp")
    if (
        state == "off"
        or not isinstance(target, (int, float))
        or isinstance(target, bool)
        or not isinstance(maximum, (int, float))
        or isinstance(maximum, bool)
    ):
        return True
    try:
        normalized_target = float(target)
        normalized_maximum = float(maximum)
    except (OverflowError, ValueError):
        return True
    return not (
        math.isfinite(normalized_target)
        and math.isfinite(normalized_maximum)
        and math.isclose(normalized_target, normalized_maximum, abs_tol=0.01)
    )


def _stored_temperature_valid(value: float) -> bool:
    try:
        normalized = float(value)
    except OverflowError:
        return False
    return math.isfinite(normalized) and -100 <= normalized <= 300
