# SPDX-License-Identifier: MIT OR Apache-2.0
"""MQTT runtime for TentAHAcle - Virtual Device Bridge."""

from __future__ import annotations

import json
import logging
import math
import uuid
from asyncio import Lock
from collections.abc import Callable
from datetime import datetime, timedelta
from time import monotonic
from typing import TYPE_CHECKING, Any

from homeassistant.components import mqtt
from homeassistant.components.cover import CoverEntityFeature
from homeassistant.core import Event, EventStateChangedData, HomeAssistant, callback
from homeassistant.exceptions import ConfigEntryError
from homeassistant.helpers.event import (
    async_call_later,
    async_track_state_change_event,
    async_track_time_interval,
)
from homeassistant.helpers.storage import Store
from homeassistant.util import dt as dt_util

from .const import (
    CONF_BRIDGE_ID,
    CONF_COLOR_FULL,
    CONF_ENDPOINT_UID,
    CONF_FRITZ_NAME,
    CONF_PROFILE,
    CONF_SCHEDULE_HOLIDAYS,
    CONF_SCHEDULE_WEEK_PROFILE,
    CONF_SCHEDULE_WORKDAYS,
    CONF_SOURCE_ENTITY_ID,
    CONF_TOPIC_ROOT,
    DOMAIN,
    MAX_VIRTUAL_DEVICES,
    PROTOCOL_VERSION,
    DeviceProfile,
)
from .models import full_color_supported
from .protocol import (
    ClimateCapabilities,
    CoverCapabilities,
    LightCapabilities,
    ProtocolError,
    feedback_for_state,
    parse_command,
    service_call_for_command,
)
from .schedule import schedule_shadow_payload
from .thermostat import (
    ThermostatOverride,
    override_superseded_by_state,
    timer_request,
)

if TYPE_CHECKING:
    from . import TentAHAcleConfigEntry

_LOGGER = logging.getLogger(__name__)


class TentAHAcleBridge:
    """Bind config subentries to a bridge over Home Assistant MQTT."""

    def __init__(
        self, hass: HomeAssistant, entry: TentAHAcleConfigEntry
    ) -> None:
        self.hass = hass
        self.entry = entry
        self._unsubscribers: list[Callable[[], None]] = []
        self._override_timers: dict[str, Callable[[], None]] = {}
        self._overrides: dict[str, ThermostatOverride] = {}
        self._stopping = False
        self._override_lock = Lock()
        self._sync_lock = Lock()
        self._override_store: Store[dict[str, object]] = Store(
            hass,
            1,
            f"{DOMAIN}.{entry.entry_id}.thermostat_overrides",
            private=True,
            atomic_writes=True,
        )
        root = entry.data[CONF_TOPIC_ROOT].strip("/")
        self.topic_prefix = f"{root}/{entry.data[CONF_BRIDGE_ID]}"
        self._mappings = {
            subentry.data[CONF_ENDPOINT_UID]: subentry.data
            for subentry in entry.subentries.values()
            if subentry.subentry_type == "device"
        }

    async def async_start(self) -> None:
        """Subscribe to commands and start state feedback."""
        if len(self._mappings) > MAX_VIRTUAL_DEVICES:
            raise ConfigEntryError(
                f"Bridge has {len(self._mappings)} mappings; maximum is "
                f"{MAX_VIRTUAL_DEVICES}"
            )
        await mqtt.async_wait_for_mqtt_client(self.hass)
        await self._async_load_overrides()
        self._unsubscribers.append(
            await mqtt.async_subscribe(
                self.hass,
                f"{self.topic_prefix}/device/+/command",
                self._async_command_message,
                qos=1,
            )
        )
        self._unsubscribers.append(
            await mqtt.async_subscribe(
                self.hass,
                f"{self.topic_prefix}/bridge/availability",
                self._async_availability_message,
                qos=1,
            )
        )
        for uid, mapping in self._mappings.items():
            entity_id = mapping[CONF_SOURCE_ENTITY_ID]
            self._unsubscribers.append(
                async_track_state_change_event(
                    self.hass,
                    [entity_id],
                    self._state_listener(uid, mapping),
                )
            )
            await self._async_start_schedule(uid, mapping)
        self._unsubscribers.append(
            async_track_time_interval(
                self.hass, self._async_schedule_tick, timedelta(minutes=1)
            )
        )
        await self._async_resume_overrides()
        await self._async_sync_bridge()

    async def async_stop(self) -> None:
        """Remove every MQTT and state subscription."""
        # Close admission before waiting for an in-flight override operation.
        self._stopping = True
        while self._unsubscribers:
            self._unsubscribers.pop()()
        async with self._override_lock:
            while self._override_timers:
                self._override_timers.popitem()[1]()
        # Drain old management writes before a replacement runtime can start.
        async with self._sync_lock:
            pass

    async def async_remove(self) -> None:
        """Restore active timers and remove all native devices with this HA hub."""
        # This is a fresh cleanup runtime created after the ordinary unload.
        # Do not leave orphan retry callbacks once the config entry is gone.
        self._stopping = True
        await self._async_load_overrides()
        failures: list[str] = []
        for uid in list(self._overrides):
            async with self._override_lock:
                try:
                    await self._async_finish_override(uid)
                except Exception:
                    failures.append(uid)
                    _LOGGER.exception(
                        "Unable to restore thermostat override while removing %s", uid
                    )
        try:
            await self._async_publish_prune([])
        except Exception:
            failures.append("native registry")
            _LOGGER.exception("Unable to prune native devices while removing bridge")
        if failures:
            raise RuntimeError(
                "Bridge removal cleanup failed for: " + ", ".join(failures)
            )
        await self._override_store.async_remove()

    @callback
    def _state_listener(self, uid: str, mapping: dict[str, Any]):
        async def listener(event: Event[EventStateChangedData]) -> None:
            new_state = event.data["new_state"]
            if new_state is not None:
                await self._async_publish_feedback(
                    uid, mapping, new_state.state, new_state.attributes
                )
                if DeviceProfile(mapping[CONF_PROFILE]) is DeviceProfile.THERMOSTAT:
                    await self._async_cancel_override_if_source_changed(uid)
                    await self._async_publish_schedule(uid, mapping)
            elif DeviceProfile(mapping[CONF_PROFILE]) is DeviceProfile.THERMOSTAT:
                await self._async_publish_feedback(uid, mapping, "unavailable", {})
                await self._async_cancel_override_if_source_changed(uid)
                await self._async_publish_schedule(uid, mapping)

        return listener

    async def _async_availability_message(self, message: mqtt.ReceiveMessage) -> None:
        """Replay desired state whenever the bridge reconnects to MQTT."""
        payload = (
            message.payload.decode()
            if isinstance(message.payload, bytes)
            else message.payload
        )
        if payload.strip().lower() == "online":
            await self._async_sync_bridge()

    async def _async_sync_bridge(self) -> None:
        """Reconcile all mappings and retained state in a stable order."""
        async with self._sync_lock:
            if self._stopping:
                return
            # Free stale native slots first, so a replacement can be inserted
            # even when the persistent provider registry was previously full.
            await self._async_publish_reconciliation()
            for uid, mapping in self._mappings.items():
                await self._async_publish_management(uid, mapping)
                state = self.hass.states.get(mapping[CONF_SOURCE_ENTITY_ID])
                if state is not None:
                    await self._async_publish_feedback(
                        uid, mapping, state.state, state.attributes
                    )
                elif DeviceProfile(mapping[CONF_PROFILE]) is DeviceProfile.THERMOSTAT:
                    await self._async_publish_feedback(
                        uid, mapping, "unavailable", {}
                    )
                await self._async_publish_schedule(uid, mapping)
                await self._async_publish_authoritative_timer(uid, mapping)
            await self._async_publish_reconciliation()

    async def _async_start_schedule(self, uid: str, mapping: dict[str, Any]) -> None:
        """Subscribe to optional schedule adapter entities and publish once."""
        schedule_entities = [
            mapping.get(CONF_SCHEDULE_WEEK_PROFILE),
            mapping.get(CONF_SCHEDULE_WORKDAYS),
            mapping.get(CONF_SCHEDULE_HOLIDAYS),
        ]
        configured = [entity_id for entity_id in schedule_entities if entity_id]
        if len(configured) != 3:
            return

        async def listener(event: Event[EventStateChangedData]) -> None:
            await self._async_publish_schedule(uid, mapping)

        self._unsubscribers.append(
            async_track_state_change_event(self.hass, configured, listener)
        )

    async def _async_schedule_tick(self, _now: datetime) -> None:
        """Retry the complete authoritative device and state snapshot."""
        await self._async_sync_bridge()

    async def _async_publish_authoritative_timer(
        self, uid: str, mapping: dict[str, Any]
    ) -> None:
        """Retry the retained thermostat timer state after transient failures."""
        if DeviceProfile(mapping[CONF_PROFILE]) is not DeviceProfile.THERMOSTAT:
            return
        async with self._override_lock:
            override = self._overrides.get(uid)
            if override is None:
                await self._async_publish_timer(uid, "cancel", 0)
            else:
                await self._async_publish_timer(
                    uid, override.action, override.end_time
                )

    async def _async_publish_schedule(self, uid: str, mapping: dict[str, Any]) -> None:
        """Publish the Home Assistant owned 5+2 schedule shadow."""
        if DeviceProfile(mapping[CONF_PROFILE]) is not DeviceProfile.THERMOSTAT:
            return
        climate = self.hass.states.get(mapping[CONF_SOURCE_ENTITY_ID])
        week = self.hass.states.get(mapping.get(CONF_SCHEDULE_WEEK_PROFILE, ""))
        workdays = self.hass.states.get(mapping.get(CONF_SCHEDULE_WORKDAYS, ""))
        holidays = self.hass.states.get(mapping.get(CONF_SCHEDULE_HOLIDAYS, ""))
        payload = "disabled"
        if (
            climate is not None
            and week is not None
            and workdays is not None
            and holidays is not None
        ):
            enabled = (
                climate.attributes.get("preset_mode") == "schedule"
                and week.state == "5+2"
                and workdays.state not in {"", "unknown", "unavailable"}
                and holidays.state not in {"", "unknown", "unavailable"}
            )
            try:
                payload = schedule_shadow_payload(
                    dt_util.now(), workdays.state, holidays.state, enabled=enabled
                )
            except ProtocolError as err:
                _LOGGER.warning(
                    "Disabling invalid schedule shadow for %s: %s", uid, err
                )
        await mqtt.async_publish(
            self.hass,
            f"{self.topic_prefix}/device/{uid}/schedule/set",
            payload,
            qos=1,
            retain=True,
        )

    async def _async_command_message(self, message: mqtt.ReceiveMessage) -> None:
        """Execute one command received from FRITZ!OS."""
        topic_parts = message.topic.split("/")
        if len(topic_parts) < 3:
            return
        uid = topic_parts[-2]
        mapping = self._mappings.get(uid)
        if mapping is None:
            _LOGGER.warning("Ignoring command for unknown endpoint %s", uid)
            return
        try:
            command = parse_command(message.payload, uid)
            profile = DeviceProfile(mapping[CONF_PROFILE])
            if (
                profile is DeviceProfile.THERMOSTAT
                and "timer_action" in command.values
            ):
                await self._async_handle_timer(uid, mapping, command.values)
                return
            entity_capabilities: (
                LightCapabilities | CoverCapabilities | ClimateCapabilities | None
            ) = None
            if profile in {
                DeviceProfile.DIMMABLE_LIGHT,
                DeviceProfile.COLOR_TEMPERATURE_LIGHT,
            }:
                source_state = self.hass.states.get(mapping[CONF_SOURCE_ENTITY_ID])
                attributes = (
                    source_state.attributes if source_state is not None else {}
                )
                entity_capabilities = _light_capabilities(attributes)
            elif profile is DeviceProfile.COVER:
                source_state = self.hass.states.get(mapping[CONF_SOURCE_ENTITY_ID])
                if source_state is not None:
                    features = source_state.attributes.get("supported_features")
                    if isinstance(features, int) and not isinstance(features, bool):
                        entity_capabilities = CoverCapabilities(
                            open=bool(features & CoverEntityFeature.OPEN),
                            close=bool(features & CoverEntityFeature.CLOSE),
                            stop=bool(features & CoverEntityFeature.STOP),
                            set_position=bool(
                                features & CoverEntityFeature.SET_POSITION
                            ),
                        )
            elif profile is DeviceProfile.THERMOSTAT:
                source_state = self.hass.states.get(mapping[CONF_SOURCE_ENTITY_ID])
                maximum = (
                    source_state.attributes.get("max_temp")
                    if source_state is not None
                    else None
                )
                temperature_unit = (
                    source_state.attributes.get("temperature_unit")
                    if source_state is not None
                    else None
                )
                if not isinstance(temperature_unit, str):
                    temperature_unit = str(self.hass.config.units.temperature_unit)
                normalized_maximum = None
                if isinstance(maximum, (int, float)) and not isinstance(
                    maximum, bool
                ):
                    try:
                        normalized_maximum = float(maximum)
                    except OverflowError:
                        normalized_maximum = math.nan
                    if not math.isfinite(normalized_maximum):
                        normalized_maximum = None
                entity_capabilities = ClimateCapabilities(
                    normalized_maximum, temperature_unit
                )
            domain, service, data = service_call_for_command(
                profile,
                mapping[CONF_SOURCE_ENTITY_ID],
                command,
                entity_capabilities=entity_capabilities,
                color_full=self._mapping_color_full(mapping),
            )
        except ProtocolError as err:
            _LOGGER.warning("Ignoring invalid command for %s: %s", uid, err)
            return
        _LOGGER.debug(
            "Command uid=%s sequence=%s entity=%s values=%s -> %s.%s data=%s",
            uid,
            command.sequence,
            mapping[CONF_SOURCE_ENTITY_ID],
            _diagnostic_values(command.values),
            domain,
            service,
            _diagnostic_values(data),
        )
        started = monotonic()
        try:
            await self.hass.services.async_call(
                domain, service, data, blocking=True
            )
        except Exception:
            _LOGGER.exception(
                "Command uid=%s sequence=%s service failed after %.1f ms",
                uid,
                command.sequence,
                (monotonic() - started) * 1000,
            )
            raise
        _LOGGER.debug(
            "Command uid=%s sequence=%s service completed in %.1f ms",
            uid,
            command.sequence,
            (monotonic() - started) * 1000,
        )

    async def _async_load_overrides(self) -> None:
        """Load and validate restart-safe thermostat override state."""
        stored = await self._override_store.async_load()
        if not isinstance(stored, dict):
            return
        records = stored.get("overrides")
        if not isinstance(records, dict):
            return
        for uid, value in records.items():
            try:
                self._overrides[uid] = ThermostatOverride.from_json(value)
            except ProtocolError as err:
                _LOGGER.warning("Ignoring invalid stored override for %s: %s", uid, err)

    async def _async_save_overrides(self) -> None:
        """Persist every active override as one consistent snapshot."""
        await self._override_store.async_save(
            {
                "overrides": {
                    uid: override.as_json() for uid, override in self._overrides.items()
                }
            }
        )

    async def _async_cancel_override_if_source_changed(self, uid: str) -> None:
        """Let an explicit later HA/source change supersede a running timer."""
        if uid not in self._overrides:
            return
        async with self._override_lock:
            override = self._overrides.get(uid)
            if override is None:
                return
            state = self.hass.states.get(override.entity_id)
            if state is None:
                # Entity removal/reload is not an explicit user command. Keep
                # the snapshot so it can still be restored after recovery.
                return
            if not override_superseded_by_state(
                override, state.state, state.attributes
            ):
                return
            timer = self._override_timers.pop(uid, None)
            if timer is not None:
                timer()
            self._overrides.pop(uid, None)
            try:
                await self._async_save_overrides()
            except Exception:
                self._overrides[uid] = override
                retry_at = max(
                    override.end_time, int(dt_util.utcnow().timestamp()) + 60
                )
                self._schedule_override(uid, retry_at)
                raise
            await self._async_publish_timer(uid, "cancel", 0)

    async def _async_resume_overrides(self) -> None:
        """Resume valid overrides or restore expired ones after restart."""
        for uid in list(self._overrides):
            async with self._override_lock:
                if self._stopping:
                    return
                # Commands or another expiry may have changed this while we waited.
                override = self._overrides.get(uid)
                if override is None:
                    continue
                try:
                    mapping = self._mappings.get(uid)
                    if (
                        mapping is None
                        or DeviceProfile(mapping[CONF_PROFILE])
                        is not DeviceProfile.THERMOSTAT
                        or mapping[CONF_SOURCE_ENTITY_ID] != override.entity_id
                    ):
                        # A deleted or rebound mapping must not leave its previous
                        # real climate entity stranded at Boost/MAX or cold/OFF.
                        await self._async_finish_override(uid)
                        continue
                    if override.end_time <= int(dt_util.utcnow().timestamp()):
                        await self._async_finish_override(uid)
                    else:
                        self._schedule_override(uid, override.end_time)
                        await self._async_apply_override(mapping, override)
                        await self._async_publish_timer(
                            uid, override.action, override.end_time
                        )
                except Exception:
                    # One unavailable endpoint must not strand other timers.
                    _LOGGER.exception(
                        "Unable to resume thermostat override for %s", uid
                    )
        async with self._override_lock:
            await self._async_save_overrides()

    async def _async_handle_timer(
        self, uid: str, mapping: dict[str, Any], values: dict[str, Any]
    ) -> None:
        """Apply, cancel or extend a 440 thermostat timer."""
        async with self._override_lock:
            if self._stopping:
                return
            action, end_time = timer_request(values, int(dt_util.utcnow().timestamp()))
            if action == "cancel":
                await self._async_finish_override(uid)
                return
            previous = self._overrides.get(uid)
            existing = previous
            if existing is None:
                state = self.hass.states.get(mapping[CONF_SOURCE_ENTITY_ID])
                if state is None:
                    raise ProtocolError("thermostat state is unavailable")
                temperature = state.attributes.get("temperature")
                normalized_temperature = math.nan
                if isinstance(temperature, (int, float)) and not isinstance(
                    temperature, bool
                ):
                    try:
                        normalized_temperature = float(temperature)
                    except OverflowError:
                        normalized_temperature = math.nan
                if (
                    state.state in {"unknown", "unavailable"}
                    or not math.isfinite(normalized_temperature)
                    or not -100 <= normalized_temperature <= 300
                ):
                    raise ProtocolError("thermostat state cannot be saved")
                existing = ThermostatOverride(
                    action=action,
                    end_time=end_time,
                    entity_id=mapping[CONF_SOURCE_ENTITY_ID],
                    previous_hvac_mode=state.state,
                    previous_temperature=normalized_temperature,
                    previous_preset_mode=(
                        state.attributes.get("preset_mode")
                        if isinstance(state.attributes.get("preset_mode"), str)
                        else None
                    ),
                )
            override = ThermostatOverride(
                action=action,
                end_time=end_time,
                entity_id=existing.entity_id,
                previous_hvac_mode=existing.previous_hvac_mode,
                previous_temperature=existing.previous_temperature,
                previous_preset_mode=existing.previous_preset_mode,
            )
            self._overrides[uid] = override
            try:
                await self._async_save_overrides()
            except Exception:
                if previous is None:
                    self._overrides.pop(uid, None)
                else:
                    self._overrides[uid] = previous
                raise
            # Expiry must survive failures in the action or MQTT confirmation.
            self._schedule_override(uid, end_time)
            await self._async_apply_override(mapping, override)
            await self._async_publish_timer(uid, action, end_time)

    async def _async_apply_override(
        self,
        mapping: dict[str, Any],
        override: ThermostatOverride,
    ) -> None:
        """Apply the generic Boost or cold behavior to the source climate."""
        entity_id = mapping[CONF_SOURCE_ENTITY_ID]
        if override.action == "cold":
            await self.hass.services.async_call(
                "climate",
                "set_hvac_mode",
                {"entity_id": entity_id, "hvac_mode": "off"},
                blocking=True,
            )
            return
        state = self.hass.states.get(entity_id)
        maximum = state.attributes.get("max_temp", 28) if state else 28
        if not isinstance(maximum, (int, float)) or isinstance(maximum, bool):
            maximum = 28
        try:
            maximum = float(maximum)
        except OverflowError:
            maximum = 28.0
        if not math.isfinite(maximum):
            maximum = 28.0
        await self.hass.services.async_call(
            "climate",
            "set_temperature",
            {
                "entity_id": entity_id,
                "temperature": float(maximum),
                "hvac_mode": "heat",
            },
            blocking=True,
        )

    async def _async_finish_override(self, uid: str) -> None:
        """Restore the state captured before the first timed override."""
        override = self._overrides.get(uid)
        timer = self._override_timers.pop(uid, None)
        if timer is not None:
            timer()
        if override is None:
            await self._async_publish_timer(uid, "cancel", 0)
            return
        try:
            await self.hass.services.async_call(
                "climate",
                "set_temperature",
                {
                    "entity_id": override.entity_id,
                    "temperature": override.previous_temperature,
                },
                blocking=True,
            )
            await self.hass.services.async_call(
                "climate",
                "set_hvac_mode",
                {
                    "entity_id": override.entity_id,
                    "hvac_mode": override.previous_hvac_mode,
                },
                blocking=True,
            )
            if override.previous_preset_mode:
                await self.hass.services.async_call(
                    "climate",
                    "set_preset_mode",
                    {
                        "entity_id": override.entity_id,
                        "preset_mode": override.previous_preset_mode,
                    },
                    blocking=True,
                )
        except Exception:
            # Keep the saved state and retry a transient climate-service failure.
            self._schedule_override(uid, int(dt_util.utcnow().timestamp()) + 60)
            raise
        self._overrides.pop(uid, None)
        await self._async_save_overrides()
        await self._async_publish_timer(uid, "cancel", 0)

    def _schedule_override(self, uid: str, end_time: int) -> None:
        """Schedule one exact override expiry in Home Assistant's event loop."""
        if self._stopping:
            return
        previous = self._override_timers.pop(uid, None)
        if previous is not None:
            previous()

        async def finish(_now: datetime) -> None:
            async with self._override_lock:
                if self._stopping or self._override_timers.get(uid) is not cancel:
                    return
                await self._async_finish_override(uid)

        delay = max(1, end_time - int(dt_util.utcnow().timestamp()))
        cancel = async_call_later(self.hass, delay, finish)
        self._override_timers[uid] = cancel

    async def _async_publish_timer(self, uid: str, action: str, end_time: int) -> None:
        """Confirm the canonical timer state back to the provider."""
        payload = "cancel" if action == "cancel" else f"{action} {end_time}"
        await mqtt.async_publish(
            self.hass,
            f"{self.topic_prefix}/device/{uid}/timer/set",
            payload,
            qos=1,
            retain=True,
        )

    async def _async_publish_management(
        self, uid: str, mapping: dict[str, Any]
    ) -> None:
        payload = {
            "schema_version": PROTOCOL_VERSION,
            "request_id": str(uuid.uuid4()),
            "operation": "upsert_device",
            "device": {
                "uid": uid,
                "profile": mapping[CONF_PROFILE],
                "name": mapping[CONF_FRITZ_NAME],
                "enabled": True,
            },
        }
        if mapping[CONF_PROFILE] == DeviceProfile.COLOR_TEMPERATURE_LIGHT.value:
            payload["device"]["color_full"] = self._mapping_color_full(  # type: ignore[index]
                mapping
            )
        await mqtt.async_publish(
            self.hass,
            f"{self.topic_prefix}/management/request",
            json.dumps(payload, separators=(",", ":")),
            qos=1,
            retain=False,
        )

    async def _async_publish_reconciliation(self) -> None:
        """Permanently remove bridge devices no longer mapped by this entry."""
        await self._async_publish_prune(sorted(self._mappings))

    async def _async_publish_prune(self, keep_uids: list[str]) -> None:
        """Publish one authoritative native-device set."""
        payload = {
            "schema_version": PROTOCOL_VERSION,
            "request_id": str(uuid.uuid4()),
            "operation": "prune_devices",
            "keep_uids": keep_uids,
        }
        await mqtt.async_publish(
            self.hass,
            f"{self.topic_prefix}/management/request",
            json.dumps(payload, separators=(",", ":")),
            qos=1,
            retain=False,
        )

    async def _async_publish_feedback(
        self,
        uid: str,
        mapping: dict[str, Any],
        state: str,
        attributes: dict[str, Any],
    ) -> None:
        profile = DeviceProfile(mapping[CONF_PROFILE])
        light_capabilities = None
        if profile in {
            DeviceProfile.DIMMABLE_LIGHT,
            DeviceProfile.COLOR_TEMPERATURE_LIGHT,
        }:
            light_capabilities = _light_capabilities(attributes)
        feedback = feedback_for_state(
            profile,
            state,
            attributes,
            color_full=self._mapping_color_full(mapping),
            light_capabilities=light_capabilities,
            temperature_unit=(
                (
                    attributes.get("temperature_unit")
                    if isinstance(attributes.get("temperature_unit"), str)
                    else str(self.hass.config.units.temperature_unit)
                )
                if DeviceProfile(mapping[CONF_PROFILE]) is DeviceProfile.THERMOSTAT
                else "°C"
            ),
        )
        _LOGGER.debug(
            "State feedback uid=%s entity=%s state=%s attributes=%s properties=%s",
            uid,
            mapping[CONF_SOURCE_ENTITY_ID],
            state,
            _diagnostic_values(attributes),
            feedback,
        )
        for property_name, value in feedback.items():
            await mqtt.async_publish(
                self.hass,
                f"{self.topic_prefix}/device/{uid}/{property_name}/set",
                "" if value is None else value,
                qos=1,
                retain=True,
            )

    def _mapping_color_full(self, mapping: dict[str, Any]) -> bool:
        """Return stored capability or infer it for pre-upgrade subentries."""
        if mapping.get(CONF_PROFILE) != DeviceProfile.COLOR_TEMPERATURE_LIGHT.value:
            return False
        if CONF_COLOR_FULL in mapping:
            return bool(mapping[CONF_COLOR_FULL])
        state = self.hass.states.get(mapping[CONF_SOURCE_ENTITY_ID])
        return state is not None and full_color_supported(
            state.attributes.get("supported_color_modes", [])
        )


def _diagnostic_values(values: dict[str, Any]) -> dict[str, Any]:
    """Return only protocol and light-state fields safe for debug logs."""
    allowed = (
        "brightness",
        "brightness_pct",
        "color_mode",
        "color_temp_kelvin",
        "color_temperature",
        "current_position",
        "entity_id",
        "hs_color",
        "hue",
        "level",
        "max_color_temp_kelvin",
        "min_color_temp_kelvin",
        "position",
        "saturation",
        "state",
        "supported_color_modes",
    )
    return {key: values[key] for key in allowed if key in values}


def _light_capabilities(attributes: dict[str, Any]) -> LightCapabilities:
    """Read only well-formed light limits and modes from an HA state."""
    modes = attributes.get("supported_color_modes")
    supported_modes = (
        frozenset(str(mode) for mode in modes)
        if isinstance(modes, (list, tuple, set, frozenset))
        else frozenset()
    )
    return LightCapabilities(
        supported_color_modes=supported_modes,
        min_color_temperature=_finite_number(
            attributes.get("min_color_temp_kelvin")
        ),
        max_color_temperature=_finite_number(
            attributes.get("max_color_temp_kelvin")
        ),
    )


def _finite_number(value: object) -> float | None:
    """Normalize one HA numeric attribute without accepting booleans."""
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return None
    try:
        normalized = float(value)
    except (OverflowError, ValueError):
        return None
    return normalized if math.isfinite(normalized) else None
