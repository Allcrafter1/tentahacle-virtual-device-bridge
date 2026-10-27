# SPDX-License-Identifier: MIT OR Apache-2.0
"""Pure protocol translation for TentAHAcle - Virtual Device Bridge."""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from typing import Any

from .const import DeviceProfile

_FRITZ_COLOR_TEMPERATURES = (2700, 3000, 3400, 3800, 4200, 4700, 5300, 5900, 6500)


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


@dataclass(frozen=True, slots=True)
class LightCapabilities:
    """Home Assistant colour modes and Kelvin limits for one light."""

    supported_color_modes: frozenset[str] = frozenset()
    min_color_temperature: float | None = None
    max_color_temperature: float | None = None


@dataclass(frozen=True, slots=True)
class ClimateCapabilities:
    """Home Assistant limits needed to preserve FRITZ thermostat semantics."""

    max_temperature: float | None = None
    temperature_unit: str = "°C"


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
    entity_capabilities: (
        LightCapabilities | CoverCapabilities | ClimateCapabilities | None
    ) = None,
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
        light_capabilities = (
            entity_capabilities
            if isinstance(entity_capabilities, LightCapabilities)
            else LightCapabilities()
        )
        return _light_service_call(
            profile,
            entity_id,
            values,
            capabilities=light_capabilities,
            color_full=color_full,
        )

    if profile is DeviceProfile.COVER:
        cover_capabilities = (
            entity_capabilities
            if isinstance(entity_capabilities, CoverCapabilities)
            else CoverCapabilities()
        )
        return _cover_service_call(
            entity_id, values, cover_capabilities
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
            climate_capabilities = (
                entity_capabilities
                if isinstance(entity_capabilities, ClimateCapabilities)
                else ClimateCapabilities()
            )
            if temperature == "max":
                maximum = climate_capabilities.max_temperature
                normalized_maximum = _finite_float(maximum)
                if normalized_maximum is None:
                    raise ProtocolError("invalid climate maximum temperature")
                return (
                    "climate",
                    "set_temperature",
                    {
                        "entity_id": entity_id,
                        "temperature": normalized_maximum,
                        "hvac_mode": "heat",
                    },
                )
            if (
                not isinstance(temperature, (int, float))
                or isinstance(temperature, bool)
                or not 8 <= temperature <= 28
            ):
                raise ProtocolError("invalid target temperature")
            native_temperature = _celsius_to_native(
                float(temperature), climate_capabilities.temperature_unit
            )
            return (
                "climate",
                "set_temperature",
                {
                    "entity_id": entity_id,
                    "temperature": native_temperature,
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
    capabilities: LightCapabilities,
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
        requested = _integer(values, "color_temperature", 1000, 10000)
        data["color_temp_kelvin"] = _kelvin_for_capabilities(
            requested, capabilities
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


def feedback_for_state(  # noqa: PLR0913
    profile: DeviceProfile,
    state: str,
    attributes: dict[str, Any],
    *,
    color_full: bool = False,
    light_capabilities: LightCapabilities | None = None,
    temperature_unit: str = "°C",
) -> dict[str, str | None]:
    """Translate one HA state into retained provider feedback properties."""
    unavailable = state in {"unknown", "unavailable"}
    if unavailable:
        if profile is DeviceProfile.THERMOSTAT:
            return {"current_temperature": "unknown"}
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
        capabilities = light_capabilities or LightCapabilities()
        color_mode = attributes.get("color_mode")
        color_temperature = _finite_float(attributes.get("color_temp_kelvin"))
        supported_color_modes = capabilities.supported_color_modes
        chromatic_modes = {"hs", "xy", "rgb", "rgbw", "rgbww"}
        use_temperature = (
            not color_full
            or color_mode == "color_temp"
            or (
                color_full
                and color_mode in {None, ""}
                and "color_temp" in supported_color_modes
                and not supported_color_modes & chromatic_modes
            )
        )
        use_hs = color_full and (
            color_mode in chromatic_modes
            or (
                color_mode in {None, ""}
                and bool(supported_color_modes & chromatic_modes)
                and "color_temp" not in supported_color_modes
            )
        )
        if profile is DeviceProfile.COLOR_TEMPERATURE_LIGHT and not color_full:
            # Also removes a retained HS value after rebinding/downgrading an
            # existing full-colour mapping.
            result["hs"] = None
        if (
            profile is DeviceProfile.COLOR_TEMPERATURE_LIGHT
            and use_temperature
            and color_temperature is not None
            and 1000 <= color_temperature <= 10000
        ):
            if color_full:
                result["hs"] = None
            # The native 440 widget steps through a fixed discrete Kelvin
            # palette.  Several HA integrations confirm nearby physical values
            # (for example 3003 K or 6535 K); feeding those values back makes
            # the next native step repeat or wrap.  Report the closest palette
            # entry that the source says it can represent.
            normalized_temperature = _feedback_kelvin_for_capabilities(
                color_temperature, capabilities
            )
            if normalized_temperature is not None:
                result["color_temperature"] = str(normalized_temperature)
        if use_hs:
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
        position = _finite_float(attributes.get("current_position"))
        if position is not None and 0 <= position <= 100:
            # Template and several device integrations expose percentages as
            # floats even though the FRITZ level is an integer.  Keep the
            # source's confirmed position instead of falling back to its broad
            # open/closed state (which would turn every non-zero position into
            # 100 percent).
            return {"position": str(int(position + 0.5))}
        # Binary covers do not expose current_position. Only publish confirmed
        # end states; opening/closing must not be mistaken for an arrived end
        # position.
        if state == "open":
            return {"position": "100"}
        if state == "closed":
            return {"position": "0"}
        return {}

    if profile is DeviceProfile.THERMOSTAT:
        return _thermostat_feedback(state, attributes, temperature_unit)

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


def _kelvin_for_capabilities(
    requested: int, capabilities: LightCapabilities
) -> int:
    """Clamp a FRITZ palette value to finite limits advertised by HA."""
    minimum = _finite_float(capabilities.min_color_temperature)
    maximum = _finite_float(capabilities.max_color_temperature)
    if minimum is not None and maximum is not None and minimum > maximum:
        raise ProtocolError("invalid light color-temperature range")
    lower = math.ceil(minimum) if minimum is not None else 1000
    upper = math.floor(maximum) if maximum is not None else 10000
    lower = max(1000, lower)
    upper = min(10000, upper)
    if lower > upper:
        raise ProtocolError("invalid light color-temperature range")
    return min(max(requested, lower), upper)


def _feedback_kelvin_for_capabilities(
    confirmed: float, capabilities: LightCapabilities
) -> int | None:
    """Map a confirmed HA value onto the 440's discrete Kelvin palette."""
    minimum = _finite_float(capabilities.min_color_temperature)
    maximum = _finite_float(capabilities.max_color_temperature)
    if minimum is not None and maximum is not None and minimum > maximum:
        return None
    choices = tuple(
        value
        for value in _FRITZ_COLOR_TEMPERATURES
        if (minimum is None or value >= minimum)
        and (maximum is None or value <= maximum)
    )
    if not choices:
        return None
    # The palette value itself is the secondary key, so an exact midpoint
    # deterministically selects the warmer (lower-Kelvin) entry.
    return min(choices, key=lambda value: (abs(value - confirmed), value))


def _thermostat_feedback(
    state: str, attributes: dict[str, Any], temperature_unit: str
) -> dict[str, str]:
    """Translate target, actual temperature and mode in a deterministic order."""
    result: dict[str, str] = {}
    normalized_temperature = _finite_float(attributes.get("temperature"))
    if normalized_temperature is not None:
        maximum = _finite_float(attributes.get("max_temp"))
        is_maximum = maximum is not None and math.isclose(
            normalized_temperature, maximum, abs_tol=0.01
        )
        if is_maximum:
            result["target_temperature"] = "max"
        else:
            celsius = _native_to_celsius(normalized_temperature, temperature_unit)
            rounded = round(celsius * 2) / 2 if celsius is not None else None
            if rounded is not None and 8 <= rounded <= 28:
                result["target_temperature"] = f"{rounded:.1f}"
    current_temperature = _finite_float(attributes.get("current_temperature"))
    current_celsius = (
        _native_to_celsius(current_temperature, temperature_unit)
        if current_temperature is not None
        else None
    )
    result["current_temperature"] = (
        f"{current_celsius:.1f}"
        if current_celsius is not None and -50 <= current_celsius <= 100
        else "unknown"
    )
    # Publish mode last so an OFF snapshot wins over its remembered setpoint.
    result["hvac_mode"] = "off" if state == "off" else "heat"
    return result


def _finite_float(value: object) -> float | None:
    """Return one finite non-boolean number without leaking conversion errors."""
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return None
    try:
        normalized = float(value)
    except (OverflowError, ValueError):
        return None
    return normalized if math.isfinite(normalized) else None


def _native_to_celsius(value: float, unit: str) -> float | None:
    """Convert an HA state-machine temperature into the FRITZ Celsius unit."""
    if unit in {"°C", "C"}:
        return value
    if unit in {"°F", "F"}:
        return (value - 32) * 5 / 9
    return None


def _celsius_to_native(value: float, unit: str) -> float:
    """Convert a FRITZ Celsius setpoint into the HA state-machine unit."""
    if unit in {"°C", "C"}:
        return value
    if unit in {"°F", "F"}:
        return value * 9 / 5 + 32
    raise ProtocolError("unsupported climate temperature unit")
