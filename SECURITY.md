# Security policy

TentAHAcle - Virtual Device Bridge loads project code into an internal
FRITZ!OS process and is intentionally restricted to exact reviewed firmware
fingerprints. Do not remove that guard to make another model or release start.

Keep the Freetz interface and MQTT broker on a trusted local network. Use a
dedicated MQTT account limited to the configured `tentahacle/<bridge_id>/#`
topic. The Freetz package writes those credentials only to a root-readable
runtime file; they must never be included in an issue, support archive or
configuration backup attached to GitHub.

Report a suspected vulnerability through GitHub's private security advisory
feature. Include the bridge hardware, FRITZ!OS revision, `aha` SHA-256, Freetz
commit and the smallest reproducible input. Do not attach AVM binaries,
firmware images, credentials, serial numbers or unredacted support data.

Only the compatibility tuple in `docs/compatibility.md` is supported. A crash
or fingerprint mismatch on another build is a compatibility report, not proof
that broadening the allowlist is safe.
