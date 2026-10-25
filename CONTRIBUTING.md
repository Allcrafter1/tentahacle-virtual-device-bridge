# Contributing

Changes are welcome under the repository's MIT OR Apache-2.0 license. Keep
contributions source-only: never commit a proprietary manufacturer binary,
original or modified firmware image, FRITZ!Box backup, credential, serial
number or unredacted support archive.

Before opening a change:

```sh
ruff check .
pytest
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -Ifreetz/package/src freetz/package/src/device_registry.c \
  tests/device_registry_test.c -o /tmp/device_registry_test
/tmp/device_registry_test
```

Provider changes also require both isolated provider self-test modes and a
fresh Freetz cross-build. A new FRITZ!OS fingerprint needs offline comparison
and a live test on a dedicated box before it can enter the compatibility
allowlist. Describe the device/profile tested and whether 440 commands and HA
state feedback were both verified.

The exact native test build commands are maintained in
`.github/workflows/validate.yml`. Build the provider from `virtual_provider.c`,
`provider_profiles.c`, `provider_events.c`, `provider_transport.c` and
`device_registry.c`; linking only the first file is insufficient. Run the
dynamic self-test with `TVB_PROVIDER_GOLDEN=tests/fixtures/provider-v0.1.4-dynamic.hex`
to compare its emitted packets against the original v0.1.4 provider. The fixture
masks only clock-derived fields; do not regenerate it from a changed encoder
to make a failing test pass.

The pure component and transport tests require no socket or AVM library. The
integration and failed-thread-start tests use temporary Unix sockets only.
Run `registry_io_test` with a timeout because it deliberately injects a
zero-progress write. Host AddressSanitizer/UndefinedBehaviorSanitizer runs are
useful for these boundaries; the ARM cross-build is also mandatory because
host `unsigned long` and router `unsigned long` have different widths.

Protocol changes must remain bounded, versioned and backward compatible for a
released schema. Persisted device UID, remote ID, profile and HAN-FUN identity
are immutable. A profile change creates a new device.
