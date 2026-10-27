# TentAHAcle - Virtual Device Bridge

Virtual Home Assistant devices for FRITZ! Smart Home and FRITZ!Smart Control 440.

[English](README.md) | [Deutsch](README.de.md)

Use a **FRITZ!Smart Control 440** to control Home Assistant lights, switches,
blinds and thermostats that FRITZ!OS cannot normally assign to the controller.

TentAHAcle - Virtual Device Bridge creates a persistent virtual FRITZ! Smart
Home device for each selected Home Assistant entity. You assign that virtual
device to the 440 in the normal FRITZ!OS interface. Commands and confirmed
state then travel in both directions over local MQTT.

For example, a Zigbee, Matter, Hue or Wi-Fi light that already works in Home
Assistant can appear as a FRITZ! light on the 440. Depending on the selected
profile, the 440 can switch it, dim it and adjust its color temperature.
With a compatible color light, the 440 color palette is also translated to the
Home Assistant light and the confirmed color is sent back.

> [!IMPORTANT]
> This is independent, experimental community software. The current candidate
> supports one exact bridge model and firmware version. Use a dedicated bridge
> box, not the FRITZ!Box responsible for your Internet connection.
>
> TentAHAcle is not affiliated with, endorsed by, sponsored by or supported by
> FRITZ! GmbH. Product names are used solely to describe compatibility. Do not
> contact FRITZ! support for help with this project. See [Trademark
> notice](TRADEMARKS.md).

## What you need

- a FRITZ!Smart Control 440;
- a dedicated classic **FRITZ!Box 7530 / HW236 with FRITZ!OS 8.25**;
- Home Assistant with a reachable MQTT broker and the MQTT integration;
- Ethernet for the one-time bridge installation;
- one of the tested build environments:
  - x86-64 Linux, or
  - Windows with x86-64 WSL2 and Ubuntu.

The Windows -> WSL2 path was used for the original laboratory installation and
is a supported path. Codex can guide and perform most of that setup from WSL2.

## Start here

1. **Prepare the bridge:** locally build and install the package on the
   dedicated 7530 using Linux or WSL2.
2. **Connect it:** configure the 7530 as an IP client, then enter the MQTT
   broker and its dedicated login in the Freetz web interface.
3. **Install the integration:** add this repository to HACS as an Integration,
   install **TentAHAcle - Virtual Device Bridge**, and restart Home Assistant.
4. **Add devices:** select a Home Assistant entity, create its virtual FRITZ!
   device, and follow the link to FRITZ!OS to assign it to the 440.

**[Open the complete installation guide](docs/installation.md)**

The guide includes the tested Windows/WSL2 route, a copyable prompt for a
Codex-assisted installation, MQTT setup, HACS setup, updating, diagnostics and
rollback.

If modifying the FRITZ!Box feels too complicated, open a
[GitHub issue](https://github.com/Allcrafter1/tentahacle-virtual-device-bridge/issues).
The maintainer is willing to work through a compatible setup with users and use
that experience to improve the installation path. Never post passwords,
configuration exports, serial numbers or other private device data in an issue.

## Supported virtual devices

| Home Assistant source | Virtual FRITZ! device | Available functions |
|---|---|---|
| `switch`, `input_boolean` | switch/socket | on/off |
| `light` | dimmable or color light | on/off, brightness, color temperature where supported, and Hue/Saturation for HA lights with `hs`, `xy`, `rgb`, `rgbw` or `rgbww` support |
| `cover` | blind | open/close; intermediate position only with `SET_POSITION`, stop only with `STOP` |
| `climate` | radiator thermostat | heat/off, 8–28 °C setpoint, MAX, current temperature, Boost/cold timer, Celsius/Fahrenheit conversion and optional next-change schedule display |

FRITZ!OS remains the layout editor for the 440. Home Assistant owns the mapping
between the virtual FRITZ! device and the real entity. One virtual device can be
placed on several controllers, and replacing the mapped Home Assistant entity
can preserve its FRITZ! identity and existing 440 assignments.

A bridge currently supports at most **64 virtual devices**. This is a
conservative limit of our statically sized registry and Home Assistant UI, not
a proven FRITZ!OS or `aha` limit. The same virtual device may be placed on
several 440s and still counts only once. Create, rename, rebind and delete
devices in Home Assistant; deleting a mapping also removes its native device.
The virtual parent is deliberately not independently deletable in FRITZ!OS.
Use FRITZ!OS for controller layout and for native templates, scenes and
routines—the bridge does not recreate those features in Home Assistant.

## Current compatibility and limitations

The preceding 0.1.5 laboratory baseline was validated with:

- FRITZ!Box 7530 classic / HW236, FRITZ!OS 8.25;
- FRITZ!Box 6690 Cable, FRITZ!OS 8.25, as Mesh master;
- FRITZ!Smart Control 440, firmware 05.45;
- Home Assistant with MQTT.

The bridge checks the exact internal `aha` binary fingerprint and refuses to
start the provider on an unknown build. Automatic FRITZ!OS updates must remain
disabled on the bridge until a new version has been analysed and allowlisted.

The validated Mesh-master route works but added about 6-7 seconds of command
latency in the laboratory. Pairing the 440 directly with the dedicated bridge
box is the intended topology and has been tested in both directions with
switching and dimming. The current development candidate adds full-color and
current-temperature feedback, capability-aware cover handling, thermostat MAX,
device-capacity enforcement and additional recovery/lifecycle hardening. These
paths have automated coverage but have not all completed the focused physical
release qualification listed below.
See [Compatibility](docs/compatibility.md) for the exact baseline.

Known boundaries of the current candidate:

- the color-temperature page has exactly nine native Kelvin presets. Short
  presses traverse them and FRITZ!OS 8.25 cycles from either endpoint to the
  other. The bridge preserves that native behavior because the absolute
  endpoint command carries no reliable indication that it came from a wrap.
  Lights without a real `color_temp` mode can only emulate Kelvin through
  color in Home Assistant, and a light range containing none of the nine
  presets cannot be represented faithfully;
- binary covers report confirmed end positions but do not invent a position
  while moving; unsupported stop or intermediate-position commands are rejected;
- FRITZ!OS can show the virtual blind's parent as a light/LED and can warn about
  unset end positions because no real blind calibration exists;
- a thermostat value above 28 °C is representable only when it is exactly the
  source entity's MAX value; other FRITZ thermostat settings such as locks,
  offset, sensor selection, adaptive heating or holiday modes are not bridged;
- an already open 440 page may not refresh immediately. Reopening it must show
  the current confirmed state; the same behavior was reproduced with original
  thermostat hardware;
- virtual devices can be selected and stored in native templates and scenes,
  but final runtime qualification of templates, scenes and routines is still
  part of the release checklist.

### Project status and contributions

This is working experimental software, not a finished appliance. The exact
`0.3.0-rc7` image built from commit `add07f9166a8dd059aec556bf5236d551e0d04fe`
is installed persistently on boot slot 1 of the documented laboratory 7530;
RC6 remains available in slot 0 for rollback. The matching Home Assistant RC7
manifest is loaded with the unchanged hub and all eleven device subentries.
The eleven-device registry survived two controlled boots unchanged, and after
the second boot the service became healthy without intervention after about
110 seconds. Sixty consecutive Provider control checks then succeeded.

RC7 fixes a second maintenance failure mode found after RC6: a blocking native
`cR/TX` write could hold the Provider's global state/control mutex long enough
for the watchdog to mistake a healthy AHA process for a failed control channel.
The Provider now duplicates the tracked descriptor, checks its connection
epoch after the unlocked write, serializes native writes separately and defers
catalog injection until that native write is complete. A deterministic
blocked-write regression covers this mechanism. A real pairing or controller
firmware update under RC7 has **not yet been physically repeated**, so the
earlier RC6 05.38-to-05.45 update is historical evidence, not confirmation of
the RC7 fix.
The exact evidence and this remaining qualification are tracked in the release
checklist. Useful contributions still include that physical maintenance canary,
longer real-world testing and verification on additional supported hardware.
Please
report reproducible findings in [GitHub Issues](https://github.com/Allcrafter1/tentahacle-virtual-device-bridge/issues).

No AVM firmware, AVM binary or modified firmware image is distributed here.
The user builds their own image locally. Read [Licensing and distribution
boundaries](docs/licensing.md) before installing it.

## Technical documentation

- [Installation, update and rollback](docs/installation.md)
- [Architecture and user workflow](docs/architecture.md)
- [Compatibility policy](docs/compatibility.md)
- [Finite release validation checklist](docs/release-validation.md)
- [MQTT protocol version 1](docs/mqtt-protocol.md)
- [Product decisions](docs/decisions.md)
- [Licensing and distribution boundaries](docs/licensing.md)
- [Trademark and independence notice](TRADEMARKS.md)

## Development disclosure

Significant parts of the interoperability research, implementation and
documentation were created with assistance from generative AI. The resulting
architecture, source code and behaviour were reviewed and tested against the
documented laboratory setup. Contributions and independent verification are
welcome.

## License

Original project code is dual-licensed under your choice of the permissive
[Apache License 2.0](LICENSE) or [MIT License](LICENSE-MIT). The MIT option keeps
the Freetz-NG package compatible with GPL-2.0-only code. Third-party components
retain their own licenses. See [NOTICE](NOTICE).
