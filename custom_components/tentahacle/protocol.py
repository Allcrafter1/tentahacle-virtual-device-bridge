# SPDX-License-Identifier: MIT OR Apache-2.0
"""Pure protocol translation for TentAHAcle - Virtual Device Bridge."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any

from .const import DeviceProfile


class ProtocolError(ValueError):
    """An MQTT message violates the bridge protocol."""


@dataclass(frozen=True, slots=True)
class BridgeCommand:
    """Validated command received from the FRITZ provider."""

    endpoint_uid: str
    sequence: int
    values: dict[str, Any]


@dataclass(frozen=True, slots=True)
class CoverCapabilities:
    """Home Assistant services supported by one mapped cover entity."""

    open: bool = True
    close: bool = True
    stop: bool = True
    set_position: bool = True


def parse_command(payload: str | bytes, expected_uid: str) -> BridgeCommand:
    """Parse and validate one provider command."""
    if not isinstance(payload, (str, bytes)) or len(payload) > 4096:
        raise ProtocolError("command exceeds protocol limits")
    try:
        raw = json.loads(payload)
    except (TypeError, ValueError, RecursionError) as err:
        raise ProtocolError("command is not valid JSON") from err
    if not isinstance(raw, dict) or raw.get("event") != "command":
        raise ProtocolError("not a command event")
    if raw.get("endpoint") != expected_uid:
        raise ProtocolError("endpoint does not match MQTT topic")
    sequence = raw.get("sequence")
    if not isinstance(sequence, int) or isinstance(sequence, bool) or sequence < 0:
        raise ProtocolError("invalid command sequence")
    values = {
        key: value
        for key, value in raw.items()
        if key not in {"event", "endpoint", "sequence"}
    }
    if not values:
        raise ProtocolError("empty command")
    return BridgeCommand(expected_uid, sequence, values)


def service_call_for_command(
    profile: DeviceProfile,
    entity_id: str,
    command: BridgeCommand,
    *,
    cover_capabilities: CoverCapabilities | None = None,
    color_full: bool = False,
) -> tuple[str, str, dict[str, Any]]:
    """Translate a provider command to one Home Assistant service call."""
    values = command.values
    domain = entity_id.partition(".")[0]

    if profile is DeviceProfile.SWITCH:
        state = _binary_state(values)
        return domain, "turn_on" if state else "turn_off", {"entity_id": entity_id}

    if profile in {
        DeviceProfile.DIMMABLE_LIGHT,
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
    }:
        return _light_service_call(profile, entity_id, values, color_full=color_full)

    if profile is DeviceProfile.COVER:
        return _cover_service_call(
            entity_id, values, cover_capabilities or CoverCapabilities()
        )

    if profile is DeviceProfile.THERMOSTAT:
        mode = values.get("hvac_mode")
        if mode == "off":
            return (
                "climate",
                "set_hvac_mode",
                {
                    "entity_id": entity_id,
                    "hvac_mode": "off",
                },
            )
        if mode == "heat" and "target_temperature" in values:
            temperature = values["target_temperature"]
            if (
                not isinstance(temperature, (int, float))
                or isinstance(temperature, bool)
                or not 8 <= temperature <= 28
            ):
                raise ProtocolError("invalid target temperature")
            return (
                "climate",
                "set_temperature",
                {
                    "entity_id": entity_id,
                    "temperature": float(temperature),
                    "hvac_mode": "heat",
                },
            )
        if mode == "heat":
            return (
                "climate",
                "set_hvac_mode",
                {
                    "entity_id": entity_id,
                    "hvac_mode": "heat",
                },
            )
        if "timer_action" in values:
            raise ProtocolError("thermostat timer needs a configured adapter")
        raise ProtocolError("unsupported thermostat command")

    raise ProtocolError(f"unsupported profile: {profile}")


def _light_service_call(
    profile: DeviceProfile,
    entity_id: str,
    values: dict[str, Any],
    *,
    color_full: bool,
) -> tuple[str, str, dict[str, Any]]:
    """Translate a validated FRITZ light command."""
    state = _binary_state(values)
    # AHA reports the current relay state together with value commands. A value
    # command received while off is still a request to apply that value.
    has_hs = "hue" in values or "saturation" in values
    has_value_command = "level" in values or "color_temperature" in values or has_hs
    if not state and not has_value_command:
        return "light", "turn_off", {"entity_id": entity_id}
    data: dict[str, Any] = {"entity_id": entity_id}
    if "level" in values:
        data["brightness_pct"] = _integer(values, "level", 0, 100)
    if "color_temperature" in values:
        if profile is not DeviceProfile.COLOR_TEMPERATURE_LIGHT:
            raise ProtocolError("color temperature is not enabled")
        data["color_temp_kelvin"] = _integer(
            values, "color_temperature", 1000, 10000
        )
    if has_hs:
        if profile is not DeviceProfile.COLOR_TEMPERATURE_LIGHT or not color_full:
            raise ProtocolError("full color is not enabled")
        if "color_temperature" in values:
            raise ProtocolError("multiple color modes in one command")
        hue = _integer(values, "hue", 0, 359)
        saturation = _integer(values, "saturation", 0, 255)
        data["hs_color"] = (hue, saturation * 100 / 255)
    return "light", "turn_on", data


def _cover_service_call(
    entity_id: str,
    values: dict[str, Any],
    capabilities: CoverCapabilities,
) -> tuple[str, str, dict[str, Any]]:
    """Translate a validated command for the mapped cover capabilities."""
    action = values.get("action")
    if action == "stop":
        if not capabilities.stop:
            raise ProtocolError("cover does not support stop")
        return "cover", "stop_cover", {"entity_id": entity_id}
    if action == "open":
        if capabilities.open:
            return "cover", "open_cover", {"entity_id": entity_id}
        if capabilities.set_position:
            return "cover", "set_cover_position", {
                "entity_id": entity_id,
                "position": 100,
            }
        raise ProtocolError("cover does not support open")
    if action == "close":
        if capabilities.close:
            return "cover", "close_cover", {"entity_id": entity_id}
        if capabilities.set_position:
            return "cover", "set_cover_position", {
                "entity_id": entity_id,
                "position": 0,
            }
        raise ProtocolError("cover does not support close")
    position = _integer(values, "position", 0, 100)
    if capabilities.set_position:
        return "cover", "set_cover_position", {
            "entity_id": entity_id,
            "position": position,
        }
    if position == 0 and capabilities.close:
        return "cover", "close_cover", {"entity_id": entity_id}
    if position == 100 and capabilities.open:
        return "cover", "open_cover", {"entity_id": entity_id}
    raise ProtocolError("cover does not support intermediate positions")


def feedback_for_state(
    profile: DeviceProfile,
    state: str,
    attributes: dict[str, Any],
    *,
    color_full: bool = False,
) -> dict[str, str | None]:
    """Translate one HA state into retained provider feedback properties."""
    unavailable = state in {"unknown", "unavailable"}
    if unavailable:
        return {}

    if profile is DeviceProfile.SWITCH:
        return {"state": "ON" if state == "on" else "OFF"}

    if profile in {
        DeviceProfile.DIMMABLE_LIGHT,
        DeviceProfile.COLOR_TEMPERATURE_LIGHT,
    }:
        result = {"state": "ON" if state == "on" else "OFF"}
        brightness = attributes.get("brightness")
        if (
            isinstance(brightness, int)
            and not isinstance(brightness, bool)
            and 0 <= brightness <= 255
        ):
            result["level"] = str(round(brightness * 100 / 255))
        color_mode = attributes.get("color_mode")
        color_temperature = attributes.get("color_temp_kelvin")
        if profile is DeviceProfile.COLOR_TEMPERATURE_LIGHT and not color_full:
            # Also removes a retained HS value after rebinding/downgrading an
            # existing full-colour mapping.
            result["hs"] = None
        if (
            profile is DeviceProfile.COLOR_TEMPERATURE_LIGHT
            and (not color_full or color_mode == "color_temp")
            and isinstance(color_temperature, (int, float))
            and not isinstance(color_temperature, bool)
            and 1000 <= color_temperature <= 10000
        ):
            if color_full:
                result["hs"] = None
            result["color_temperature"] = str(round(color_temperature))
        if color_full and color_mode in {"hs", "xy", "rgb", "rgbw", "rgbww"}:
            hs_color = attributes.get("hs_color")
            if (
                isinstance(hs_color, (list, tuple))
                and len(hs_color) == 2
                and all(
                    isinstance(value, (int, float))
                    and not isinstance(value, bool)
                    for value in hs_color
                )
                and 0 <= hs_color[0] <= 360
                and 0 <= hs_color[1] <= 100
            ):
                hue = min(359, int(float(hs_color[0]) + 0.5))
                saturation = int(float(hs_color[1]) * 255 / 100 + 0.5)
                result["color_temperature"] = None
                result["hs"] = f"{hue} {saturation}"
        return result

    if profile is DeviceProfile.COVER:
        position = attributes.get("current_position")
        if (
            isinstance(position, int)
            and not isinstance(position, bool)
            and 0 <= position <= 100
        ):
            return {"position": str(position)}
        # Binary covers do not expose current_position. Only publish confirmed
        # end states; opening/closing must not be mistaken for an arrived end
        # position.
        if state == "open":
            return {"position": "100"}
        if state == "closed":
            return {"position": "0"}
        return {}

    if profile is DeviceProfile.THERMOSTAT:
        result = {}
        temperature = attributes.get("temperature")
        if (
            isinstance(temperature, (int, float))
            and not isinstance(temperature, bool)
            and 7.5 <= temperature <= 28.5
        ):
            rounded = round(float(temperature) * 2) / 2
            if 8 <= rounded <= 28:
                result["target_temperature"] = f"{rounded:.1f}"
        # Publish mode last so an OFF snapshot wins over its remembered setpoint.
        result["hvac_mode"] = "off" if state == "off" else "heat"
        return result

    return {}


def _binary_state(values: dict[str, Any]) -> bool:
    state = values.get("state")
    if not isinstance(state, (str, int)) or isinstance(state, bool):
        raise ProtocolError("invalid binary state")
    if state in {1, "1", "ON", "on"}:
        return True
    if state in {0, "0", "OFF", "off"}:
        return False
    raise ProtocolError("invalid binary state")


def _integer(values: dict[str, Any], key: str, minimum: int, maximum: int) -> int:
    value = values.get(key)
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or not minimum <= value <= maximum
    ):
        raise ProtocolError(f"invalid {key}")
    return value
