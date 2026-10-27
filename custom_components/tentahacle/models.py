# SPDX-License-Identifier: MIT OR Apache-2.0
"""Transport-independent bridge models."""

from __future__ import annotations

import hashlib
import json
import math
import re
from collections.abc import Collection, Mapping
from dataclasses import dataclass
from urllib.parse import urlsplit, urlunsplit

from .const import PROTOCOL_VERSION, DeviceProfile

_BRIDGE_ID = re.compile(r"^[a-z0-9][a-z0-9_-]{2,31}$")
_ENDPOINT_UID = re.compile(r"^TVB[0-9A-F]{16}$")
_CLIMATE_TARGET_TEMPERATURE = 1


@dataclass(frozen=True, slots=True)
class VirtualDevice:
    """Persistent mapping sent to the bridge."""

    endpoint_uid: str
    profile: DeviceProfile
    name: str
    source_entity_id: str
    enabled: bool = True
    color_full: bool = False

    def __post_init__(self) -> None:
        if not _ENDPOINT_UID.fullmatch(self.endpoint_uid):
            raise ValueError("invalid endpoint UID")
        validate_device_name(self.name)
        if "." not in self.source_entity_id:
            raise ValueError("invalid Home Assistant entity ID")
        if (
            self.color_full
            and self.profile is not DeviceProfile.COLOR_TEMPERATURE_LIGHT
        ):
            raise ValueError("full color requires the color-temperature profile")

    def management_payload(self, request_id: str) -> dict[str, object]:
        """Return a versioned idempotent upsert request."""
        device: dict[str, object] = {
            "uid": self.endpoint_uid,
            "profile": self.profile.value,
            "name": self.name,
            "enabled": self.enabled,
        }
        if self.profile is DeviceProfile.COLOR_TEMPERATURE_LIGHT:
            device["color_full"] = self.color_full
        return {
            "schema_version": PROTOCOL_VERSION,
            "request_id": request_id,
            "operation": "upsert_device",
            "device": device,
        }


def validate_device_name(value: object) -> str:
    """Match the provider's UTF-8 name and control-character constraints."""
    if not isinstance(value, str) or not value or value != value.strip():
        raise ValueError("invalid device name")
    if any(ord(char) < 32 or 127 <= ord(char) <= 159 for char in value):
        raise ValueError("device name contains control characters")
    if len(value.encode("utf-8")) > 79:
        raise ValueError("device name exceeds 79 UTF-8 bytes")
    return value


def validate_topic_root(value: str, bridge_id: str) -> str:
    """Validate a normalized topic prefix accepted by the C bridge."""
    root = value.strip("/")
    if not root or "//" in root or any(
        ord(char) <= 32 or ord(char) == 127 or char in "+#" for char in root
    ):
        raise ValueError("invalid MQTT topic root")
    if len(f"{root}/{bridge_id}".encode()) > 255:
        raise ValueError("MQTT topic prefix exceeds 255 UTF-8 bytes")
    return root


def validate_bridge_id(value: str) -> str:
    """Validate a bridge ID used as one MQTT topic component."""
    value = value.strip().lower()
    if not _BRIDGE_ID.fullmatch(value):
        raise ValueError("invalid bridge ID")
    return value


def validate_fritz_url(value: str) -> str:
    """Validate and normalize the local FRITZ!Box configuration URL."""
    value = value.strip()
    parsed = urlsplit(value)
    if (
        parsed.scheme not in {"http", "https"}
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("invalid FRITZ!Box URL")
    return urlunsplit((parsed.scheme, parsed.netloc, parsed.path.rstrip("/"), "", ""))


def fritz_smart_home_url(value: str) -> str:
    """Build the supported FRITZ!OS Smart Home deep link from a safe base URL."""
    parsed = urlsplit(validate_fritz_url(value))
    path = f"{parsed.path.rstrip('/')}/"
    return urlunsplit((parsed.scheme, parsed.netloc, path, "lp=sh_dev", ""))


def endpoint_uid_from_token(token: str) -> str:
    """Create the stable 19-character FRITZ UID from a random token."""
    digest = hashlib.sha256(token.encode("utf-8")).hexdigest()[:16].upper()
    return f"TVB{digest}"


def bridge_details_from_discovery(topic: str, payload: str | bytes) -> tuple[str, str]:
    """Validate a retained bridge announcement and return ID and topic root."""
    parts = topic.strip("/").split("/")
    if len(parts) < 4 or parts[-2:] != ["bridge", "info"]:
        raise ValueError("invalid discovery topic")
    bridge_id = validate_bridge_id(parts[-3])
    topic_root = "/".join(parts[:-3])
    if not topic_root:
        raise ValueError("empty discovery topic root")
    try:
        document = json.loads(payload)
    except (TypeError, ValueError, RecursionError) as err:
        raise ValueError("invalid discovery payload") from err
    if not isinstance(document, dict) or document.get("bridge_id") != bridge_id:
        raise ValueError("discovery identity mismatch")
    if document.get("schema_version") != PROTOCOL_VERSION:
        raise ValueError("unsupported discovery schema")
    return bridge_id, validate_topic_root(topic_root, bridge_id)


def profile_for_domain(
    domain: str, *, supported_color_modes: Collection[str] | None = None
) -> DeviceProfile:
    """Return the default virtual profile for a Home Assistant domain."""
    if domain in {"switch", "input_boolean"}:
        return DeviceProfile.SWITCH
    if domain == "light":
        modes = set(supported_color_modes or ())
        if modes == {"onoff"}:
            return DeviceProfile.SWITCH
        if modes & {"color_temp", "hs", "xy", "rgb", "rgbw", "rgbww"}:
            return DeviceProfile.COLOR_TEMPERATURE_LIGHT
        return DeviceProfile.DIMMABLE_LIGHT
    if domain == "cover":
        return DeviceProfile.COVER
    if domain == "climate":
        return DeviceProfile.THERMOSTAT
    raise ValueError(f"unsupported source domain: {domain}")


def full_color_supported(supported_color_modes: Collection[str] | None) -> bool:
    """Return whether Home Assistant can represent chromatic colour."""
    return bool(
        set(supported_color_modes or ()) & {"hs", "xy", "rgb", "rgbw", "rgbww"}
    )


def climate_entity_supported(
    attributes: Mapping[str, object], *, fallback_temperature_unit: str = "°C"
) -> bool:
    """Return whether a climate entity can faithfully implement the FRITZ profile."""
    features = attributes.get("supported_features")
    if (
        not isinstance(features, int)
        or isinstance(features, bool)
        or not features & _CLIMATE_TARGET_TEMPERATURE
    ):
        return False
    modes = attributes.get("hvac_modes")
    if (
        not isinstance(modes, Collection)
        or isinstance(modes, (str, bytes))
        or not {"heat", "off"}.issubset(modes)
    ):
        return False
    unit = attributes.get("temperature_unit", fallback_temperature_unit)
    if not isinstance(unit, str):
        return False
    minimum = _temperature_to_celsius(attributes.get("min_temp"), unit)
    maximum = _temperature_to_celsius(attributes.get("max_temp"), unit)
    if minimum is None or maximum is None or minimum > 8 or maximum < 28:
        return False
    step = attributes.get("target_temp_step")
    if step is None:
        return True
    normalized_step = _finite_float(step)
    if normalized_step is None or normalized_step <= 0 or normalized_step > 0.5:
        return False
    return math.isclose(0.5 / normalized_step, round(0.5 / normalized_step))


def profiles_for_entity(
    domain: str, supported_color_modes: Collection[str] | None = None
) -> list[str]:
    """Return compatible virtual profiles for one HA entity domain."""
    if domain in {"switch", "input_boolean"}:
        return [DeviceProfile.SWITCH.value]
    if domain == "light":
        modes = set(supported_color_modes or ())
        profiles = [DeviceProfile.SWITCH.value]
        if modes != {"onoff"}:
            profiles.append(DeviceProfile.DIMMABLE_LIGHT.value)
        if modes & {"color_temp", "hs", "xy", "rgb", "rgbw", "rgbww"}:
            profiles.append(DeviceProfile.COLOR_TEMPERATURE_LIGHT.value)
        return profiles
    if domain == "cover":
        return [DeviceProfile.COVER.value]
    if domain == "climate":
        return [DeviceProfile.THERMOSTAT.value]
    return []


def _finite_float(value: object) -> float | None:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return None
    try:
        normalized = float(value)
    except (OverflowError, ValueError):
        return None
    return normalized if math.isfinite(normalized) else None


def _temperature_to_celsius(value: object, unit: str) -> float | None:
    normalized = _finite_float(value)
    if normalized is None:
        return None
    if unit in {"°C", "C"}:
        return normalized
    if unit in {"°F", "F"}:
        return (normalized - 32) * 5 / 9
    return None
