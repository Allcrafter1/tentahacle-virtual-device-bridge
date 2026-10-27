# Installation on a dedicated FRITZ!Box 7530

[English](installation.md) | [Deutsch](installation.de.md)

This guide takes you from an unused compatible FRITZ!Box to a working virtual
device on the FRITZ!Smart Control 440. The normal path is:

1. build and install the bridge firmware locally;
2. configure the bridge network and MQTT connection in its web interfaces;
3. install the Home Assistant integration through HACS;
4. create a virtual device and assign it in FRITZ!OS.

After the one-time firmware preparation, normal configuration requires no SSH,
terminal commands or manual file editing.

> [!IMPORTANT]
> Version 0.3.0-rc3 upgrades 0.2.0-beta.1, 0.3.0-rc.1 or 0.3.0-rc2 in place: update the Home Assistant
> integration and Freetz package together, while keeping existing virtual-device
> identities and controller assignments. Only installations coming directly
> from a former 0.1.x test build need a clean reinstall because 0.2.0-beta.1
> deliberately changed the Home Assistant, MQTT, Freetz, filesystem and virtual
> device identifiers. Back up both sides before either update path.

If the FRITZ!Box modification is the part keeping you from trying the project,
open a [GitHub issue](https://github.com/Allcrafter1/tentahacle-virtual-device-bridge/issues).
The maintainer is willing to work through a compatible setup with users and
turn recurring difficulties into a simpler documented process. Do not attach
passwords, configuration exports, serial numbers or other private device data.

The currently supported target is deliberately narrow: classic FRITZ!Box 7530
(HW236), German FRITZ!OS 8.25, and the exact `aha` fingerprint listed in the
compatibility document. Do not use the build on another model or firmware.

## Before building

Use a separate laboratory/bridge box. Export its FRITZ!Box configuration and
have the official AVM recovery procedure available. Disable automatic FRITZ!OS
updates after installation because a new `aha` build will fail the runtime
fingerprint check until it has been analysed and supported explicitly.

You need:

- an x86-64 Linux environment with Git and the normal Freetz-NG build
  prerequisites. This can be native Linux or Ubuntu under WSL2 on Windows;
- the dedicated FRITZ!Box 7530;
- an MQTT broker reachable by both the box and Home Assistant;
- Home Assistant with its MQTT integration.

The first installation is easiest while the dedicated 7530 is connected
directly to the build computer. Configure IP-client and optional Mesh operation
afterwards in the normal FRITZ!OS interface. The production Mesh master remains
unmodified; the provider can enter virtual endpoints through the repeater's
existing Smart Home/Mesh path. Pairing the 440 directly with the bridge box is
the intended low-latency topology but still requires its final qualification.

[Freetz-NG documents its build hosts, dependencies and installation
methods](https://freetz-ng.github.io/freetz-ng/wiki/10_Beginner/install.en/).
By default, the script does not run `sudo` or install host packages. Missing
host tools such as `bison` or `flex` are reported by the Freetz prerequisite
check. The optional `--install-prerequisites` mode delegates their installation
to Freetz-NG and may request the user's `sudo` password.

## Choose an installation environment

### Native x86-64 Linux

Ubuntu, Debian and other Freetz-NG build hosts can run the project script
directly. Keep the computer connected to the Internet while building, then
connect its Ethernet adapter directly to the dedicated 7530 for the first
installation.

### Windows with WSL2 and Ubuntu — tested

The original laboratory bridge was prepared from Windows using x86-64 WSL2 and
Ubuntu. This is therefore a practically tested installation path for this
project even though upstream Freetz-NG treats WSL installations as potentially
problematic in general.

Open PowerShell as Administrator and install or update WSL2:

```powershell
wsl --install -d Ubuntu
wsl --update
```

Restart Windows if requested, open Ubuntu, and keep the repository in the Linux
home directory rather than under `/mnt/c`:

```sh
sudo apt update
sudo apt install --yes git
mkdir -p ~/src
cd ~/src
git clone https://github.com/Allcrafter1/tentahacle-virtual-device-bridge.git
cd tentahacle-virtual-device-bridge
uname -m
```

`uname -m` must print `x86_64`. Connect the dedicated 7530 directly by Ethernet.
If Windows does not retain an address while the box reboots, assign
`192.168.178.2` with subnet mask `255.255.255.0` temporarily to that Windows
Ethernet adapter. A firewall or VPN that intercepts FTP can prevent access to
the short-lived EVA bootloader; pause it for this isolated direct connection if
the script cannot reach `192.168.178.1`.

Then use the same guided build command as on native Linux:

```sh
./tools/build-firmware.sh --install-prerequisites --flash
```

### Codex-assisted installation — tested with WSL2

[Codex can run inside WSL2](https://learn.chatgpt.com/docs/windows/wsl). Open
Codex in the cloned repository and paste the following prompt. Replace the
bracketed values when needed.

```text
Set up TentAHAcle - Virtual Device Bridge from this repository on my separate laboratory
FRITZ!Box 7530. Work independently and carry the setup through as far as the
local environment allows. Inspect the repository instructions and current
state first. Verify that this is x86-64 Linux or WSL2, install only the required
build prerequisites, and use the repository's pinned build script.

Target hardware: classic FRITZ!Box 7530, HWRevision 236.
Required target firmware: German FRITZ!OS 8.25.
Bootloader address: 192.168.178.1.

This is a dedicated bridge box, not my production Internet router. Do not
modify any other FRITZ!Box. Do not download, publish or commit a prebuilt AVM
or modified firmware image. Build it locally as documented. Before flashing,
check the model and firmware with me, make sure I have exported the stock
FRITZ!Box configuration, and show me the exact locally built image. Then start
the repository's interactive first-install procedure. Ask me only for physical
actions you cannot perform, such as connecting Ethernet, removing or applying
power, and confirming the actual flash operation.

After the box starts, guide me through the normal FRITZ!OS and Freetz web
interfaces: IP-client setup, disabling automatic FRITZ!OS updates, securing
Freetz, and configuring the MQTT broker. My broker host is [HOME_ASSISTANT_IP],
port 1883, and I will enter the dedicated MQTT username and password myself.
Then guide me through adding the repository to HACS, installing the Home
Assistant integration, creating one test virtual device, and assigning it in
FRITZ!OS. Verify each completed step and record any deviation from the
documented compatibility baseline.
```

Codex can inspect, build and run the installer, but the user must still perform
the physical power cycle and make the final flash decision. If the box does not
match HW236 and FRITZ!OS 8.25, stop before flashing.

## Guided local build and first installation

From the repository directory on native Linux or WSL2, run:

```sh
./tools/build-firmware.sh --flash
```

If the machine does not yet have the Freetz-NG build packages, use the guided
prerequisite installation as part of the same run:

```sh
./tools/build-firmware.sh --install-prerequisites --flash
```

Freetz-NG shows the distribution packages first and requests `sudo` only when
the host actually needs packages.

The script checks out the pinned Freetz-NG revision below `.build`, installs
the source package, selects the validated 7530 profile and builds locally. It
then starts Freetz-NG's interactive `push_firmware` installer. Connect only the
dedicated 7530 to the computer by Ethernet, use the default bootloader address
`192.168.178.1`, and follow the power-cycle and confirmation prompts. If the
computer does not retain an address on that subnet while the box reboots, set a
temporary static address such as `192.168.178.2/24` on its Ethernet adapter.

The flashing step is never started by a normal build. Omitting `--flash`
produces the image only. A non-default bootloader address can be selected with
`--box-ip ADDRESS`.

Neither this repository nor its release downloads offers a ready-made firmware
image. The resulting image under `.build/freetz-ng/images` is for the person
who built it and must not be redistributed.

To inspect or add ordinary Freetz options before compiling:

```sh
./tools/build-firmware.sh --menuconfig
```

Advanced users can add the package to an existing checkout without replacing
its `.config`:

```sh
./tools/install-freetz-package.sh /path/to/freetz-ng
```

Then enable **Packages → T → TentAHAcle - Virtual Device Bridge** in `make menuconfig`.

## First setup after flashing

1. Before flashing, verify **FRITZ!Box 7530 (HW236)** and **FRITZ!OS 8.25** in
   the stock interface, set an administrator password and export the box
   configuration. The compatibility guard will reject every other `aha`
   binary, but model verification still belongs before the write.
2. After the modified image boots, open the normal FRITZ!OS interface. Configure
   the bridge box as an IP client and, when needed, as a Mesh repeater. Give it
   a stable address or DHCP reservation. This network-specific step stays in
   FRITZ!OS; the installer does not change the computer's or box's normal LAN
   configuration. Under **System → Update → Auto-Update**, select the option
   that only informs you about new FRITZ!OS versions. If the bridge remains in
   the Mesh, also disable adoption of the Mesh Master's settings under **Home
   Network → Mesh → Mesh Settings**. Do not start an update for this box from
   the Mesh overview until that firmware has been validated by this project.
3. Open the Freetz web interface on port `81`, secure its administration access,
   then open **Packages → TentAHAcle - Virtual Device Bridge**. No SSH setup is required.
4. Enter the existing MQTT broker address, credentials and a unique bridge ID
   containing 3–32 lowercase letters, digits, `_` or `-`. Use the LAN IP or
   hostname that the FRITZ!Box can reach; Home Assistant's internal hostname
   `core-mosquitto` is not normally reachable from the box. Give this MQTT user
   read/write access only to `tentahacle/<bridge-id>/#` when the broker
   supports per-topic ACLs.
5. Enable the service and apply the Freetz configuration. Freetz stores the
   values persistently and restarts the package; no runtime file needs to be
   edited. Its fingerprint guard
   refuses an unsupported `/usr/bin/aha` instead of preloading code into an
   unknown build. The MQTT broker should now contain retained `bridge/info` and
   `bridge/availability` messages.
6. In HACS open **Custom repositories**, add
   `https://github.com/Allcrafter1/tentahacle-virtual-device-bridge` as category
   **Integration**, install **TentAHAcle - Virtual Device Bridge**, and restart Home
   Assistant. A manual copy of `custom_components/tentahacle` is the
   development alternative.
7. Home Assistant discovers the retained bridge announcement and creates its
   integration entry automatically. Open it in **Settings → Devices &
   services**, then use the `+` action labelled **Add virtual FRITZ! device**.
   Manual setup uses the same bridge ID and the default MQTT topic root
   `tentahacle`.
8. Select one HA entity per virtual device. The integration infers the narrowest
   matching profile; a color-capable light may deliberately be reduced to a
   dimmable light. The color profile automatically enables Hue/Saturation when
   the HA light supports `hs`, `xy`, `rgb`, `rgbw` or `rgbww`; otherwise it
   provides color temperature only. The final wizard page and a persistent Home
   Assistant notification both link directly to FRITZ!OS. Assign or configure
   the resulting device there.

A 7530 that already runs Freetz can normally be updated by building without
`--flash` and uploading the locally produced image through the Freetz web
interface. The upstream [Freetz-NG installation documentation](https://freetz-ng.github.io/freetz-ng/INSTALL/)
remains authoritative for recovery and unusual first-install situations.

The bridge permits one persistent virtual device per Home Assistant entity.
One FRITZ device can be placed on several 440 controllers, so duplicate
mappings are unnecessary. Reconfiguring a mapping can bind a replacement HA
entity or change the FRITZ name without changing its FRITZ identity. A device
type change creates a new mapping and requires a new 440 assignment.

One bridge currently supports at most **32 virtual devices**. This is a
conservative implementation limit, not a proven FRITZ!OS or `aha` limit.
Home Assistant is the
lifecycle authority: create, rename, rebind and delete devices there. Deleting
a mapping removes its virtual device from the native AHA registry as well. The
virtual parent reports itself as non-deletable in FRITZ!OS so that FRITZ!OS
cannot silently diverge from Home Assistant. If the complete Home Assistant
bridge entry is removed, the integration restores active thermostat overrides
where possible and requests an empty native registry. Do this while the source
entities and MQTT broker are reachable. If cleanup fails, Home Assistant logs
the error and retains its private recovery state; an affected thermostat or a
stale native endpoint may then require manual cleanup.

FRITZ!OS remains the intended place to assign a virtual device to one or more
440 controllers, choose its display position/name, and include it in native
templates, scenes and routines. Those native automation functions are not
duplicated by the bridge.

## Profile behavior and current boundaries

- **Switch/socket:** `switch`, `input_boolean`, and reduced on/off lights expose
  a confirmed on/off roundtrip.
- **Lights:** dimmable lights add brightness. The color profile adds color
  temperature where supported and Hue/Saturation for HA color modes `hs`,
  `xy`, `rgb`, `rgbw` and `rgbww`.
- **Covers:** every selectable source must support both opening and closing,
  directly or through numeric positioning. Intermediate positions require
  `SET_POSITION`; stop requires `STOP`. A binary cover reports confirmed 0/100
  endpoints but no invented position while it is moving.
- **Thermostats:** the bridged set is target temperature from 8–28 °C, Heat/Off,
  exact source MAX, current temperature, Boost/cold timers and optional schedule
  display. Celsius and Fahrenheit HA configurations are converted. A source
  value above 28 °C that is not exactly MAX has no native FRITZ representation.

The virtual blind parent can appear cosmetically as a light/LED in FRITZ!OS,
and FRITZ!OS can warn that blind end positions are not calibrated. The bridge
does not invent calibration data for hardware that does not exist. Likewise,
an already open 440 page does not always refresh immediately; reopen it to see
the latest confirmed state. This display behavior was also reproduced with an
original thermostat.

The command/state paths above have automated coverage. Before broad deployment,
the current release candidate still needs focused physical checks for repeated
warm/cold and color changes, binary and positionable covers, complete
thermostat timer/restart behavior, and execution of one harmless native
template, scene and routine.

## Pair the FRITZ!Smart Control 440 directly with the bridge

For low latency, the 440 must be registered directly with the bridge 7530. The
7530 can remain a LAN IP client and even a Mesh repeater; the button command is
still handled by its local `aha` first. Mesh is not required by the bridge. For
maximum separation from the production router, remove the 7530 from the Mesh
after its IP-client setup.

Move the controller only after the package service, MQTT and at least one
virtual device work:

1. Back up both boxes as described below.
2. On the 7530 open **Smart Home → Devices and Groups → Register Device**.
3. Factory-reset the 440 from its device menu, then select **Start
   registration** on the controller. Exact icons can vary with controller
   firmware.
4. Assign the virtual device to a display position in the 7530 interface and
   test command and confirmed state in both directions.
5. Delete the old disconnected 440 entry on the production box only after the
   new direct path has passed the test.

Display assignments stored on the production box are not migrated to the
7530. Its configuration export is a rollback aid, not a migration mechanism
for the 440 layout.

FRITZ documents the [440 factory-reset procedure](https://fritz.com/apps/knowledge-base/FRITZ-Box-7590/3722_Werkseinstellungen-des-FRITZ-Tasters-laden/)
and subsequent registration in its knowledge base.

Do not keep an older MQTT automation active for the same virtual endpoint.
During migration, validate the new integration with a new device first, then
disable the corresponding old automation. Existing helpers and automations can
be removed only after their consumers have been checked and the new path has
survived a Home Assistant restart.

For a Zigbee2MQTT thermostat, the integration automatically enables the
schedule shadow when the climate entity has sibling entities named
`select.<name>_week`, `text.<name>_workdays_schedule`, and
`text.<name>_holidays_schedule`. Other thermostats retain setpoint, Heat/Off,
MAX, current temperature and Boost/cold timers without schedule mirroring.

Home Assistant remains authoritative for the virtual thermostat. Do not edit
its schedule or other thermostat configuration in FRITZ!OS or the FRITZ!App.
Comfort/reduced settings, offset, locks, external-sensor selection, adaptive
heating, holiday/summer and window functions are not bridge features. The
displayed battery state is synthetic and always healthy/100%, because the
virtual endpoint has no battery.

The bridge uses the central MQTT broker; it does not run another broker on the
FRITZ!Box. The small `mosquitto_pub` diagnostic client is included because
Freetz-NG otherwise forces its broker package when selecting `libmosquitto`.
Home Assistant uses the credentials already stored by its MQTT
integration. The FRITZ!Box is a separate MQTT client: enter its broker account
under **Freetz → Packages → TentAHAcle - Virtual Device Bridge**. Freetz writes those values
to the root-only runtime file `/var/run/tentahacle/mqtt.conf` (mode `0600`).

With Home Assistant's official Mosquitto app, the recommended simple path is a
dedicated broker-local login:

1. Open **Settings → Apps → Mosquitto broker → Configuration**.
2. Add a login that is used only by this bridge:

   ```yaml
   logins:
     - username: tentahacle
       password: choose-a-long-unique-password
   ```

3. Save and restart the Mosquitto app.
4. Enter the Home Assistant machine's LAN address, port `1883`, that username
   and that password under **Freetz → Packages → TentAHAcle - Virtual Device Bridge**.

The Mosquitto app also accepts dedicated Home Assistant users. A broker-local
login is recommended here because it is visibly scoped to MQTT and does not
create another Home Assistant login. Advanced users can additionally apply a
topic ACL for `tentahacle/<bridge-id>/#`. An empty user and password work only
with brokers that permit anonymous clients; the official Mosquitto app does
not.

## Back up the bridge

Create two backups before every firmware change and after substantial device
configuration:

1. Create a password-protected export under **FRITZ!OS → System → Backup →
   Save**. It contains the AVM configuration, including Smart Home settings.
2. Create an encrypted backup under **Freetz → System → Backup & Restore**. It
   also contains the persistent TentAHAcle - Virtual Device Bridge device registry and MQTT
   configuration. Treat it as sensitive because it contains credentials, and
   store its password separately.

After restoring a FRITZ!Box export, DECT devices can occasionally require radio
registration again even though their configuration was retained. In that case,
use extended registration and the device button or menu instead of deleting
the retained entry and starting from scratch. A Freetz backup belongs only to
the bridge box for which it was created.
This follows FRITZ's documented
[restore flow for Smart Home and DECT devices](https://fritz.com/apps/knowledge-base/FRITZ-Box-7530/4_Einstellungen-der-FRITZ-Box-sichern-und-wiederherstellen/).

## Updating

Automatic FRITZ!OS updates must remain disabled on the bridge box. Every new
firmware can change the private `aha` ABI and is unsupported until its binary
has been analysed and explicitly allowlisted. Updating this project means
building a new local image from the supported version and applying it through
Freetz. The persistent registry under `/tmp/flash/tentahacle/registry.json`
keeps device identities across ordinary restarts and compatible image updates
and is included in the Freetz backup; create both backups before every firmware
change.

Home Assistant integration updates follow the normal HACS flow. The bridge
replays all desired mappings whenever its MQTT connection returns, and the HA
side does the same when it sees the bridge come online, so startup order does
not matter.

### Clean transition from a 0.1.x test installation

The former `fritz_virtual_bridge`/`FVB` generation is not an in-place upgrade.
Its Home Assistant domain, MQTT namespace, persistent path and device UID
prefix differ, and changing an `FVB...` UID to `TVB...` does not preserve the
native parent identity. There is currently no supported importer.

Before the transition, create both backups above and record the existing 440
layout. While the old bridge is still healthy, remove its mappings through the
old Home Assistant integration so it can remove the corresponding native
devices. Then disable the old config entry; do not begin by deleting the whole
entry, because its removal callback may issue authoritative cleanup. Install
both new halves, create the TentAHAcle bridge and devices as new identities,
and assign them on one 440 before moving further controllers. Keep the backups
and the disabled old entry until the new setup has survived the physical and
recovery checklist. Never copy the old registry into the new path: an empty or
partially configured new HA entry performs an authoritative prune at startup.

## Diagnostics

On the bridge, the Freetz service status and these two logs are the first
places to check:

```text
/var/log/tentahacle-aha.log
/var/log/tentahacle-mqtt.log
```

The retained `bridge/info` document reports `ready`, the registry revision and
supported profiles. `online` with `ready: false` means MQTT works but the local
provider has not finished connecting or reprovisioning. A fingerprint error in
the service log means the installed FRITZ!OS build is deliberately unsupported.

For a command or state roundtrip problem, enable **Debug logging** in the
TentAHAcle Freetz service, restart that service, reproduce exactly one action
and save both log excerpts with their timestamps. The AHA log then records the
decoded native command, endpoint UID, sequence and normalized value; the MQTT
log records command forwarding with the same UID and sequence. State feedback
does not carry that command sequence; correlate it through UID, value and the
nearby monotonic timestamp instead.
Enable debug logging for `custom_components.tentahacle` in Home Assistant as
well when the problem crosses into an HA entity. Turn all debug logging off
again after the capture. Never attach the generated MQTT configuration file or
paste broker credentials into an issue.

## Rollback

Disable the service in Freetz. The stop action terminates only the bridge MQTT
process and the modified `aha` instance, then starts stock `aha` again without
`LD_PRELOAD`. Existing virtual entries may remain unavailable in FRITZ!OS;
they can be removed separately after checking 440 assignments. Reflashing an
unmodified image removes the package completely.
