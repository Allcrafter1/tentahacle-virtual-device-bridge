#!/bin/sh
# SPDX-License-Identifier: MIT OR Apache-2.0
# Host-only memory/undefined-behavior checks. Requires cJSON and Mosquitto headers.
set -eu
cd "$(dirname "$0")/.."
audit_build=$(mktemp -d /tmp/tvb-sanitizers-XXXXXX)
trap 'rm -rf "$audit_build"' EXIT HUP INT TERM
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
compile() {
    "${CC:-cc}" -O1 -g -std=gnu11 -Wall -Wextra -Werror \
        -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer \
        -Ifreetz/package/src "$@"
}
src=freetz/package/src
compile tests/device_registry_test.c "$src/device_registry.c" -o "$audit_build/registry"
"$audit_build/registry"
compile tests/registry_store_test.c "$src/device_registry.c" "$src/registry_store.c" -lcjson -o "$audit_build/store"
"$audit_build/store"
compile tests/parser_boundaries_test.c "$src/device_registry.c" "$src/registry_store.c" -lcjson -o "$audit_build/parser"
"$audit_build/parser"
compile tests/registry_io_test.c "$src/device_registry.c" -lcjson -o "$audit_build/io"
"$audit_build/io"
compile tests/provider_components_test.c "$src/provider_events.c" -o "$audit_build/components"
"$audit_build/components"
compile tests/provider_transport_test.c "$src/provider_transport.c" -o "$audit_build/transport"
"$audit_build/transport"
compile tests/provider_catalog_sync_test.c "$src/provider_catalog_sync.c" -o "$audit_build/catalog"
"$audit_build/catalog"
compile -ffunction-sections -fdata-sections tests/mqtt_bridge_test.c "$src/device_registry.c" "$src/registry_store.c" \
    -lcjson -pthread -lm -Wl,--gc-sections -o "$audit_build/mqtt"
"$audit_build/mqtt"
compile tests/provider_failure_regression_test.c "$src/provider_profiles.c" "$src/provider_events.c"     "$src/provider_transport.c" "$src/provider_catalog_sync.c" "$src/device_registry.c"     -ldl -pthread -o "$audit_build/failure-regression"
compile tests/provider_color_test.c "$src/provider_profiles.c" "$src/provider_events.c" \
    "$src/provider_transport.c" "$src/provider_catalog_sync.c" "$src/device_registry.c" \
    -ldl -pthread -o "$audit_build/provider-color"
compile tests/provider_dispatch_fuzz_test.c "$src/provider_profiles.c" "$src/provider_events.c" \
    "$src/provider_transport.c" "$src/provider_catalog_sync.c" "$src/device_registry.c" \
    -ldl -pthread -o "$audit_build/dispatch-fuzz"
"$audit_build/dispatch-fuzz"
"$audit_build/provider-color"
"$audit_build/failure-regression"
compile -fPIC -shared "$src/virtual_provider.c" "$src/provider_profiles.c" "$src/provider_events.c" \
    "$src/provider_transport.c" "$src/provider_catalog_sync.c" "$src/device_registry.c" \
    -ldl -pthread -Wl,-z,defs -o "$audit_build/provider.so"
compile "$src/provider_selftest.c" -o "$audit_build/selftest"
compile tests/provider_catalog_integration_test.c -ldl -o "$audit_build/catalog-integration"
compile -fPIC -shared tests/provider_catalog_converter_mock.c -o "$audit_build/converter.so"
# The sanitizer runtime must precede the interposed provider. Fault injection
# that deliberately hides libc symbols runs in the ordinary C job instead.
audit_asan=$("${CC:-cc}" -print-file-name=libasan.so)
AHA_VIRTUAL_LAB=1 AHA_VIRTUAL_CONTROL_PATH="$audit_build/catalog.sock" \
    LD_PRELOAD="$audit_asan:$audit_build/provider.so:$audit_build/converter.so" "$audit_build/catalog-integration"
AHA_VIRTUAL_LAB=1 AHA_VIRTUAL_CONTROL_PATH="$audit_build/dynamic.sock" \
    TVB_PROVIDER_GOLDEN=tests/fixtures/provider-v0.1.4-dynamic.hex \
    LD_PRELOAD="$audit_asan:$audit_build/provider.so" "$audit_build/selftest"
AHA_VIRTUAL_LAB=1 AHA_VIRTUAL_LEGACY=1 AHA_VIRTUAL_HANFUN_EXPERIMENT=1 \
    AHA_VIRTUAL_CONTROL_PATH="$audit_build/legacy.sock" \
    LD_PRELOAD="$audit_asan:$audit_build/provider.so" "$audit_build/selftest"
