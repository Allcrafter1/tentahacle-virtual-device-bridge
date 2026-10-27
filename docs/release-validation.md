# Release validation checklist

This is the finite release gate for the next TentAHAcle candidate. It separates
what can be proven offline from the few observations that require the dedicated
7530, a FRITZ!Smart Control 440 and real Home Assistant entities. It is not an
open-ended feature backlog.

## Evidence snapshot — 2026-10-27

| Layer | Observed status |
|---|---|
| Running 7530 | TentAHAcle `0.3.0-rc2` built from `a47e330` is installed; service and provider are running and MQTT reports connected |
| Running Home Assistant | Matching `0.3.0-rc2` component is loaded; the old `fritz_virtual_bridge` entry is disabled; `Morgenlicht` and `Testrollladen` are provisioned as two canaries |
| Exact artifact | `TentAHAcle-0.3.0-rc2-7530-08.25.image`, SHA-256 `b1092993dbc137f27a9ab0fbb16d8b304b9db012f286c964a869ada79ecdbf51` |
| Live smoke evidence | Freetz package registration, service/provider startup, migrated broker authentication, MQTT discovery, persistent registry and the Smart Home fragment deep link passed |
| Automated evidence | Exact head: 294 Python tests passed, one Lua-dependent test skipped, plus Ruff and packaging regression checks. The code candidate passed the strict native, ASan/UBSan, fuzz, fault-injection, analyzer and ARM checks; the packaging-only final commit was rebuilt for ARM. LSan/TSan could not run reliably in the current host environment and are not claimed for this exact head |
| Physical evidence for this exact source | Not yet confirmed with the 440. Warm/cold, dimming, cover feedback, thermostat/recovery and native automation rows below remain release gates |

Consequently, installation and a live smoke check must not be described as
hardware-confirmed until the corresponding row below is repeated with a 440.
The archived `0.3.0-rc.1` image predates the current fixes.

## Automated gate

Before building firmware, all of the following must pass from a clean checkout:

- complete Python/Lua test suite and Ruff;
- shell syntax and Freetz packaging/restart tests;
- native host suite with strict warnings, ASan, UBSan (including float casts)
  and LeakSanitizer;
- deterministic registry/parser/dispatcher fuzz loops, dynamic and legacy
  provider self-tests and the byte-level golden fixture;
- exact supported `aha` fingerprint and successful ARM/Freetz package build;
- clean `git diff --check`, documented version/changelog and no AVM binary or
  generated firmware committed to the repository.

The current source includes automated coverage for exact-palette Kelvin/HS
state, off-palette Kelvin normalization at both software boundaries, binary and
position covers (including float positions), Celsius/Fahrenheit thermostat
translation, OFF/MAX/current temperature, restart-safe timers, the current
32-device implementation capacity, URL fragments, HA-authoritative deletion,
registry persistence and watchdog transitions. Automated coverage is not a
claim that the same revision is installed or physically confirmed.

## Physical function matrix

Use one directly paired 440 for release qualification before changing the
other six controllers.

| Area | Already covered automatically | Physical release check | Pass condition |
|---|---|---|---|
| Switch/socket | `switch`, `input_boolean`, reduced on/off `light`; commands and external feedback | Toggle from 440 and HA, then reopen the 440 page | Both directions agree after every action |
| Dimming | Scaling, off-with-value command, retained replay | Repeated slow and fast up/down; off then set a value; HA restart | Requested final percentage reaches HA and reopening shows it |
| Kelvin/full color | Kelvin and HS roundtrip, exact-palette normalization, mode switch and stale retained-mode clearing | Use short presses through all nine Kelvin values and both endpoints; HA -> 440; Kelvin <-> color; provider reconnect | Correct source mode/value each time; non-exact source feedback returns as the nearest native value; no old color returns after reconnect. At either endpoint a further press holds the value and creates no HA/MQTT command |
| Binary cover | Open/close, 0/100 feedback, no invented moving endpoint | Cover without `current_position`; open/close and observe `opening`/`closing` | Commands work; page shows confirmed endpoint after completion |
| Position cover | Set position, endpoint fallback, capability rejection, optional stop | 25/50/75 %, stop during travel, external HA position change | HA and reopened 440 page agree; unsupported actions are not offered/forwarded |
| Thermostat basics | Compatible-source filter, 8–28 °C, heat/off, MAX, current temperature, C/F | Ordinary +/- including OFF/MAX; change measured temperature externally | Correct HA service/value and correct state after reopening the page |
| Boost/cold | Persisted start, extend, cancel, natural expiry, external-change cancellation | Start/cancel and natural expiry for both; restart HA during one; make an external HA change during one | Original state restores only on genuine expiry/cancel; external change is never overwritten |
| 5+2 shadow schedule | Parsing, current/next calculation, stale schedule clearing | Use validated Zigbee2MQTT adapter and confirm next-change time | Display matches HA schedule; FRITZ edits do not become authority |
| Mapping lifecycle | Stable UID, rebind, delete, whole-hub cleanup | Rebind keeps 440 placement; delete removes endpoint; repeat with an active timer | Old climate is restored and no stale native device remains |
| Native automation | Protocol endpoint is native; selection/persistence observed | Activate one harmless template per profile, one multi-device scenario and one routine | HA action occurs and confirmed state returns; unsupported FRITZ trigger types are documented |
| Capacity/link | Current 32-device implementation limit, prune-before-upsert, fragment deep-link construction | 31 -> 32 -> rejected 33; replace one at full capacity; save and open `http://192.168.178.38/#/smart-home/devices` from all three HA links | No partial registry, replacement succeeds, links open Smart Home; capacity is not described as a native ceiling |

The lack of immediate refresh on an already open 440 page is not a failure: it
was reproduced with an original FRITZ!Thermo. Reopening the page must show the
correct confirmed state.

## Recovery and persistence matrix

Run these only on the laboratory 7530. Do not inject them into the productive
6690.

| Event | Expected recovery |
|---|---|
| Restart Home Assistant | Mappings keep their UIDs; retained state and active thermostat timer are replayed |
| Restart MQTT broker / temporary broker outage | Bridge reconnects automatically; HA performs a complete authoritative replay |
| Terminate only the MQTT bridge | Watchdog restarts it while virtual AHA remains available |
| Make initial virtual-provider startup fail once | Stock AHA is only temporary; watchdog keeps retrying until the virtual provider is healthy |
| Restart virtual AHA/provider | Registry restores identical devices/remote IDs and bounded screen resync restores saved widgets |
| Reboot the 7530 | Registry survives flash persistence; devices and 440 assignments retain identity |
| Fail one registry/persist attempt in the host fault test | Periodic HA upsert/prune and bridge dirty-flash retry converge without duplication |
| Stop/restart during helper activity | Native helpers abort safely; an active `modsave` may delay stop but is not killed and finishes within the 40-second service window |
| Persistent MQTT auth failure | Process remains retryable; each package log is reset after crossing 1 MiB on the next one-minute watchdog check |

After the matrix, use the bridge normally with one 440 long enough to include
at least one HA restart, broker reconnect and 7530 reboot. Only then assign the
remaining six 440s.

## Accepted limitations

- Only the documented FRITZ!Box/FRITZ!OS/`aha` fingerprint is supported. An
  update requires a new offline build and qualification; automatic firmware
  updates stay disabled on the dedicated box.
- A virtual cover parent may look like an LED/lamp and FRITZ!OS may show an
  end-position calibration warning. No unverified calibration data is faked.
- Binary covers do not report invented intermediate positions. Stop and numeric
  positions require the corresponding HA capabilities.
- FRITZ!OS 8.25 natively wraps the nine-step color-temperature selector from
  6500 back to 2700 K and from 2700 to 6500 K. The bridge deliberately differs:
  for dynamic virtual lights it consumes either endpoint-wrap attempt, echoes
  the unchanged endpoint immediately and emits no HA/MQTT command. Physical
  and other native devices retain their stock behavior.
- A source light with no real `color_temp` mode may emulate Kelvin as chromatic
  color, and a source range that contains none of the nine native presets has
  no fully faithful 440 representation.
- A thermostat value above 28 °C that is not exact source MAX has no native
  FRITZ representation. Non-bridged Thermo editor settings and the displayed
  synthetic battery are not source configuration/telemetry.
- 440 long-press gestures are not available through the tested native
  assignment path. Already open pages may refresh only after reopening.
- Templates, scenarios and routines remain FRITZ!OS features. The bridge does
  not reproduce their editor or promise device types FRITZ!OS itself excludes.
- MQTT commands use a bounded in-memory provider event queue, not a durable
  event log. The local broker and LAN remain part of the availability model.

## Release decision

The candidate has been flashed to the laboratory 7530 and its basic runtime
path has passed the smoke check. The next gate is the focused physical matrix
with one directly paired 440, followed by the bounded recovery checks. It is
ready for seven-controller production use and public release only when those
checks have no critical failure or reproducible major functional defect.
Cosmetic limitations above are acceptable when documented; new speculative
enhancements are not release blockers.
