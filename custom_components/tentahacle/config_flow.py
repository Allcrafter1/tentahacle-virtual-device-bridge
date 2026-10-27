# SPDX-License-Identifier: MIT OR Apache-2.0
"""Config flow for TentAHAcle - Virtual Device Bridge."""

from __future__ import annotations

import secrets
from typing import Any, override

import probatio
from homeassistant.components import persistent_notification
from homeassistant.config_entries import (
    ConfigEntry,
    ConfigFlow,
    ConfigFlowResult,
    ConfigSubentryFlow,
    SubentryFlowResult,
)
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.selector import (
    EntitySelector,
    EntitySelectorConfig,
    SelectSelector,
    SelectSelectorConfig,
    TextSelector,
    TextSelectorConfig,
)
from homeassistant.helpers.service_info.mqtt import MqttServiceInfo
from homeassistant.helpers.translation import async_get_translations

from .const import (
    CONF_BRIDGE_ID,
    CONF_COLOR_FULL,
    CONF_ENDPOINT_UID,
    CONF_FRITZ_NAME,
    CONF_FRITZ_URL,
    CONF_PROFILE,
    CONF_SCHEDULE_HOLIDAYS,
    CONF_SCHEDULE_WEEK_PROFILE,
    CONF_SCHEDULE_WORKDAYS,
    CONF_SOURCE_ENTITY_ID,
    CONF_TOPIC_ROOT,
    DEFAULT_FRITZ_URL,
    DEFAULT_TOPIC_ROOT,
    DOMAIN,
    MAX_VIRTUAL_DEVICES,
    DeviceProfile,
)
from .models import (
    bridge_details_from_discovery,
    endpoint_uid_from_token,
    full_color_supported,
    profile_for_domain,
    profiles_for_entity,
    validate_bridge_id,
    validate_device_name,
    validate_fritz_url,
    validate_topic_root,
)

SUBENTRY_TYPE_DEVICE = "device"
_NOTIFICATION_ID_SUFFIX = "workflow"


class TentAHAcleConfigFlow(ConfigFlow, domain=DOMAIN):
    """Configure one MQTT bridge."""

    VERSION = 1

    @override
    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Create one bridge entry."""
        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                bridge_id = validate_bridge_id(user_input[CONF_BRIDGE_ID])
            except ValueError:
                errors[CONF_BRIDGE_ID] = "invalid_bridge_id"
            try:
                fritz_url = validate_fritz_url(user_input[CONF_FRITZ_URL])
            except ValueError:
                errors[CONF_FRITZ_URL] = "invalid_fritz_url"
            try:
                topic_root = validate_topic_root(
                    user_input[CONF_TOPIC_ROOT], user_input[CONF_BRIDGE_ID].strip()
                )
            except ValueError:
                errors[CONF_TOPIC_ROOT] = "invalid_topic_root"
            if not errors:
                await self.async_set_unique_id(bridge_id)
                self._abort_if_unique_id_configured()
                result = self.async_create_entry(
                    title=bridge_id,
                    data={
                        CONF_BRIDGE_ID: bridge_id,
                        CONF_TOPIC_ROOT: topic_root,
                        CONF_FRITZ_URL: fritz_url,
                    },
                )
                await _async_notify_bridge_ready(
                    self.hass,
                    bridge_id=bridge_id,
                    fritz_url=fritz_url,
                )
                return result

        schema = probatio.Schema(
            {
                probatio.Required(CONF_BRIDGE_ID): TextSelector(
                    TextSelectorConfig(type="text")
                ),
                probatio.Required(
                    CONF_TOPIC_ROOT, default=DEFAULT_TOPIC_ROOT
                ): TextSelector(TextSelectorConfig(type="text")),
                probatio.Required(
                    CONF_FRITZ_URL, default=DEFAULT_FRITZ_URL
                ): TextSelector(TextSelectorConfig(type="url")),
            }
        )
        return self.async_show_form(step_id="user", data_schema=schema, errors=errors)

    @override
    async def async_step_mqtt(
        self, discovery_info: MqttServiceInfo
    ) -> ConfigFlowResult:
        """Handle a retained bridge announcement from MQTT."""
        try:
            bridge_id, topic_root = bridge_details_from_discovery(
                discovery_info.topic, discovery_info.payload
            )
        except ValueError:
            return self.async_abort(reason="invalid_discovery")
        await self.async_set_unique_id(bridge_id)
        self._abort_if_unique_id_configured()
        result = self.async_create_entry(
            title=bridge_id,
            data={
                CONF_BRIDGE_ID: bridge_id,
                CONF_TOPIC_ROOT: topic_root,
                CONF_FRITZ_URL: DEFAULT_FRITZ_URL,
            },
        )
        await _async_notify_bridge_ready(
            self.hass,
            bridge_id=bridge_id,
            fritz_url=DEFAULT_FRITZ_URL,
        )
        return result

    @override
    async def async_step_reconfigure(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        """Change the topic root or FRITZ!Box configuration link."""
        entry = self._get_reconfigure_entry()
        errors: dict[str, str] = {}
        if user_input is not None:
            try:
                fritz_url = validate_fritz_url(user_input[CONF_FRITZ_URL])
            except ValueError:
                errors[CONF_FRITZ_URL] = "invalid_fritz_url"
            try:
                topic_root = validate_topic_root(
                    user_input[CONF_TOPIC_ROOT], entry.data[CONF_BRIDGE_ID]
                )
            except ValueError:
                errors[CONF_TOPIC_ROOT] = "invalid_topic_root"
            if not errors:
                await self.async_set_unique_id(entry.unique_id)
                self._abort_if_unique_id_mismatch()
                return self.async_update_and_abort(
                    entry,
                    data_updates={
                        CONF_TOPIC_ROOT: topic_root,
                        CONF_FRITZ_URL: fritz_url,
                    },
                )
        return self.async_show_form(
            step_id="reconfigure",
            data_schema=probatio.Schema(
                {
                    probatio.Required(
                        CONF_TOPIC_ROOT,
                        default=entry.data.get(CONF_TOPIC_ROOT, DEFAULT_TOPIC_ROOT),
                    ): TextSelector(TextSelectorConfig(type="text")),
                    probatio.Required(
                        CONF_FRITZ_URL,
                        default=entry.data.get(CONF_FRITZ_URL, DEFAULT_FRITZ_URL),
                    ): TextSelector(TextSelectorConfig(type="url")),
                }
            ),
            errors=errors,
        )

    @classmethod
    @callback
    @override
    def async_get_supported_subentry_types(
        cls, config_entry: ConfigEntry
    ) -> dict[str, type[ConfigSubentryFlow]]:
        """Return virtual-device subentry support."""
        return {SUBENTRY_TYPE_DEVICE: VirtualDeviceSubentryFlow}


class VirtualDeviceSubentryFlow(ConfigSubentryFlow):
    """Add or rebind one persistent virtual device."""

    _source_entity_id: str | None = None

    @override
    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> SubentryFlowResult:
        """Choose the Home Assistant entity to expose."""
        if self._device_count() >= MAX_VIRTUAL_DEVICES:
            return self.async_abort(reason="device_limit_reached")
        errors: dict[str, str] = {}
        if user_input is not None:
            entity_id = user_input[CONF_SOURCE_ENTITY_ID]
            if self._source_in_use(entity_id):
                errors[CONF_SOURCE_ENTITY_ID] = "already_mapped"
            else:
                self._source_entity_id = entity_id
                return await self.async_step_details()
        return self.async_show_form(
            step_id="user",
            data_schema=probatio.Schema(
                {
                    probatio.Required(CONF_SOURCE_ENTITY_ID): EntitySelector(
                        EntitySelectorConfig(
                            domain=[
                                "switch",
                                "input_boolean",
                                "light",
                                "cover",
                                "climate",
                            ]
                        )
                    )
                }
            ),
            errors=errors,
        )

    async def async_step_details(
        self, user_input: dict[str, Any] | None = None
    ) -> SubentryFlowResult:
        """Confirm the inferred FRITZ profile and name."""
        if self._source_entity_id is None:
            return self.async_abort(reason="source_entity_missing")
        source_state = self.hass.states.get(self._source_entity_id)
        if source_state is None:
            return self.async_abort(reason="source_entity_missing")
        domain = self._source_entity_id.partition(".")[0]
        supported_color_modes = source_state.attributes.get(
            "supported_color_modes", []
        )
        default_profile = profile_for_domain(
            domain, supported_color_modes=supported_color_modes
        )
        profiles = profiles_for_entity(domain, supported_color_modes)
        errors: dict[str, str] = {}
        if user_input is not None:
            if user_input[CONF_PROFILE] not in profiles:
                errors[CONF_PROFILE] = "incompatible_profile"
            elif not _valid_name(user_input[CONF_FRITZ_NAME]):
                errors[CONF_FRITZ_NAME] = "invalid_name"
            elif self._device_count() >= MAX_VIRTUAL_DEVICES:
                return self.async_abort(reason="device_limit_reached")
            else:
                endpoint_uid = endpoint_uid_from_token(secrets.token_hex(16))
                data = {
                    **user_input,
                    CONF_SOURCE_ENTITY_ID: self._source_entity_id,
                    CONF_ENDPOINT_UID: endpoint_uid,
                    CONF_COLOR_FULL: (
                        user_input[CONF_PROFILE]
                        == DeviceProfile.COLOR_TEMPERATURE_LIGHT.value
                        and full_color_supported(supported_color_modes)
                    ),
                }
                data.update(
                    _detected_schedule_entities(
                        self.hass, self._source_entity_id, user_input[CONF_PROFILE]
                    )
                )
                fritz_name = user_input[CONF_FRITZ_NAME].strip()
                result = self.async_create_entry(
                    title=fritz_name,
                    data=data,
                    unique_id=endpoint_uid,
                    description_placeholders={
                        "fritz_name": fritz_name,
                        "fritz_url": self._get_entry().data.get(
                            CONF_FRITZ_URL, DEFAULT_FRITZ_URL
                        ),
                    },
                )
                entry = self._get_entry()
                await _async_notify_device_created(
                    self.hass,
                    bridge_id=entry.data[CONF_BRIDGE_ID],
                    fritz_name=fritz_name,
                    fritz_url=entry.data.get(CONF_FRITZ_URL, DEFAULT_FRITZ_URL),
                )
                return result

        friendly_name = source_state.attributes.get("friendly_name")
        default_name = (
            friendly_name
            if isinstance(friendly_name, str) and _valid_name(friendly_name)
            else self._source_entity_id.partition(".")[2].replace("_", " ").title()
        )
        schema = probatio.Schema(
            {
                probatio.Required(
                    CONF_PROFILE, default=default_profile.value
                ): SelectSelector(
                    SelectSelectorConfig(
                        options=profiles,
                        translation_key=CONF_PROFILE,
                    )
                ),
                probatio.Required(CONF_FRITZ_NAME, default=default_name): TextSelector(
                    TextSelectorConfig(type="text")
                ),
            }
        )
        return self.async_show_form(
            step_id="details", data_schema=schema, errors=errors
        )

    @override
    async def async_step_reconfigure(
        self, user_input: dict[str, Any] | None = None
    ) -> SubentryFlowResult:
        """Rebind or rename a virtual device without changing its identity."""
        subentry = self._get_reconfigure_subentry()
        errors: dict[str, str] = {}
        if user_input is not None:
            entity_id = user_input[CONF_SOURCE_ENTITY_ID]
            profile = DeviceProfile(subentry.data[CONF_PROFILE])
            source_state = self.hass.states.get(entity_id)
            if source_state is None:
                errors[CONF_SOURCE_ENTITY_ID] = "source_entity_missing"
            elif self._source_in_use(
                entity_id, exclude_uid=subentry.data[CONF_ENDPOINT_UID]
            ):
                errors[CONF_SOURCE_ENTITY_ID] = "already_mapped"
            elif profile.value not in profiles_for_entity(
                entity_id.partition(".")[0],
                source_state.attributes.get("supported_color_modes", []),
            ):
                errors[CONF_SOURCE_ENTITY_ID] = "incompatible_profile"
            elif not _valid_name(user_input[CONF_FRITZ_NAME]):
                errors[CONF_FRITZ_NAME] = "invalid_name"
            else:
                data = {
                    **{
                        key: value
                        for key, value in subentry.data.items()
                        if key
                        not in {
                            CONF_SCHEDULE_WEEK_PROFILE,
                            CONF_SCHEDULE_WORKDAYS,
                            CONF_SCHEDULE_HOLIDAYS,
                        }
                    },
                    **user_input,
                    CONF_ENDPOINT_UID: subentry.data[CONF_ENDPOINT_UID],
                    CONF_COLOR_FULL: (
                        profile is DeviceProfile.COLOR_TEMPERATURE_LIGHT
                        and full_color_supported(
                            source_state.attributes.get("supported_color_modes", [])
                        )
                    ),
                }
                data.update(
                    _detected_schedule_entities(
                        self.hass, entity_id, subentry.data[CONF_PROFILE]
                    )
                )
                return self.async_update_and_abort(
                    self._get_entry(),
                    subentry,
                    data=data,
                    title=user_input[CONF_FRITZ_NAME].strip(),
                )
        return self.async_show_form(
            step_id="reconfigure",
            data_schema=self.add_suggested_values_to_schema(
                _reconfigure_schema(), subentry.data
            ),
            errors=errors,
        )

    def _source_in_use(self, entity_id: str, *, exclude_uid: str | None = None) -> bool:
        """Return whether another persistent mapping already owns an entity."""
        return any(
            subentry.subentry_type == SUBENTRY_TYPE_DEVICE
            and subentry.data.get(CONF_SOURCE_ENTITY_ID) == entity_id
            and subentry.data.get(CONF_ENDPOINT_UID) != exclude_uid
            for subentry in self._get_entry().subentries.values()
        )

    def _device_count(self) -> int:
        """Return mappings owned by this bridge, excluding helper subentries."""
        return sum(
            subentry.subentry_type == SUBENTRY_TYPE_DEVICE
            for subentry in self._get_entry().subentries.values()
        )


async def _async_common_translations(hass: HomeAssistant) -> dict[str, str]:
    """Load this integration's runtime notification strings."""
    return await async_get_translations(
        hass,
        hass.config.language,
        "common",
        {DOMAIN},
    )


def _translation(translations: dict[str, str], key: str) -> str:
    """Return one required translated runtime string."""
    return translations[f"component.{DOMAIN}.common.{key}"]


async def _async_notify_bridge_ready(
    hass: HomeAssistant, *, bridge_id: str, fritz_url: str
) -> None:
    """Explain the otherwise icon-only config-subentry action."""
    translations = await _async_common_translations(hass)
    persistent_notification.async_create(
        hass,
        _translation(translations, "bridge_ready_message").format(fritz_url=fritz_url),
        _translation(translations, "bridge_ready_title"),
        f"{DOMAIN}_{bridge_id}_{_NOTIFICATION_ID_SUFFIX}",
    )


async def _async_notify_device_created(
    hass: HomeAssistant,
    *,
    bridge_id: str,
    fritz_name: str,
    fritz_url: str,
) -> None:
    """Show the next FRITZ!OS assignment step after the device wizard."""
    translations = await _async_common_translations(hass)
    persistent_notification.async_create(
        hass,
        _translation(translations, "device_created_message").format(
            fritz_name=fritz_name,
            fritz_url=fritz_url,
        ),
        _translation(translations, "device_created_title"),
        f"{DOMAIN}_{bridge_id}_{_NOTIFICATION_ID_SUFFIX}",
    )


def _valid_name(name: object) -> bool:
    """Validate the bounded UTF-8 name transported to FRITZ!OS."""
    try:
        validate_device_name(name)
    except ValueError:
        return False
    return True


def _detected_schedule_entities(
    hass: HomeAssistant, source_entity_id: str, profile: str
) -> dict[str, str]:
    """Detect the standard Zigbee2MQTT 5+2 schedule siblings."""
    if profile != DeviceProfile.THERMOSTAT.value:
        return {}
    object_id = source_entity_id.partition(".")[2]
    candidates = {
        CONF_SCHEDULE_WEEK_PROFILE: f"select.{object_id}_week",
        CONF_SCHEDULE_WORKDAYS: f"text.{object_id}_workdays_schedule",
        CONF_SCHEDULE_HOLIDAYS: f"text.{object_id}_holidays_schedule",
    }
    if all(hass.states.get(entity_id) is not None for entity_id in candidates.values()):
        return candidates
    return {}


def _reconfigure_schema() -> probatio.Schema:
    """Return fields that may change while identity and profile remain stable."""
    return probatio.Schema(
        {
            probatio.Required(CONF_SOURCE_ENTITY_ID): EntitySelector(
                EntitySelectorConfig(
                    domain=["switch", "input_boolean", "light", "cover", "climate"]
                )
            ),
            probatio.Required(CONF_FRITZ_NAME): TextSelector(
                TextSelectorConfig(type="text")
            ),
        }
    )
