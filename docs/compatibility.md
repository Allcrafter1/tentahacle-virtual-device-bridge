# Compatibility policy

The provider hooks an internal, non-public FRITZ!OS process ABI. Compatibility
is therefore an explicit allowlist, never a broad version claim.

## Validated baseline

| Component | Validated version |
|---|---|
| Bridge hardware | FRITZ!Box 7530 classic / HW236 |
| Bridge firmware | FRITZ!OS 8.25, revision 131719 |
| `aha` SHA-256 | `4e85494198125132876aafa0cb88cf2d873266904b7098b6e4213c3ce8a38aaf` |
| Freetz-NG | commit `995afdf2ded44fbb342e69e5941d9ad36a274e93` |
| Mesh master used in tests | FRITZ!Box 6690 Cable, FRITZ!OS 8.25 |
| Controller used in tests | FRITZ!Smart Control 440, firmware 05.45 |

The provider refuses startup if bridge model, architecture or `aha` fingerprint
is unknown. Adding another firmware requires offline protocol
comparison, isolated self-tests and a live laboratory qualification run.

The listed Mesh combination was functionally validated on the earlier 0.1.5
laboratory baseline, including exact brightness round trips. It is not the
recommended latency topology: commands from the 6690 master to virtual
endpoints on the 7530 took about 6–7 seconds in the laboratory. Direct pairing
of the 440 with the dedicated bridge box is the target topology.

The exact `0.3.0-rc6` image built from commit `ae76886` is installed on the
laboratory box together with the matching Home Assistant component. Exact
root-filesystem hashes, alternate-slot boot, nine-device registry persistence,
automatic provider/MQTT startup, an HA restart and presence of all nine virtual
devices on the 6690 Mesh master passed live checks. The RC6 watchdog also let a
real directly paired 440 complete its 05.38 to 05.45 update without restarting
AHA. Qualification of profiles outside the canary is stated in
[`release-validation.md`](release-validation.md).
