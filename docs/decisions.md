# Product decisions

The current choices are:

1. **One HA bridge entry with device subentries.** This gives one connection
   setup and a native add/reconfigure/remove flow for every mapping.
2. **Custom integration plus Freetz package.** An HA add-on would need its own
   UI and API authentication and would exclude HA Container/Core users.
3. **Dual MIT OR Apache-2.0 repository.** Both choices are permissive. The MIT
   option keeps inclusion in the GPL-2.0-only Freetz-NG build compatible; the
   Apache-2.0 option adds an explicit patent grant for other uses.
4. **Source-only firmware distribution.** GitHub never hosts AVM or modified
   firmware images. A pinned build script produces the image locally.
5. **Conservative first compatibility target.** The initial target supports only the
   exact validated 7530/8.25 `aha` fingerprint.
6. **FRITZ!OS remains the 440 layout editor.** Home Assistant creates and maps
   persistent virtual devices; screen positions and short display labels are
   assigned in FRITZ!OS.
7. **HA removal deletes the virtual FRITZ device.** A removed mapping is also
   deleted through the local native AHA path and from the bridge registry. This
   keeps HA and FRITZ!OS consistent. The virtual native parent advertises
   `IsDeleteSupported=0`, so FRITZ!OS cannot independently delete HA-owned
   endpoints. Removing the whole HA hub restores active thermostat overrides
   and requests an empty authoritative device set before deleting its private
   recovery state. This whole-hub cleanup is best-effort when MQTT or a source
   entity is unavailable; failures retain recovery state and are logged.
8. **The first schedule adapter targets the validated Zigbee2MQTT 5+2 format.**
   Other climate devices work without schedule mirroring until an adapter is
   implemented for their data model.
9. **Normal configuration stays in web interfaces.** The one-time script runs
   on native x86-64 Linux or the tested Windows/WSL2 path and interactively
   installs the locally built image. Afterwards FRITZ!OS owns network/Mesh
   setup, Freetz owns MQTT credentials and Home Assistant owns entity mappings;
   SSH and manual configuration files are development tools.
10. **Keep the native Home Assistant subentry UI.** The bridge entry explains
    the icon-only `+` action. The final wizard page, persistent notification and
    bridge device all link to FRITZ!OS. A separate frontend panel is deferred
    until its maintenance cost solves a demonstrated workflow problem.
11. **One persistent virtual device per mapped HA entity.** A stable endpoint
    can be assigned to several 440s and keeps one coherent state. Rebinding an
    entity preserves that identity; changing profile creates a new device.
    The bridge currently enforces a conservative implementation limit of 64
    devices through matching bounded C arrays and a Home Assistant guard. This
    is not claimed as a native FRITZ!OS or `aha` maximum; a higher capacity
    requires a coordinated rebuild plus a dedicated persistence and runtime
    lab test for devices 33 onward.
12. **FRITZ!OS owns presentation and native automation.** FRITZ!OS remains the
    editor for 440 pages, short labels, templates, scenarios and routines. The
    bridge supplies native-compatible endpoints but does not duplicate those
    editors in HA.
13. **Capabilities are derived, not manually emulated.** The color-light
    profile adds Hue/Saturation only when the HA light supports a chromatic
    mode. Covers must implement both endpoints; stop and intermediate position
    remain optional. Climate sources must cover the exact heat/off and
    half-degree range required by the virtual thermostat.
14. **Home Assistant state is periodically authoritative.** On connection and
    once per minute HA replays prune/upsert, confirmed state, schedule and timer
    data. This bounded traffic is preferred to leaving a one-off persistence or
    provider failure unrepaired until restart.
15. **Recovery requires both provider and MQTT transport.** Stock `aha` alone
    is a temporary fallback, not a healthy state on the dedicated bridge box.
    The watchdog retries the virtual provider and separately restarts a dead
    MQTT process. Direct helpers may be aborted during shutdown, but an active
    bounded `modsave` is allowed to complete to avoid flash corruption.

Changing identity or lifecycle semantics after users have configured several
controllers would be a breaking migration. Further schedule adapters remain
later, explicit additions.
