/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "device_registry.h"
#include "registry_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>

static int failures;
static unsigned allocation_calls, allocation_failure, live_allocations;
static void *failing_malloc(size_t size) {
    if (++allocation_calls == allocation_failure) return NULL;
    void *memory = malloc(size);
    if (memory) ++live_allocations;
    return memory;
}
static void tracked_free(void *memory) {
    if (memory) --live_allocations;
    free(memory);
}

#define CHECK(value) do { \
    if (!(value)) { \
        fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #value); \
        ++failures; \
    } \
} while (0)

int main(void) {
    tvb_device_registry original, loaded, unchanged, full, full_loaded;
    uint16_t remote_id = 0;
    char error[160], path[128], *json, key[20], max_name[80];
    const tvb_device *device;
    size_t index;
    tvb_registry_init(&original);
    CHECK(tvb_registry_add(&original, 0, "TVB0123456789abcdef", "Morgenlicht",
                           TVB_PROFILE_COLOR_TEMPERATURE_LIGHT, &remote_id) == TVB_REGISTRY_OK);
    CHECK(remote_id == TVB_FIRST_REMOTE_ID);
    CHECK(tvb_registry_set_color_full(&original, original.revision,
                                      "TVB0123456789ABCDEF", 1) == TVB_REGISTRY_OK);
    CHECK(tvb_registry_add(&original, original.revision, "TVB1111111111111111", "Heizung Büro",
                           TVB_PROFILE_THERMOSTAT, NULL) == TVB_REGISTRY_OK);
    CHECK(tvb_registry_set_enabled(&original, original.revision,
                                   "TVB1111111111111111", 0) == TVB_REGISTRY_OK);

    json = tvb_registry_serialize(&original);
    CHECK(json != NULL);
    CHECK(json && strstr(json, "\"schema_version\":1"));
    CHECK(json && tvb_registry_deserialize(json, &loaded, error, sizeof(error)));
    if (json) tvb_registry_serialized_free(json);
    CHECK(loaded.count == 2);
    CHECK(loaded.revision == original.revision);
    CHECK(loaded.next_remote_id == original.next_remote_id);
    device = tvb_registry_find(&loaded, "TVB0123456789ABCDEF");
    CHECK(device != NULL);
    CHECK(device && !strcmp(device->name, "Morgenlicht"));
    CHECK(device && device->hanfun.unit_id == 1);
    CHECK(device && device->color_full);
    device = tvb_registry_find(&loaded, "TVB1111111111111111");
    CHECK(device && !device->enabled);

    /* The configured release ceiling must remain serializable, persistable
     * and exactly recoverable even with maximum-length UTF-8-safe names and
     * the larger HAN-FUN identity records. */
    tvb_registry_init(&full);
    memset(max_name, 'x', sizeof(max_name) - 1);
    max_name[sizeof(max_name) - 1] = 0;
    for (index = 0; index < TVB_REGISTRY_CAPACITY; ++index) {
        snprintf(key, sizeof(key), "TVB%016llX", (unsigned long long)index);
        CHECK(tvb_registry_add(&full, full.revision, key, max_name,
                               (tvb_device_profile)(index % TVB_PROFILE_COUNT),
                               NULL) == TVB_REGISTRY_OK);
    }
    CHECK(full.count == 64);
    json = tvb_registry_serialize(&full);
    CHECK(json != NULL);
    CHECK(json && strlen(json) < 65536u);
    CHECK(json && tvb_registry_deserialize(json, &full_loaded, error, sizeof(error)));
    if (json) tvb_registry_serialized_free(json);
    CHECK(!memcmp(&full, &full_loaded, sizeof(full)));

    /* Allocation failure at every construction/printing step must release
     * both unattached JSON children and the eventual root tree. */
    cJSON_Hooks hooks = {failing_malloc, tracked_free};
    cJSON_InitHooks(&hooks);
    for (allocation_failure = 1; allocation_failure < 128; ++allocation_failure) {
        allocation_calls = 0;
        char *attempt = tvb_registry_serialize(&original);
        tvb_registry_serialized_free(attempt);
        CHECK(live_allocations == 0);
    }
    cJSON_InitHooks(NULL);

    const char *nul_name = "{\"schema_version\":1,\"revision\":1,\"next_remote_id\":457,\"devices\":["
        "{\"uid\":\"TVB0123456789ABCDEF\",\"name\":\"Name\\u0000suffix\",\"remote_id\":456,"
        "\"profile\":\"switch\",\"enabled\":true,\"revision\":1}]}";
    const char *nul_uid = "{\"schema_version\":1,\"revision\":1,\"next_remote_id\":457,\"devices\":["
        "{\"uid\":\"TVB0123456789ABCDEF\\u0000suffix\",\"name\":\"Name\",\"remote_id\":456,"
        "\"profile\":\"switch\",\"enabled\":true,\"revision\":1}]}";
    CHECK(!tvb_registry_deserialize(nul_name, &loaded, error, sizeof(error)));
    CHECK(!tvb_registry_deserialize(nul_uid, &loaded, error, sizeof(error)));
    tvb_device_registry literal;
    tvb_registry_init(&literal);
    CHECK(tvb_registry_add(&literal, 0, "TVB0123456789ABCDEF", "literal \\u0000",
                           TVB_PROFILE_SWITCH, NULL) == TVB_REGISTRY_OK);
    json = tvb_registry_serialize(&literal);
    CHECK(json && tvb_registry_deserialize(json, &literal, error, sizeof(error)));
    CHECK(!strcmp(literal.devices[0].name, "literal \\u0000"));
    tvb_registry_serialized_free(json);

    unchanged = loaded;
    CHECK(!tvb_registry_deserialize(
        "{\"schema_version\":2,\"revision\":0,\"next_remote_id\":456,\"devices\":[]}",
        &loaded, error, sizeof(error)));
    CHECK(!tvb_registry_deserialize(
        "{\"schema_version\":1,\"revision\":1,\"next_remote_id\":457,\"devices\":["
        "{\"uid\":\"TVB0123456789ABCDEF\",\"name\":\"x\",\"remote_id\":456,"
        "\"profile\":\"switch\",\"enabled\":true,\"color_full\":false,\"revision\":1}]}",
        &loaded, error, sizeof(error)));
    CHECK(!memcmp(&loaded, &unchanged, sizeof(loaded)));
    CHECK(!tvb_registry_deserialize(
        "{\"schema_version\":1,\"revision\":0,\"next_remote_id\":456,\"devices\":[]}junk",
        &loaded, error, sizeof(error)));
    CHECK(!tvb_registry_deserialize(
        "{\"schema_version\":1,\"revision\":1e999,\"next_remote_id\":456,\"devices\":[]}",
        &loaded, error, sizeof(error)));
    CHECK(!memcmp(&loaded, &unchanged, sizeof(loaded)));
    CHECK(!tvb_registry_deserialize(
        "{\"schema_version\":1,\"revision\":1,\"next_remote_id\":457,\"devices\":["
        "{\"uid\":\"TVB0123456789ABCDEF\",\"name\":\"x\",\"remote_id\":456,"
        "\"profile\":\"cover\",\"enabled\":true,\"revision\":1,"
        "\"hanfun\":{\"ipui\":\"not-hexxxx\",\"discriminator\":1,\"unit_id\":1}}]}",
        &loaded, error, sizeof(error)));

    snprintf(path, sizeof(path), "/tmp/tvb-registry-test-%ld.json", (long)getpid());
    unlink(path);
    CHECK(tvb_registry_save_file(path, &full, error, sizeof(error)));
    memset(&full_loaded, 0, sizeof(full_loaded));
    CHECK(tvb_registry_load_file(path, &full_loaded, error, sizeof(error)));
    CHECK(!memcmp(&full, &full_loaded, sizeof(full)));
    unlink(path);
    CHECK(tvb_registry_save_file(path, &original, error, sizeof(error)));
    memset(&loaded, 0, sizeof(loaded));
    CHECK(tvb_registry_load_file(path, &loaded, error, sizeof(error)));
    CHECK(loaded.count == original.count);
    CHECK(loaded.revision == original.revision);
    unchanged = loaded;
    char link_path[160];
    snprintf(link_path, sizeof(link_path), "%s.link", path);
    CHECK(symlink(path, link_path) == 0);
    CHECK(!tvb_registry_load_file(link_path, &loaded, error, sizeof(error)));
    CHECK(!memcmp(&loaded, &unchanged, sizeof(loaded)));
    unlink(link_path);
    CHECK(mkfifo(link_path, 0600) == 0);
    CHECK(!tvb_registry_load_file(link_path, &loaded, error, sizeof(error)));
    unlink(link_path);
    FILE *append = fopen(path, "ab");
    CHECK(append != NULL);
    if (append) { CHECK(fwrite("\0junk", 1, 5, append) == 5); fclose(append); }
    CHECK(!tvb_registry_load_file(path, &loaded, error, sizeof(error)));
    CHECK(!memcmp(&loaded, &unchanged, sizeof(loaded)));
    unlink(path);

    if (failures) return 1;
    puts("registry_store_test: OK");
    return 0;
}
