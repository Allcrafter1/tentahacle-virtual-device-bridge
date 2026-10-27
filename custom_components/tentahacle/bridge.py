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
        """Refresh schedules and retry the authoritative device set."""
        async with self._sync_lock:
            if self._stopping:
                return
            for uid, mapping in self._mappings.items():
                if CONF_SCHEDULE_WORKDAYS in mapping:
                    await self._async_publish_schedule(uid, mapping)
            await self._async_publish_reconciliation()

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
            entity_capabilities: CoverCapabilities | ClimateCapabilities | None = None
            if profile is DeviceProfile.COVER:
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
                    else self.hass.config.units.temperature_unit
                )
                if not isinstance(temperature_unit, str):
                    temperature_unit = str(temperature_unit)
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
        await self.hass.services.async_call(domain, service, data, blocking=True)

    async def _async_load_overrides(self) -> None:
        """Load and validate restart-safe thermostat override state."""
        stored = await self._override_store.async_load()
        if not isinstance(stored, dict):
            return
        records = stored.get("overrides")
        if not isinstance(records, dict):
            return
        for uid, value in records.items():
            if uid not in self._mappings:
                continue
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
            if state is not None and not override_superseded_by_state(
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
                mapping = self._mappings.get(uid)
                if (
                    mapping is None
                    or mapping[CONF_SOURCE_ENTITY_ID] != override.entity_id
                ):
                    self._overrides.pop(uid, None)
                    continue
                try:
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
        payload = {
            "schema_version": PROTOCOL_VERSION,
            "request_id": str(uuid.uuid4()),
            "operation": "prune_devices",
            "keep_uids": sorted(self._mappings),
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
        feedback = feedback_for_state(
            DeviceProfile(mapping[CONF_PROFILE]),
            state,
            attributes,
            color_full=self._mapping_color_full(mapping),
            temperature_unit=(
                str(
                    attributes.get(
                        "temperature_unit", self.hass.config.units.temperature_unit
                    )
                )
                if DeviceProfile(mapping[CONF_PROFILE]) is DeviceProfile.THERMOSTAT
                else "°C"
            ),
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
