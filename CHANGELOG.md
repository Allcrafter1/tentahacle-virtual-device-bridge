# Changelog

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
