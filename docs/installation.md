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

> [!WARNING]
> Version 0.2.0-beta.1 is a clean break from the former 0.1.x test builds. It
> deliberately uses new Home Assistant, MQTT, Freetz, filesystem and virtual
> device identifiers. There is no automatic migration: remove the old test
> integration and bridge package, install this beta fresh, and recreate the
> virtual devices and their controller assignments. Back up the box first if
> you need to preserve the old laboratory state.

If the FRITZ!Box modification is the part keeping you from trying the project,
open a [GitHub issue](https://github.com/Allcrafter1/tentahacle-virtual-device-bridge/issues).
The maintainer is willing to work through a compatible setup with users and
turn recurring difficulties into a simpler documented process. Do not attach
passwords, configuration exports, serial numbers or other private device data.

The first public target is deliberately narrow: classic FRITZ!Box 7530
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
   matching profile; a color-temperature light may deliberately be reduced to
   a dimmable light. The final wizard page and a persistent Home Assistant
   notification both link directly to FRITZ!OS. Assign or configure the
   resulting device there.

A 7530 that already runs Freetz can normally be updated by building without
`--flash` and uploading the locally produced image through the Freetz web
interface. The upstream [Freetz-NG installation documentation](https://freetz-ng.github.io/freetz-ng/INSTALL/)
remains authoritative for recovery and unusual first-install situations.

The bridge permits one persistent virtual device per Home Assistant entity.
One FRITZ device can be placed on several 440 controllers, so duplicate
mappings are unnecessary. Reconfiguring a mapping can bind a replacement HA
entity or change the FRITZ name without changing its FRITZ identity. A device
type change creates a new mapping and requires a new 440 assignment.

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
`text.<name>_holidays_schedule`. Other thermostats retain all ordinary and
timed controls without schedule mirroring.

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

## Rollback

Disable the service in Freetz. The stop action terminates only the bridge MQTT
process and the modified `aha` instance, then starts stock `aha` again without
`LD_PRELOAD`. Existing virtual entries may remain unavailable in FRITZ!OS;
they can be removed separately after checking 440 assignments. Reflashing an
unmodified image removes the package completely.
