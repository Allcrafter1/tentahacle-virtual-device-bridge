# MQTT protocol version 1

This protocol is local to TentAHAcle - Virtual Device Bridge. Every JSON object contains
`"schema_version": 1`. The examples below use the configured topic prefix
`tentahacle/lab7530` and endpoint `TVB0123456789ABCDEF`.

## Bridge lifecycle

| Topic | Retained | Payload |
|---|---:|---|
| `tentahacle/lab7530/bridge/availability` | yes | `online` or `offline` |
| `tentahacle/lab7530/bridge/info` | yes | bridge ID, version, readiness, registry revision and supported profiles |
| `tentahacle/lab7530/bridge/registry` | yes | complete desired-device registry without credentials |

Home Assistant replays every mapping and its current state when availability
changes to `online`. This makes either startup order safe. A bridge can be
online while `ready` is false; persisted desired state is then reconciled when
the FRITZ provider reconnects.

## Device management

Requests are non-retained QoS 1 messages to
`tentahacle/lab7530/management/request`. `request_id` contains 1–64 ASCII
letters, digits, `_` or `-`. An optional `expected_revision` provides
optimistic concurrency control.

Create a device or idempotently update its name and enabled state:

```json
{
  "schema_version": 1,
  "request_id": "d34149fa-2026-4ad0-a86a-7062dbff8df7",
  "operation": "upsert_device",
  "device": {
    "uid": "TVB0123456789ABCDEF",
    "profile": "color_temperature_light",
    "name": "Morgenlicht",
    "enabled": true
  }
}
```

The immutable `uid` and `profile` determine the persistent FRITZ identity.
Supported profiles are `switch`, `dimmable_light`,
`color_temperature_light`, `cover` and `thermostat`. Names contain 1–79 valid
UTF-8 bytes without leading whitespace. The registry holds at most 32 devices.

Other operations are:

```json
{"schema_version":1,"request_id":"rename-1","operation":"rename_device","uid":"TVB0123456789ABCDEF","name":"Leselicht"}
{"schema_version":1,"request_id":"disable-1","operation":"set_enabled","uid":"TVB0123456789ABCDEF","enabled":false}
{"schema_version":1,"request_id":"sync-1","operation":"reconcile_devices","enabled_uids":["TVB0123456789ABCDEF"]}
{"schema_version":1,"request_id":"prune-1","operation":"prune_devices","keep_uids":["TVB0123456789ABCDEF"]}
{"schema_version":1,"request_id":"list-1","operation":"list_devices"}
{"schema_version":1,"request_id":"announce-1","operation":"reannounce"}
```

Responses are published to
`tentahacle/lab7530/management/response/<request_id>`. `accepted: true`
means the desired state was stored. `ready: false` with `provider_pending`
means it will be applied after the provider reconnects. `flash_persisted`
reports whether the atomic runtime registry was also committed through
Freetz's `modsave`.

`prune_devices` makes Home Assistant's mapping list authoritative. Devices not
listed in `keep_uids` are deleted through the local native AHA API, removed
from the running provider and removed from the persistent bridge registry.
The operation is retried periodically by Home Assistant and is idempotent.
`reconcile_devices` remains available for backward compatibility and only
changes enabled state.

## Commands from FRITZ!OS

The bridge publishes non-retained QoS 1 JSON on
`tentahacle/lab7530/device/<uid>/command`. Common fields are `event:
"command"`, the matching `endpoint`, and a monotonically increasing process
local `sequence`.

Depending on the profile, a command contains one of these shapes:

```json
{"event":"command","endpoint":"TVB0123456789ABCDEF","state":1,"sequence":1}
{"event":"command","endpoint":"TVB0123456789ABCDEF","state":1,"level":55,"sequence":2}
{"event":"command","endpoint":"TVB0123456789ABCDEF","state":1,"color_mode":"temperature","color_temperature":4000,"sequence":3}
{"event":"command","endpoint":"TVB0123456789ABCDEF","action":"open","position":50,"sequence":4}
{"event":"command","endpoint":"TVB0123456789ABCDEF","hvac_mode":"heat","target_temperature":21.5,"sequence":5}
{"event":"command","endpoint":"TVB0123456789ABCDEF","timer_action":"boost","end_time":1791046800,"duration":1800,"sequence":6}
```

Cover actions are `open`, `close`, `stop` or a position update. Thermostat
timer actions are `boost`, `cold` or `cancel`.

## Confirmed state from Home Assistant

Home Assistant publishes retained QoS 1 scalar payloads below
`tentahacle/lab7530/device/<uid>`:

| Suffix | Payload |
|---|---|
| `state/set` | `ON` or `OFF` |
| `level/set` | integer 0–100 |
| `color_temperature/set` | kelvin value |
| `position/set` | Home Assistant cover position 0–100 |
| `target_temperature/set` | 8–28 °C in 0.5 °C steps |
| `hvac_mode/set` | `off` or `heat` |
| `timer/set` | `cancel`, `boost <unix-time>` or `cold <unix-time>` |
| `schedule/set` | `disabled` or `active <current-half-degrees> <next-half-degrees> <current-week-minute> <next-week-minute>` |

The `/set` suffix means confirmed source state, not an unconfirmed command.
The bridge caches the newest valid values while the provider is unavailable
and replays them after device announcement and HAN-FUN unit provisioning.
