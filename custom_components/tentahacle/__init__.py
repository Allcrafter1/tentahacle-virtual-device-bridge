# SPDX-License-Identifier: MIT OR Apache-2.0
"""TentAHAcle - Virtual Device Bridge integration."""

from __future__ import annotations

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.translation import async_get_translations

from .bridge import TentAHAcleBridge
from .const import CONF_BRIDGE_ID, CONF_FRITZ_URL, DEFAULT_FRITZ_URL, DOMAIN
from .models import fritz_smart_home_url

type TentAHAcleConfigEntry = ConfigEntry[TentAHAcleBridge]


async def async_setup_entry(
    hass: HomeAssistant, entry: TentAHAcleConfigEntry
) -> bool:
    """Set up a configured bridge."""
    bridge = TentAHAcleBridge(hass, entry)
    try:
        await bridge.async_start()
        translations = await async_get_translations(
            hass, hass.config.language, "common", {DOMAIN}
        )
        bridge_id = entry.data[CONF_BRIDGE_ID]
        entry_title = translations.get(
            f"component.{DOMAIN}.common.bridge_entry_title",
            "{bridge_id} - use + to add a device",
        ).format(bridge_id=bridge_id)
        old_default_title = f"TentAHAcle - Virtual Device Bridge {bridge_id}"
        if entry.title in {bridge_id, old_default_title}:
            hass.config_entries.async_update_entry(entry, title=entry_title)
        dr.async_get(hass).async_get_or_create(
            config_entry_id=entry.entry_id,
            identifiers={(DOMAIN, bridge_id)},
            name=translations.get(
                f"component.{DOMAIN}.common.bridge_device_name",
                "Open the FRITZ!OS interface",
            ),
            manufacturer="TentAHAcle - Virtual Device Bridge project",
            model="Local MQTT bridge",
            configuration_url=fritz_smart_home_url(
                entry.data.get(CONF_FRITZ_URL, DEFAULT_FRITZ_URL)
            ),
        )
        entry.async_on_unload(entry.add_update_listener(_async_reload_entry))
    except BaseException:
        # Setup may fail or be cancelled after subscriptions/timers exist.
        # HA has no completed runtime to unload yet; release it before retry.
        await bridge.async_stop()
        raise
    entry.runtime_data = bridge
    return True


async def async_unload_entry(
    hass: HomeAssistant, entry: TentAHAcleConfigEntry
) -> bool:
    """Unload a configured bridge."""
    await entry.runtime_data.async_stop()
    return True


async def async_remove_entry(
    hass: HomeAssistant, entry: TentAHAcleConfigEntry
) -> None:
    """Restore active overrides and prune native devices when deleting a hub."""
    await TentAHAcleBridge(hass, entry).async_remove()


async def _async_reload_entry(
    hass: HomeAssistant, entry: TentAHAcleConfigEntry
) -> None:
    """Reload after bridge or device-subentry changes."""
    await hass.config_entries.async_reload(entry.entry_id)
