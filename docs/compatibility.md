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

The exact `0.3.0-rc7` image built from commit
`add07f9166a8dd059aec556bf5236d551e0d04fe` is installed persistently in boot
slot 1 of the laboratory box; the preceding RC6 image remains in slot 0 for
rollback. Its matching Home Assistant RC7 manifest is loaded with the unchanged
hub and eleven device subentries. The eleven-device registry survived two
controlled boots unchanged. After the second boot the Provider, watchdog and
MQTT path became healthy without intervention in about 110 seconds, followed
by 60 successful control `GET` checks out of 60.

RC7 removes the Provider state/control mutex from the duration of blocking
native `cR/TX` writes while preserving connection identity, native write order
and deferred catalog injection. The failure mechanism is covered by a
deterministic blocked-write regression. A real pairing or controller firmware
update has **not yet been physically repeated with RC7**. The successful 05.38
to 05.45 update belongs to RC6 and must not be treated as physical validation
of this RC7 correction. Exact artifacts, hashes and remaining profile
qualification are stated in
[`release-validation.md`](release-validation.md).
