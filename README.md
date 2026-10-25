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

> [!IMPORTANT]
> This is independent, experimental community software. The first release
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
| `light` | dimmable or color-temperature light | on/off, brightness, optional color temperature |
| `cover` | blind | open, close, stop, position |
| `climate` | radiator thermostat | mode, setpoint, Boost/cold timer, optional next-change schedule display |

FRITZ!OS remains the layout editor for the 440. Home Assistant owns the mapping
between the virtual FRITZ! device and the real entity. One virtual device can be
placed on several controllers, and replacing the mapped Home Assistant entity
can preserve its FRITZ! identity and existing 440 assignments.

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
switching and dimming. Version 0.2.0-beta.1 adds the accumulated recovery,
catalogue-sync, parser and lifecycle hardening and completes the TentAHAcle
rename. This beta passed the repository test suites but has not yet completed
live qualification with the physical controller.
See [Compatibility](docs/compatibility.md) for the exact baseline.

### Project status and contributions

This is a working experimental beta, not a finished appliance. The supported
profiles, persistent device lifecycle, MQTT recovery and direct 440 command
path have automated tests and have been exercised on the documented laboratory
setup. Useful contributions include longer real-world testing, verification on
additional FRITZ!OS builds and hardware, better Home Assistant diagnostics,
parser fuzzing and further modularisation of the native MQTT bridge. Please
report reproducible findings in [GitHub Issues](https://github.com/Allcrafter1/tentahacle-virtual-device-bridge/issues).

No AVM firmware, AVM binary or modified firmware image is distributed here.
The user builds their own image locally. Read [Licensing and distribution
boundaries](docs/licensing.md) before installing it.

## Technical documentation

- [Installation, update and rollback](docs/installation.md)
- [Architecture and user workflow](docs/architecture.md)
- [Compatibility policy](docs/compatibility.md)
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
