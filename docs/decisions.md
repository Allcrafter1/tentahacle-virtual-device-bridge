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
5. **Conservative first compatibility target.** Public beta supports only the
   exact validated 7530/8.25 `aha` fingerprint.
6. **FRITZ!OS remains the 440 layout editor.** Home Assistant creates and maps
   persistent virtual devices; screen positions and short display labels are
   assigned in FRITZ!OS.
7. **HA removal deletes the virtual FRITZ device.** A removed mapping is also
   deleted through the local native AHA path and from the bridge registry. This
   keeps HA and FRITZ!OS consistent; the UI deletion confirmation is the guard.
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

Changing identity or lifecycle semantics after users have configured several
controllers would be a breaking migration. Further schedule adapters remain
later, explicit additions.
