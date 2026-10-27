# Architecture and user workflow

## Components

The system has three deliberately separate layers.

### Freetz-NG package

The package runs on a dedicated supported FRITZ!Box and contains only project
code:

- a small provider loaded into the existing `aha` process,
- a separate MQTT and persistence process,
- a local control client,
- a Freetz init script and configuration page.

The provider reuses the original FRITZ!OS Smart Home and Mesh stack. It does
not implement DECT radio and does not replace an AVM executable. The init
script starts `aha` with the provider only after verifying the exact supported
binary fingerprint. Stopping the package restarts the original process
without the provider.

A package restart replaces the virtual process directly, without briefly
starting a stock instance in between. After restoring devices, the MQTT worker
checks each enabled target through the native AHA API and resubmits its saved
440 widgets unchanged. This addresses widgets disappearing from the physical
display even though their assignments remain saved. Matching uses the stable
native identifier, since numeric child IDs can change after restart. The native
setter also rebuilds other widgets on the same page from their numeric IDs.
Consequently, every target on that page must resolve consistently before a
write; otherwise an unresolved neighbour could lose its saved identifier.
Targets on other ready pages can still proceed. Each target has at most 45
attempts, two seconds apart or longer when sharing the worker with other
targets. This leaves about 90 seconds even with a single target. A successful
target is not resubmitted again during that
reconciliation. Native submission is not a DECT delivery acknowledgement.

The native AHA helper runs without the MQTT worker's Freetz `LD_LIBRARY_PATH`,
so that its loader and AVM libraries use the same runtime. Both deletion and
widget restoration use this environment and a bounded child-process deadline.
Delete/resync helpers may be cancelled during shutdown. A running
`modsave flash` is deliberately allowed to finish for up to 30 seconds; the
init script consequently waits up to 40 seconds before declaring bridge stop a
failure instead of killing a flash write midway.

The service watchdog treats both halves as required. It restarts a crashed
MQTT bridge while the provider is healthy, and it keeps retrying virtual AHA
startup after an initial provider failure instead of accepting a permanently
running stock `aha` as recovery. Manual starts do not create duplicate watchdog
workers. Both package logs are checked once per minute and truncated at 1 MiB
to bound `/var`/tmpfs use during persistent authentication or provider errors.

### Home Assistant custom integration

One config entry represents one TentAHAcle - Virtual Device Bridge. Every mapped Home
Assistant entity is stored as a config subentry. This gives the user normal UI
flows to add, rename, reconfigure and remove mappings without YAML or a second
web interface.

The integration does not create a second controllable copy of the selected
light, switch, cover or climate entity. It translates between the stable
virtual-device endpoint and the original entity's native Home Assistant
actions and state attributes.

### MQTT transport

MQTT carries management, FRITZ-originated commands and confirmed Home
Assistant state. Messages are local, versioned and independent of Home
Assistant's internal storage format. Retained state and a complete
Home-Assistant-owned replay on bridge connection and once per minute allow
both sides to recover after restart, a rejected registry write or temporary
provider failure in either startup order.

## Native source layout and ownership

The C provider is split by responsibility. A button press still takes the same
path: the native `send()` hook recognizes a complete AHA command, updates the
selected device under a mutex, and puts a value-only event in a bounded queue.
The worker takes that snapshot and sends JSON to the separate MQTT bridge.
Confirmed HA state returns through the local control socket and the profile
encoder constructs the original AHA status packets.

| Source | Responsibility |
|---|---|
| `virtual_provider.c` | Interposition hooks, transport discovery, immutable identity substitution, device selection, native command handling and local control worker |
| `provider_state.h` | One independent set of values for each device, plus the legacy self-test state |
| `provider_profiles.c` | Exact switch, light, cover and thermostat packet layouts; explicit encoder context and output callback |
| `provider_codec.h` | Unaligned-safe network-order numbers and complete-batch framing checks |
| `provider_transport.c` | Finish short frame writes and retry interrupted writes through the original native function |
| `provider_events.c` | Bounded event queue and checked JSON formatting outside the provider mutex |
| `control_parse.h` | Bounded decimal timer/schedule parsing shared with the MQTT bridge, independent of 32/64-bit `unsigned long` |
| `device_registry.c`, `registry_store.c` | Identity validation/allocation and atomic persistence, respectively |

Device selection changes a pointer to the device's own values. It no longer
copies dozens of globals into and out of a shared scratch space. Selection,
registry changes, encoder use and queue operations all require the same mutex.
The worker copies an event before releasing that mutex, so no borrowed device
pointer escapes into JSON delivery. Profile helpers are private symbols; only
the existing libc hooks interpose on the native process.

Startup creates a nonblocking wake pipe and a private listener before enabling
the hooks. An invalid socket path, failed listener or failed worker creation
leaves the provider inactive and releases the resources it acquired. Discovery
slots are reclaimed when either socketpair endpoint closes, so reconnects do
not permanently exhaust the fixed table. Socket type flags are independent of
the stream type. Closed WATCH clients are retired before accepting another
client, even when no device event arrives to reveal the disconnection.

### Native review boundaries

The refactor keeps the reverse-engineered wire constants, identities, control
commands and MQTT schema. `tests/fixtures/provider-v0.1.4-dynamic.hex` contains
the complete sequence of frames observed by the dynamic integration test using
the original provider at commit `99bd3f2`. The test compares every byte after
zeroing only the clock-derived Function 95/98 and thermostat activation fields.
This includes all supported profiles, state isolation, restore gaps, native
commands, mixed batches and confirmed state feedback. The legacy integration
test remains separate.

Fault tests also cover queue overflow order, undersized JSON buffers, numeric
overflow, startup descriptor ownership, interrupted/short/zero-progress writes
and errors from closing a persistent file. Atomic save never replaces the old
file on those failures and never retries `close()` on a possibly reused fd.

The persistence reader opens one non-symlink descriptor, checks that descriptor
with `fstat`, and reads it with an explicit size limit. It rejects FIFOs without
blocking, truncated/growing files, embedded NUL bytes, decoded JSON NUL escapes,
and trailing non-JSON text. Literal backslash text remains valid. Allocation
fault tests exercise construction and cleanup of both persisted and published
JSON, including children not yet attached to their parent object.

The control CLI handles interrupted/short stdout writes and rejects truncated
SEQPACKET replies or EOF before a reply. Provider control sockets close on exec;
missing native function symbols leave the provider inactive. The MQTT process
checks authentication/will/loop startup errors and validates WATCH acceptance.
Native deletion and flash persistence have monotonic four- and thirty-second
child-process deadlines. Shutdown aborts direct native helpers but never kills
an in-progress flash persistence child. The cache tests cover every supported
profile/property combination and prove that deletion compaction keeps feedback
attached to its original UID. Hue/saturation feedback uses bounded decimal
parsing rather than `scanf`, including exact-tail and overflow checks for MQTT
input.

### Compiler and loader assumptions

Freetz can supply `-Ofast`, which otherwise permits the compiler to assume that
NaN and infinity never occur. The package appends `-fno-fast-math` and
`-fno-finite-math-only` after those inherited flags, preserving validation before
floating-point-to-integer conversions. Optimized tests explicitly include NaN
and infinity. Package builds also use strict warnings, a strong stack protector,
full RELRO/eager binding and undefined-symbol link checks.

The ARM shared object was checked against exports from the original AVM
libraries, including `__stack_chk_fail` in libc and `__stack_chk_guard` in the
loader. The pinned uClibc headers explicitly disable `_FORTIFY_SOURCE`; this
build therefore does not claim FORTIFY coverage. Loader symbol checks are an
offline compatibility check, not a substitute for a dedicated-box test.

Remaining operating limits are explicit:

- The event ring retains the newest 31 pending commands and drops the oldest
  on overflow, matching the existing policy. It is bounded, not a durable queue.
- Native output holds the provider mutex to keep frame order. Short positive
  writes and `EINTR` are completed; zero progress, `EAGAIN` and other errors fail
  immediately. A fatal failure after a stream prefix cannot retract those bytes.
  This is not a nonblocking or real-time transport redesign.
- A slow local control client can occupy the worker for its existing one-second
  receive timeout. Watcher sends remain nonblocking; slow watchers disconnect.
- The provider is process-lifetime code: stopping the package restarts `aha`.
  Unloading an active provider with `dlclose()` is unsupported.
- Directory `fsync` after the registry rename remains best-effort. Power-loss
  durability depends on the filesystem and the separate Freetz flash-persist
  step; a successful in-memory/native test does not prove flash durability.
- Helper deadlines supervise the direct child. Arbitrary configured commands
  that daemonize grandchildren are outside that contract. Native device deletion
  and registry persistence also remain separate operations without transactional
  rollback across AVM and the filesystem.
- Offline golden tests and host sanitizers do not prove undocumented AVM thread
  behavior, radio synchronization or 440 pages. A dedicated-box smoke test is
  still required before deploying the refactor or expanding firmware support.

## Device identity and lifecycle

Each mapping receives an immutable random 64-bit identifier. Its FRITZ UID is
`TVB` followed by sixteen hexadecimal characters, which fits the native
19-character field. The following values are allocated once and persisted on
the bridge:

- FRITZ UID,
- provider remote ID,
- device profile,
- HAN-FUN IPUI/unit discriminator where applicable,
- user-visible device name,
- enabled/disabled state.

Renaming or binding a replacement Home Assistant entity keeps the FRITZ UID
and all radio-side identities. Existing 440 assignments therefore survive.
Changing the device profile creates a new virtual device because FRITZ!OS
capabilities and widget layouts are type-specific.

Removing a mapping is authoritative: the bridge calls the local native AHA
deletion path, removes the provider endpoint and then removes the persistent
registry entry. Existing controller assignments for that virtual device are
therefore removed with it. Provider IDs are never reused. The virtual native
parent reports `IsDeleteSupported=0`, so FRITZ!OS cannot independently remove
HA-owned endpoints. Removing the whole HA bridge entry first restores any
active thermostat override and then publishes an empty authoritative device
set. If that best-effort cleanup cannot reach a source entity or MQTT, Home
Assistant logs the removal error and preserves the private recovery store;
the user may still need to restore the affected thermostat or remove stale
native endpoints manually.

## Management protocol

The initial public protocol uses schema version 1 beneath
`tentahacle/<bridge_id>`:

| Topic | Direction | Purpose |
|---|---|---|
| `bridge/availability` | bridge -> HA | retained online/offline status |
| `bridge/info` | bridge -> HA | retained version, capabilities and compatibility |
| `management/request` | HA -> bridge | versioned upsert/rename/prune request |
| `management/response/<request_id>` | bridge -> HA | correlated accepted/error response |
| `device/<uid>/command` | bridge -> HA | commands from FRITZ!OS or the 440 |
| `device/<uid>/<property>/set` | HA -> bridge | confirmed HA state and configuration |

Management JSON is validated and persisted by the separate MQTT process. The
provider inside `aha` receives only a bounded, validated local snapshot and
never parses MQTT credentials or writes persistent files.

Home Assistant sends prune-before-upsert and a final prune on every complete
replay. The same complete mapping, confirmed-state, schedule and timer snapshot
is repeated once per minute. This makes registry-save failures, provider-pending
upserts and stale retained timer feedback self-healing without duplicating
devices; unchanged operations are idempotent and do not rewrite the registry.

Provider restoration carries each persistent remote ID explicitly. Replaying
only device names in insertion order is insufficient after deletions: the
durable allocator retains gaps while an empty provider starts at 456.

For the pinned local 7530 receiver, Function 98's opaque interface array uses
little-endian words; the surrounding network fields remain big-endian. The
local ETSI handler performs another byte reversal on this array. Incorrect
encoding can leave a visible parent device without a controllable unit.
Native unit creation must be checked independently of transport acceptance.

The exact version-1 messages, validation rules and recovery semantics are in
[MQTT protocol version 1](mqtt-protocol.md).

## Supported profiles

The release-candidate profiles are:

| HA source | Virtual FRITZ profile | Functions |
|---|---|---|
| `switch`, `input_boolean` | switch/socket | on/off |
| `light` | dimmable or color light | on/off, brightness, Kelvin when supported, Hue/Saturation for `hs`, `xy`, `rgb`, `rgbw` and `rgbww` sources |
| `cover` | blind | open/close directly or through endpoint positions; optional stop and intermediate position according to HA capabilities |
| `climate` | radiator thermostat | heat/off, 8–28 °C setpoint, native MAX, current temperature, Boost/cold timer and optional schedule shadow |

Full color uses the existing color-light profile. The integration derives a
persistent `color_full` capability from the HA source; FRITZ commands and
confirmed state use Hue 0–359 and saturation 0–255. Switching between Kelvin
and HS clears the inactive retained value so reconnect cannot replay an old
color mode. The roundtrip and mode changes are automated; final physical 440
qualification with several colors remains in the release checklist.

A cover is accepted only if it can implement both endpoints through OPEN/CLOSE
or numeric SET_POSITION. STOP and intermediate positions remain optional.
Binary covers confirm only 0/100 and intentionally do not invent an endpoint
while HA reports `opening` or `closing`.

Climate sources must support target temperature, heat and off, a range covering
8–28 °C and steps that can represent every half degree. HA-configured Celsius
or Fahrenheit is converted at the integration boundary. FRITZ MAX maps to the
source entity's `max_temp`; values between 28 °C and MAX have no ordinary
FRITZ representation and therefore leave the last representable retained
setpoint until the source returns to 8–28 °C or exact MAX. Current temperature
is sent in tenths of a degree, or as unknown when HA has no valid value. The
native battery display is synthetic healthy/100 %, not source telemetry.

The thermostat schedule adapter is optional. The first adapter supports the
validated Zigbee2MQTT `5+2` workday/holiday schedule entities. The internal
schedule model is generic so later adapters can support other thermostat
integrations without changing the FRITZ protocol.

For the standard Zigbee2MQTT entity naming scheme the integration detects the
`select.*_week`, `text.*_workdays_schedule` and
`text.*_holidays_schedule` siblings automatically. It publishes a rolling
current/next transition once per minute only while the real thermostat uses
the `schedule` preset and the week selector is `5+2`. Editing the virtual
schedule in FRITZ!OS is rejected by the provider and the HA-owned shadow is
restored.

Boost and cold overrides are kept in a private atomic Home Assistant store.
The first override records the previous temperature, HVAC mode and preset.
Extending or switching the override retains that original state. Expiry,
manual cancellation and a Home Assistant restart restore it without visible
input helpers. A later explicit source change cancels the override so expiry
cannot overwrite that change. Temporary missing/unavailable source state keeps
the snapshot. Deleting or rebinding a mapping restores the old source first;
whole-entry removal performs the same cleanup before pruning native endpoints.

FRITZ!OS thermostat editor fields outside this contract are not mirrored:
comfort/reduced configuration, offset, locks, external sensor selection,
adaptive heating, holiday/summer and window settings remain unsupported and
must not be edited on a virtual thermostat.

## User workflow

1. Install **TentAHAcle - Virtual Device Bridge**. Home Assistant creates an integration
   entry automatically from the retained MQTT bridge announcement and shows an
   onboarding notification explaining the otherwise icon-only add action.
2. Open the bridge entry and use the `+` action in the top-right corner. The
   following dialog is titled **Add virtual FRITZ! device**.
3. Select a source entity. The integration proposes the matching profile and
   capabilities; the user can reduce optional capabilities.
4. Choose a FRITZ device name. The immutable endpoint is created and confirmed
   by the bridge.
5. Follow the final wizard page's direct FRITZ!OS link and place or configure
   the device there. The safe base URL is extended with
   `#/smart-home/devices` so the link opens Smart Home directly, including
   configured port/proxy paths. The
   persistent completion notification and the bridge
   device named **Open the FRITZ!OS interface** remain permanent alternatives.
   Screen layout and its short display label remain FRITZ!OS responsibilities.
6. Reconfigure a mapping to bind a replacement HA entity while retaining the
   FRITZ identity and 440 assignments.

When a mapped thermostat has no recognized schedule adapter, setpoint, mode,
Boost and cold continue to work; only the native next-change schedule display
stays disabled.

Home Assistant owns virtual-device creation, renaming, rebinding and deletion.
The current bridge implementation conservatively limits each bridge to 64
devices. The value comes from the compile-time C registry/cache arrays
(`TVB_REGISTRY_CAPACITY`) and the matching Home Assistant guard
(`MAX_VIRTUAL_DEVICES`); no 5-bit or other 32-device protocol field has been
identified. Raising it further requires changing both sides together,
rebuilding the package and testing registry persistence and devices 65 onward
on the lab box. Automated persistence coverage exercises all 64 current slots;
FRITZ!OS runtime behavior above the former ceiling still needs lab qualification.
It is not a proven FRITZ!OS or `aha` limit. AVM separately documents up to 50
devices registered directly with this box, but also type-specific limits such
as 30 third-party HAN-FUN devices; those public radio-registration limits do
not prove how injected mixed-profile units behave at this ceiling. See
[AVM knowledge document 3729](https://fritz.com/de-lu/apps/knowledge-base/FRITZ-Box-7530/3729_Anzahl-und-Typ-der-Smart-Home-Gerate-die-an-FRITZ-Box-angemeldet-werden-konnen).
FRITZ!OS remains the editor for 440 screen placement and for native templates,
scenarios and routines. Those native automation features are intentionally not
recreated in the bridge.

## Known FRITZ!OS and 440 presentation limits

- A virtual blind's parent can be shown with an LED/lamp-style icon and
  FRITZ!OS can warn that end positions are not calibrated. The provider exposes
  the verified blind unit/interfaces; it does not fabricate undocumented
  calibration state merely to hide cosmetic warnings.
- An already open 440 page does not always refresh immediately after an
  external state change. The same behavior was reproduced with an original
  FRITZ!Thermo. Reopening the page must show the confirmed current state.
- Virtual devices can already be selected and saved in native templates and
  scenarios. Runtime activation per profile and one harmless routine remain
  physical release-qualification items. Cover availability as a routine trigger
  is governed by FRITZ!OS rather than the bridge.
- Long-press gestures are not exposed by the tested 440/FRITZ!OS assignment
  model. The project does not modify controller firmware to add them.

## Latency topology

The MQTT leg is local push. In the initial validation topology, however, the
440 is paired with a 6690 Mesh master while virtual endpoints originate on a
7530 Mesh repeater. Measured absolute dim commands all reached Home Assistant
with the correct value, but the Mesh path serialized them at roughly 6–7
seconds each. The confirmed Home Assistant state reached the FRITZ entity about
0.6 seconds after the source entity changed.

For interactive control, pair the controller directly with the dedicated
bridge box. The laboratory 440 now uses the 7530 directly. Captured commands
that reached MQTT were forwarded to Zigbee2MQTT within about 0–45 ms; in one
physical test Zigbee2MQTT reported the requested values about 180–210 ms after
the bridge command. These are segment measurements, not button-to-light
latency guarantees or proof of physical bulb response.

Direct-pairing investigation also found a separate provider defect fixed in
0.1.4: FRITZ!OS may combine several protocol frames in a single `send()`.
Requiring the first frame's length to equal the entire buffer silently bypassed
virtual command handling for such batches. The provider now validates the full
batch, processes its frames in order and preserves unrelated traffic. The
regression test fails against the old provider and passes against the fix.
This is independent of MQTT polling and does not require a faster HA poll rate.

## Safety and rollback

### HAN-FUN unit registration and local discovery

The injected provider stream first reaches the Mesh master. Its configuration
writeback to provider IDs is distinct from the client catalog, which uses the
master's native device IDs. Receiving a Function98 writeback therefore does
not prove that the local 7530 has imported the unit.

FRITZ!OS 8.25 has different interface-array decoding in its Function98 create
and update paths: creation expects BE DWORDs, whereas an existing unit consumes
native LE DWORDs. The bridge sends LE and BE payloads, in that order, inside one
416-byte outer message with one 64-bit microsecond revision. A new unit rejects
the first form before creation and accepts the second; an existing unit accepts
the first and rejects the equal-revision second form. This handles first setup,
reconnects and repeated announcements without an existence-check race. Ordinary
status and control messages keep their existing format.

After receiving master metadata matching the published UID, unit, name, type,
interfaces and revision, the provider queues the native client's normal Type5
catalog request. The request uses an observed cR/TX startup frame, socket pair
and handle. This completes local discovery when the master's incremental stream
contains status updates but omits the newly created unit description. It does
not restart AHA or rewrite controller pages. Requests are coalesced and deduplicated
by metadata and connection generation; a newer timestamp alone cannot create a
refresh loop. CONFIG_RSP imports do not trigger new requests.

The native relay can split a 216-byte Function98 into 168+48 bytes. A pass-through
converter hook observes the fully reassembled input, including in-place conversion,
and correlates only successful conversions with an output buffer and sufficient
capacity. Return values, output bytes and errno remain unchanged.

An ambiguous client, stale descriptor or partial native write prevents injection.
Capture, send and descriptor retirement share a lock; a failed send waits for a
new client connection instead of starting a retry timer. Host socket tests cover
correlation failures, deduplication, rename and descriptor reuse. Separate tests
cover partial writes and the synchronization state machine.

### Deployment safeguards

- Only explicitly fingerprinted FRITZ!OS/`aha` builds are accepted.
- Provider input lengths, IDs and enum values are bounded before use.
- MQTT credentials remain in root-readable Freetz configuration.
- Unknown management schema versions are rejected.
- Stopping the Freetz service restarts stock `aha` without `LD_PRELOAD`.
- Removing the package requires building/flashing a normal Freetz or AVM
  image; no AVM file is patched in place at runtime.
- The productive Mesh master is never modified by the installation.

### 2026-10-08 robustness audit

The audit fixes were checked with negative controls: restoring each old
provider branch makes its regression fail, while valid adjacent inputs and
reconnection still pass. Original HA modules likewise fail the new error-path
tests while valid lifecycle cases remain successful.

- Unknown lamp level types no longer generate a successful command event.
  Relative timer end times reject uint32 overflow before changing state.
- If an injected frame fails after writing a prefix, the provider shuts down
  that stream under the lifecycle lock and invalidates its bindings. It never
  closes AHA's descriptor itself. A failure before any bytes, valid short
  writes, EINTR and a subsequent connection are covered separately. A zero-byte
  native write cannot clear the catalog's partial-write guard.
- Thermostat TARGET feedback updates the remembered setpoint without turning
  an OFF device on. SCHEDULE updates schedule metadata rather than replacing
  the actual target/mode. HA sends target before mode, and reconnect replay
  applies schedule metadata before actual state, including compatibility with
  the older provider behavior.
- HA rejects malformed state/action types and non-finite or extreme temperature
  values through its normal validation path. Timer expiry is scheduled before
  service/MQTT calls; a failed restoration retains the original state for retry.
  Resume isolates failures per timer, and failed persistence rolls back the
  corresponding in-memory change without replacing the existing timer.
  Interaction review also found an expiry/apply race introduced by early
  scheduling during resume, and an existing stale-callback race after timer
  replacement. Resume now shares the override lock; expiry callbacks
  verify that they still own the active timer after acquiring that lock.
  Real overlapping asyncio tasks reproduce both races with the prior code
  and pass with the combined fixes.

`sh tests/run_c_sanitizers.sh` runs ASan, UBSan (including float casts), and
LeakSanitizer on the C component, registry, provider and integration tests.
It includes 30,000 deterministic malformed frame/string/registry cases with
exact-size allocations. The regular C job separately covers fault injection
that hides libc symbols, which cannot be combined with the sanitizer runtime.
Both C jobs, all 84 Python/Lua tests, static GCC analysis and ARM builds
passed in this audit. No heap/stack overwrite or invalid memory access was observed in
these runs; this is bounded coverage, not proof of memory safety. HA lifecycle
tests execute the actual methods with mocked services/scheduling, not a live
Home Assistant instance. Audit changes have not been deployed to the devices.

### Follow-up audit: partial failures and setup cleanup

Three additional defects were reproduced against the preceding audit state:

- A prune could remove provider devices and then fail to persist the registry,
  while still reporting ready. Because the provider generation remained healthy,
  periodic health checks did not restore the unchanged desired state. Deletion
  now clears readiness and pending unit/screen work before its first side effect.
  On failure, the existing reconciliation loop restores the desired device set.
  Tests cover native-helper failure, failed REMOVE acknowledgement, failed save,
  subsequent recovery and successful prune. This is recovery across separate
  systems, not an atomic rollback of native controller configuration.
- Boost accepted non-finite source `max_temp` attributes or overflowed converting
  a huge integer to float. Invalid values now use the existing 28-degree default;
  valid source maxima retain their original behavior.
- Failed/cancelled HA setup left subscriptions and timers active, including
  failures after bridge startup. Setup now stops that incomplete runtime before
  propagating the failure; runtime_data is assigned only after successful setup.
  Tests execute actual start/stop methods and verify subscription/timer cleanup.

Negative controls failed before each fix; valid neighboring cases passed.
At that audit checkpoint, the combined state passed 96 Python/Lua tests, Ruff,
the C sanitizer suite with
30,000 malformed-input cases, and the ARM MQTT bridge build. No live deployment
was performed. Home Assistant dependencies are mocked in lifecycle tests.

### Continued audit: parser boundaries and runtime replacement

Further confirmed defects were fixed in source, with failing negative controls
against the preceding implementation and passing valid neighboring cases:

- Brightness feedback rejects values outside 0..255 before floating-point
  arithmetic. Command JSON rejects oversized input and converts recursion and
  integer-conversion failures into protocol errors. Discovery likewise handles
  excessive nesting as a validation error rather than escaping its config flow.
- Manual setup/reconfiguration now validates MQTT topic roots and their combined
  UTF-8 prefix length against the C bridge limits. Device-name validation agrees
  with the provider on control characters and the 79-byte UTF-8 limit.
- Management JSON accepts a literal escaped backslash followed by `u0000` while
  still rejecting a decoded NUL. Registry and management share the escape scanner.
- Provider control parsing rejects an overlong UID rather than interpreting its
  suffix as an ADD profile or RENAME text.
- Stop timeouts retain process PID files, allowing a subsequent stop to retry.
  Recovery refuses to launch stock AHA while the previous process remains alive.
  These tests use mocked process state, never the live router.
- Unload closes timer admission before acquiring the override lock, drains active
  work and cancels timers. Queued commands, resumed overrides and stale expiry
  callbacks cannot restart work afterward. Saved active overrides remain available
  to the next runtime. Tests cover both waiting commands and an active slow service.
- Unload also drains the device-sync lock. Reconnect and periodic reconciliation
  check the stopping flag after acquiring that lock. An old runtime can therefore
  no longer send a delayed authoritative prune after a replacement runtime starts.
  This is a reproducible lifecycle defect, not proof that it caused a particular
  observed 440 disappearance; that still requires live correlation.

At that audit checkpoint, the combined state passed 134 Python/Lua tests, Ruff
and shell syntax checks.
The C suite passes ASan/UBSan/LeakSanitizer with 180,000 deterministic malformed
cases (30,000 parser, 100,000 actual binary dispatcher and 50,000 actual control
parser cases). The ordinary C suite, GCC static analysis and ARM provider/MQTT
builds also pass. An additional 20,000 malformed Python command/value combinations
completed without unexpected exceptions. After the last fix, a further review of
validation, timer/reconciliation locking, transport and catalog lifecycle found
no additional confirmed defect in those paths. This is finite test coverage, not
proof of absence of bugs. HA lifecycle dependencies are mocked; no live deployment
or physical 440 verification was performed during these audit rounds.

### 2026-10-27 release-candidate completion pass

The bounded completion pass concentrated on confirmed functional gaps and the
failure modes that matter on a dedicated long-running bridge:

- Light feedback now preserves value commands received with an off relay,
  supports a complete Kelvin/HS roundtrip and clears the inactive retained
  color mode. Cover translation follows each HA entity's advertised endpoint,
  position and stop capabilities; incompatible read-only/tilt-only sources are
  rejected before creation.
- Thermostat feedback now includes current temperature, exact MAX semantics,
  HA Celsius/Fahrenheit conversion and explicit clearing of stale schedules.
  External changes cancel active overrides, and deletion/rebinding/hub removal
  restores saved source state instead of leaving a real thermostat at MAX/OFF.
- The bridge's currently implemented capacity of 64 devices is enforced in HA;
  it is not a demonstrated native FRITZ!OS/`aha` ceiling. Prune-before-upsert
  permits replacement at full capacity, and the complete desired devices,
  confirmed values and timer state are replayed on reconnect and once per
  minute.
- The watchdog now supervises the MQTT process as well as the provider and
  remains alive after an initial virtual-provider startup failure. Package logs
  are bounded. Shutdown cancels native helpers promptly but gives `modsave` its
  full bounded completion window.
- MQTT Hue/Saturation input no longer relies on overflowing `scanf` numeric
  conversions. The complete native suite passes strict warnings plus
  ASan/UBSan, including the binary/control fuzz loops and all
  dynamic/legacy provider self-tests. No further confirmed memory-corruption,
  invalid-pointer, data-race or code-execution defect was found in the bounded
  review.

RC7 addresses a second maintenance path that the RC6 transport-disconnect grace
period could not cover. A native `cR/TX` write can block during pairing or a
battery-device firmware transfer. The old hook held the global Provider mutex
across that write, so the control worker could not answer `GET`; after three
such failures the watchdog could restart an otherwise healthy AHA process.

The RC7 hook snapshots the tracked socket-pair epoch and duplicates the file
descriptor while holding the state mutex, then releases that mutex before the
native write. A dedicated `native_write_lock` preserves write ordering without
blocking health and state access. After the write, catalog bookkeeping is
accepted only if the original pair and epoch still match, preventing a
concurrent close and numeric descriptor reuse from attaching stale work to a
new connection. Provider catalog injection is deferred while the native writer
owns the stream and retried afterwards. A deterministic regression blocks two
native writers, confirms that control `GET` remains responsive, verifies
deferred injection and simulates descriptor reuse.

For this exact RC7 source, 300 repository tests pass and the optional Lua 5.1
test skips on the current host. Ruff, strict native integration/self-tests, the
focused ASan/UBSan blocked-write regression and the ARM GCC 13.4/Freetz image
build pass. The broader sanitizer/fuzz/fault evidence covers unchanged paths;
full LeakSanitizer and ThreadSanitizer are not claimed for this exact head.

This is still a release candidate, not a claim that undocumented FRITZ radio
and UI behavior can be proven offline. The `0.3.0-rc7` image built from commit
`add07f9166a8dd059aec556bf5236d551e0d04fe` is installed persistently in boot
slot 1 of the laboratory 7530, with RC6 retained in slot 0 for rollback. The
matching Home Assistant RC7 manifest is loaded with one hub and eleven device
subentries. The unchanged eleven-device registry survived the installation and
a second controlled reboot; after that reboot the complete service recovered
autonomously in about 110 seconds and 60 consecutive Provider control `GET`
checks succeeded.

The exact RC7 image and runtime hashes are recorded in the
[release validation checklist](release-validation.md). The earlier real 440
update from 05.38 to 05.45 remains RC6 evidence only. Pairing or device firmware
maintenance has **not** yet been physically repeated with RC7; the deterministic
blocked-write test validates the repaired mechanism but does not replace that
hardware observation. Broader profile checks likewise retain their explicitly
documented qualification level.
