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

Version 0.3.0-rc.1 completes the existing profile round trips and the bounded
recovery/security audit. Its host, parser, fault-injection and sanitizer suites
pass. Physical qualification of the newly completed color, cover and thermostat
paths, recovery cases and native FRITZ!OS automations remains tracked in
[`release-validation.md`](release-validation.md). Until that checklist passes,
the candidate is suitable for the dedicated laboratory box but not yet the
recommended seven-controller production rollout.
