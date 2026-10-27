# Changelog

## Unreleased

- Normalize physical color-temperature feedback such as 2702, 3003, 4716 or
  6535 K onto the nine exact presets recognized by FRITZ!OS 8.25. Enforce the
  same invariant again at the native MQTT boundary so stale retained data and
  older clients cannot inject an unrecognized value.
- Accept FRITZ!OS configuration URLs with fragments, including
  `http://192.168.178.38/#/smart-home/devices`, while retaining validation of
  credentials, query parameters, ports and control characters.
- Accept finite floating-point cover positions from Home Assistant instead of
  falling back to broad open/closed state, and add opt-in correlated debug logs
  across the Provider, MQTT bridge and Home Assistant command path.
- Correct the documentation: 32 devices is the current implementation limit,
  not a demonstrated native maximum.

These changes pass the automated source tests but have **not** yet been
installed on the laboratory 7530 or physically confirmed with the 440. The
running laboratory system still uses the earlier `fritz_virtual_bridge`/`FVB`
generation. Do not treat this section as a release or rollout approval.

## 0.3.0-rc.1 - 2026-10-27

- Add automated coverage for brightness, color-temperature and
  Hue/Saturation round trips for compatible Home Assistant lights.
- Make cover commands follow each source entity's actual open, close, stop and
  positioning capabilities, including truthful binary-cover feedback.
- Complete thermostat feedback for current temperature, OFF and MAX, support
  Celsius and Fahrenheit sources, harden Boost/cold persistence and cancellation,
  and prevent deletion or rebinding from leaving a real thermostat at MAX/OFF.
- Enforce the current implementation's 32-device capacity, retry the complete authoritative Home
  Assistant snapshot every minute and attempt native-device cleanup when their
  Home Assistant hub is removed, retaining recovery state if cleanup cannot
  reach MQTT or a source entity.
- Add direct Smart Home configuration links and reject source entities that
  cannot implement the selected persistent profile.
- Supervise both the virtual AHA provider and MQTT bridge, retain autonomous
  recovery after failed startup, bound runtime logs and make shutdown safe
  around flash persistence.
- Replace overflow-prone native HS parsing with bounded decimal parsing and
  extend lifecycle, fault-injection, sanitizer and parser regression coverage.

This is a release candidate for the existing FRITZ!Box 7530 / FRITZ!OS 8.25
compatibility tuple. The host suites and bounded security/recovery audit pass.
The focused physical-controller checklist in `docs/release-validation.md` must
still pass on the laboratory box before the final `0.3.0` release and broad
multi-controller deployment. Update both the Home Assistant integration and
the Freetz package together.

## 0.2.0-beta.1 - 2026-10-25

- Rename the project and every public or technical identifier to
  **TentAHAcle - Virtual Device Bridge**. This includes the Home Assistant
  domain, HACS metadata, Freetz package, service and binary names, MQTT topic
  root, runtime paths, log names and virtual-device UID prefix.
- Add an explicit trademark and independence notice. References to FRITZ!
  products now serve only to describe compatibility.
- Add event-driven local catalogue refresh after virtual unit provisioning and
  bounded restoration of saved controller widgets after AHA restarts.
- Harden native partial-write recovery, process replacement, provider cleanup,
  registry persistence, MQTT reconciliation and Home Assistant unload/setup
  races.
- Tighten parser, topic, name, numeric and timer validation, with expanded
  fault-injection, sanitizer, fuzz and lifecycle tests.

This is a breaking prerelease. Existing 0.1.x Home Assistant entries, MQTT
topics, Freetz settings, virtual UIDs and runtime paths are not migrated. Remove
the previous test installation on both sides and install this beta as a new
bridge. The accumulated robustness changes passed the repository test suites
but have not yet completed live qualification with a physical controller.

## 0.1.5 - 2026-10-04

- Split the native provider into state, profile encoding, event and transport
  modules while preserving all 70 recorded v0.1.4 protocol packets byte for
  byte, apart from clock-derived fields.
- Harden provider startup and shutdown, socket and process ownership, short or
  interrupted I/O, MQTT setup, strict replies and registry file loading.
- Fix cleanup after cJSON allocation failures and protect finite-number checks
  from Freetz's `-Ofast` defaults. Add stack protection, immediate symbol
  resolution, RELRO and a non-executable stack to all native targets.
- Add fault-injection, sanitizer, component, lifecycle and cross-build tests,
  including all supported device profiles and failure paths.
- Report the actual package version in bridge diagnostics.

This release requires updating the FRITZ!Box package. The Home Assistant
integration version is bumped in step with it, although its protocol remains
compatible with 0.1.4.

## 0.1.4 - 2026-10-04

- Handle multiple native protocol messages in one stream write. Previously,
  batched commands bypassed the virtual provider, causing intermittent missing
  dimming commands and inconsistent controller feedback.
- Validate complete batches before processing and preserve command order and
  unrelated-frame forwarding. Add regression tests reproducing the previous
  failure, mixed traffic and malformed batches.
- Requires updating the FRITZ!Box provider; an integration-only update does not
  change the native command path.

## 0.1.3 - 2026-10-04

- Restore each device's persisted remote ID explicitly after provider restarts,
  including allocation gaps left by deleted devices. Previously an ID mismatch
  stopped provisioning before the lamp unit was created.
- Correct the HAN-FUN interface-list byte order for the local 7530 / 8.25
  receiver. A device could previously appear by name but have no assignable
  lamp or blind unit for a directly paired controller.
- Add regression coverage for durable IDs, collisions and local interface
  encoding. Verified native dimmable unit creation on the laboratory 7530.
- This fix requires rebuilding/updating the FRITZ!Box package; updating only
  the Home Assistant integration does not replace the provider.

## 0.1.2 - 2026-10-04

- Detect an unresponsive internal provider control channel and disconnect the
  stale watcher instead of silently accepting unusable MQTT state.
- Add a local watchdog that restarts only the isolated FRITZ!OS Smart Home
  process after three failed health checks; internet and network services are
  not restarted.
- Bound local control-socket connection attempts so a full or stale queue
  cannot block recovery indefinitely.

## 0.1.1 - 2026-10-04

- Reworked the README into a problem-first onboarding path for FRITZ!Smart
  Control 440 users and added a complete German entry page.
- Documented the practically tested Windows/WSL2 setup, including a copyable
  Codex-assisted installation prompt.
- Clarified the recommended dedicated Mosquitto-app login and the separate MQTT
  credentials used by the bridge.
- Fixed the Freetz service status check to use the MQTT bridge PID file.
- Fixed missing labels in the Freetz package UI by declaring its translated
  CGI file to the Freetz language build step.
- Made the Freetz installer derive the source archive version from the package
  definition so release bumps cannot leave a stale archive name behind.
- Documented update isolation, two-part backups and direct 440 pairing with the
  bridge box.

## 0.1.0 - 2026-10-04

First public test release.

- Expose Home Assistant switches, lights, covers and climate entities as
  persistent virtual FRITZ! Smart Home devices.
- Carry commands from FRITZ!OS and FRITZ!Smart Control 440 to Home Assistant
  and confirmed state back over local MQTT.
- Support dimming, color temperature, cover position, thermostat setpoints,
  timed Boost/cold modes and the tested Zigbee2MQTT schedule shadow.
- Add MQTT discovery and Home Assistant UI flows for creating, rebinding and
  deleting virtual devices.
- Add a Freetz web page for broker credentials and bridge configuration.
- Restrict provider injection to the validated FRITZ!Box 7530 / FRITZ!OS 8.25
  `aha` fingerprint.

Known limitation: commands routed through a separate 6690 Mesh master took
roughly 6–7 seconds in the laboratory. Pair the controller directly with the
dedicated bridge box for the intended topology; its final latency measurement
is still pending.
