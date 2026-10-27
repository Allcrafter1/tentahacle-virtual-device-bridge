# SPDX-License-Identifier: MIT OR Apache-2.0
"""Constants for TentAHAcle - Virtual Device Bridge."""

from enum import StrEnum

DOMAIN = "tentahacle"
PROTOCOL_VERSION = 1
MAX_VIRTUAL_DEVICES = 32
DEFAULT_TOPIC_ROOT = "tentahacle"
DEFAULT_FRITZ_URL = "http://fritz.box"

CONF_BRIDGE_ID = "bridge_id"
CONF_TOPIC_ROOT = "topic_root"
CONF_SOURCE_ENTITY_ID = "source_entity_id"
CONF_PROFILE = "profile"
CONF_FRITZ_NAME = "fritz_name"
CONF_FRITZ_URL = "fritz_url"
CONF_ENDPOINT_UID = "endpoint_uid"
CONF_COLOR_FULL = "color_full"
CONF_SCHEDULE_WEEK_PROFILE = "schedule_week_profile"
CONF_SCHEDULE_WORKDAYS = "schedule_workdays"
CONF_SCHEDULE_HOLIDAYS = "schedule_holidays"


class DeviceProfile(StrEnum):
    """Profiles implemented by the FRITZ provider."""

    SWITCH = "switch"
    DIMMABLE_LIGHT = "dimmable_light"
    COLOR_TEMPERATURE_LIGHT = "color_temperature_light"
    COVER = "cover"
    THERMOSTAT = "thermostat"
