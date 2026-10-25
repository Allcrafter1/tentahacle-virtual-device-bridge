/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
#include "registry_store.h"
#include "control_parse.h"

#include <cjson/cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TVB_REGISTRY_SCHEMA_VERSION 1
#define TVB_MAX_JSON_BYTES 65536u
#define TVB_MAX_JSON_INTEGER 9007199254740991ULL

static void set_error(char *error, size_t size, const char *message) {
    if (error && size) {
        snprintf(error, size, "%s", message ? message : "unknown error");
    }
}

static int json_u64(const cJSON *value, uint64_t maximum, uint64_t *result) {
    uint64_t converted;
    double number;
    if (!cJSON_IsNumber(value)) return 0;
    number = value->valuedouble;
    if (number < 0.0 || number > (double)maximum ||
        number > (double)TVB_MAX_JSON_INTEGER) return 0;
    converted = (uint64_t)number;
    if ((double)converted != number) return 0;
    *result = converted;
    return 1;
}

static int parse_profile(const char *name, tvb_device_profile *profile) {
    unsigned candidate;
    if (!name) return 0;
    for (candidate = 0; candidate < TVB_PROFILE_COUNT; ++candidate) {
        const char *known = tvb_profile_name((tvb_device_profile)candidate);
        if (known && !strcmp(name, known)) {
            *profile = (tvb_device_profile)candidate;
            return 1;
        }
    }
    return 0;
}

static int is_hanfun_profile(tvb_device_profile profile) {
    return profile == TVB_PROFILE_DIMMABLE_LIGHT ||
           profile == TVB_PROFILE_COLOR_TEMPERATURE_LIGHT ||
           profile == TVB_PROFILE_COVER;
}

static char hex_character(unsigned value) {
    return (char)(value < 10u ? '0' + value : 'A' + value - 10u);
}

static int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static void format_ipui(const uint8_t ipui[5], char output[11]) {
    size_t index;
    for (index = 0; index < 5; ++index) {
        output[index * 2] = hex_character(ipui[index] >> 4);
        output[index * 2 + 1] = hex_character(ipui[index] & 15u);
    }
    output[10] = 0;
}

static int parse_ipui(const char *input, uint8_t output[5]) {
    size_t index;
    if (!input || strlen(input) != 10) return 0;
    for (index = 0; index < 5; ++index) {
        int high = hex_value(input[index * 2]);
        int low = hex_value(input[index * 2 + 1]);
        if (high < 0 || low < 0) return 0;
        output[index] = (uint8_t)((unsigned)high << 4 | (unsigned)low);
    }
    return 1;
}

char *tvb_registry_serialize(const tvb_device_registry *registry) {
    cJSON *root = NULL, *devices = NULL;
    char *serialized = NULL;
    size_t index;
    if (!registry || registry->count > TVB_REGISTRY_CAPACITY ||
        registry->revision > TVB_MAX_JSON_INTEGER ||
        registry->next_remote_id < TVB_FIRST_REMOTE_ID ||
        registry->next_remote_id > (uint32_t)UINT16_MAX + 1u) return NULL;
    root = cJSON_CreateObject();
    devices = cJSON_CreateArray();
    if (!root || !devices ||
        !cJSON_AddNumberToObject(root, "schema_version", TVB_REGISTRY_SCHEMA_VERSION) ||
        !cJSON_AddNumberToObject(root, "revision", (double)registry->revision) ||
        !cJSON_AddNumberToObject(root, "next_remote_id", registry->next_remote_id) ||
        !cJSON_AddItemToObject(root, "devices", devices)) goto done;
    devices = NULL;
    cJSON *items = cJSON_GetObjectItemCaseSensitive(root, "devices");
    for (index = 0; index < registry->count; ++index) {
        const tvb_device *device = &registry->devices[index];
        const char *profile = tvb_profile_name(device->profile);
        cJSON *item = cJSON_CreateObject();
        if (!item || !profile || device->revision > TVB_MAX_JSON_INTEGER ||
            !cJSON_AddStringToObject(item, "uid", device->uid) ||
            !cJSON_AddStringToObject(item, "name", device->name) ||
            !cJSON_AddNumberToObject(item, "remote_id", device->remote_id) ||
            !cJSON_AddStringToObject(item, "profile", profile) ||
            !cJSON_AddBoolToObject(item, "enabled", device->enabled != 0) ||
            !cJSON_AddNumberToObject(item, "revision", (double)device->revision)) {
            cJSON_Delete(item);
            goto done;
        }
        if (is_hanfun_profile(device->profile)) {
            cJSON *identity = cJSON_CreateObject();
            char ipui[11];
            format_ipui(device->hanfun.ipui, ipui);
            if (!identity || !cJSON_AddStringToObject(identity, "ipui", ipui) ||
                !cJSON_AddNumberToObject(identity, "discriminator", device->hanfun.discriminator) ||
                !cJSON_AddNumberToObject(identity, "unit_id", device->hanfun.unit_id) ||
                !cJSON_AddItemToObject(item, "hanfun", identity)) {
                cJSON_Delete(identity);
                cJSON_Delete(item);
                goto done;
            }
        }
        cJSON_AddItemToArray(items, item);
    }
    serialized = cJSON_PrintUnformatted(root);
done:
    cJSON_Delete(devices); /* Non-NULL only before ownership passed to root. */
    cJSON_Delete(root);
    return serialized;
}

void tvb_registry_serialized_free(char *serialized) {
    cJSON_free(serialized);
}

int tvb_registry_deserialize(const char *serialized,
                             tvb_device_registry *registry,
                             char *error,
                             size_t error_size) {
    cJSON *root = NULL, *devices;
    tvb_device_registry parsed;
    uint64_t number;
    int item_count, index;
    if (!serialized || !registry) {
        set_error(error, error_size, "missing registry input");
        return 0;
    }
    if (tvb_json_has_decoded_nul(serialized)) {
        set_error(error, error_size, "registry contains a decoded NUL");
        return 0;
    }
    root = cJSON_ParseWithLengthOpts(serialized, strlen(serialized) + 1, NULL, 1);
    if (!cJSON_IsObject(root)) {
        set_error(error, error_size, "registry is not valid JSON object");
        goto fail;
    }
    memset(&parsed, 0, sizeof(parsed));
    if (!json_u64(cJSON_GetObjectItemCaseSensitive(root, "schema_version"),
                  TVB_REGISTRY_SCHEMA_VERSION, &number) ||
        number != TVB_REGISTRY_SCHEMA_VERSION) {
        set_error(error, error_size, "unsupported registry schema_version");
        goto fail;
    }
    if (!json_u64(cJSON_GetObjectItemCaseSensitive(root, "revision"),
                  TVB_MAX_JSON_INTEGER, &parsed.revision)) {
        set_error(error, error_size, "invalid registry revision");
        goto fail;
    }
    if (!json_u64(cJSON_GetObjectItemCaseSensitive(root, "next_remote_id"),
                  (uint64_t)UINT16_MAX + 1u, &number) ||
        number < TVB_FIRST_REMOTE_ID) {
        set_error(error, error_size, "invalid next_remote_id");
        goto fail;
    }
    parsed.next_remote_id = (uint32_t)number;
    devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
    if (!cJSON_IsArray(devices) ||
        (item_count = cJSON_GetArraySize(devices)) < 0 ||
        item_count > (int)TVB_REGISTRY_CAPACITY) {
        set_error(error, error_size, "invalid device array");
        goto fail;
    }
    for (index = 0; index < item_count; ++index) {
        cJSON *item = cJSON_GetArrayItem(devices, index);
        cJSON *uid, *name, *profile, *enabled, *identity;
        tvb_device *device = &parsed.devices[parsed.count];
        size_t previous;
        uint64_t remote_id, revision, discriminator, unit_id;
        if (!cJSON_IsObject(item)) {
            set_error(error, error_size, "invalid device entry");
            goto fail;
        }
        uid = cJSON_GetObjectItemCaseSensitive(item, "uid");
        name = cJSON_GetObjectItemCaseSensitive(item, "name");
        profile = cJSON_GetObjectItemCaseSensitive(item, "profile");
        enabled = cJSON_GetObjectItemCaseSensitive(item, "enabled");
        if (!cJSON_IsString(uid) || !tvb_registry_valid_uid(uid->valuestring) ||
            !cJSON_IsString(name) || !tvb_registry_valid_name(name->valuestring) ||
            !cJSON_IsString(profile) || !parse_profile(profile->valuestring, &device->profile) ||
            (!cJSON_IsTrue(enabled) && !cJSON_IsFalse(enabled)) ||
            !json_u64(cJSON_GetObjectItemCaseSensitive(item, "remote_id"), UINT16_MAX, &remote_id) ||
            remote_id < TVB_FIRST_REMOTE_ID || remote_id >= parsed.next_remote_id ||
            !json_u64(cJSON_GetObjectItemCaseSensitive(item, "revision"), parsed.revision, &revision) ||
            revision == 0) {
            set_error(error, error_size, "invalid device fields");
            goto fail;
        }
        for (previous = 0; previous < parsed.count; ++previous) {
            if (!strcasecmp(parsed.devices[previous].uid, uid->valuestring) ||
                parsed.devices[previous].remote_id == remote_id) {
                set_error(error, error_size, "duplicate device identity");
                goto fail;
            }
        }
        snprintf(device->uid, sizeof(device->uid), "%s", uid->valuestring);
        for (previous = 3; device->uid[previous]; ++previous) {
            if (device->uid[previous] >= 'a' && device->uid[previous] <= 'f')
                device->uid[previous] = (char)(device->uid[previous] - 'a' + 'A');
        }
        snprintf(device->name, sizeof(device->name), "%s", name->valuestring);
        device->remote_id = (uint16_t)remote_id;
        device->enabled = (uint8_t)cJSON_IsTrue(enabled);
        device->revision = revision;
        identity = cJSON_GetObjectItemCaseSensitive(item, "hanfun");
        if (is_hanfun_profile(device->profile)) {
            cJSON *ipui;
            if (!cJSON_IsObject(identity) ||
                !cJSON_IsString(ipui = cJSON_GetObjectItemCaseSensitive(identity, "ipui")) ||
                !parse_ipui(ipui->valuestring, device->hanfun.ipui) ||
                !json_u64(cJSON_GetObjectItemCaseSensitive(identity, "discriminator"),
                          UINT16_MAX, &discriminator) || discriminator == 0 ||
                !json_u64(cJSON_GetObjectItemCaseSensitive(identity, "unit_id"),
                          UINT16_MAX, &unit_id) || unit_id == 0) {
                set_error(error, error_size, "invalid HAN-FUN identity");
                goto fail;
            }
            device->hanfun.discriminator = (uint16_t)discriminator;
            device->hanfun.unit_id = (uint16_t)unit_id;
        } else if (identity) {
            set_error(error, error_size, "unexpected HAN-FUN identity");
            goto fail;
        }
        ++parsed.count;
    }
    if (parsed.next_remote_id < TVB_FIRST_REMOTE_ID + parsed.count) {
        set_error(error, error_size, "allocator watermark precedes devices");
        goto fail;
    }
    *registry = parsed;
    cJSON_Delete(root);
    set_error(error, error_size, "");
    return 1;
fail:
    cJSON_Delete(root);
    return 0;
}

static int write_all(int descriptor, const char *data, size_t length) {
    while (length) {
        ssize_t written = write(descriptor, data, length);
        if (written < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (written == 0) return 0;
        data += written;
        length -= (size_t)written;
    }
    return 1;
}

static void sync_parent_directory(const char *path) {
    char directory[PATH_MAX], *separator;
    int descriptor;
    if (strlen(path) >= sizeof(directory)) return;
    strcpy(directory, path);
    separator = strrchr(directory, '/');
    if (!separator) strcpy(directory, ".");
    else if (separator == directory) separator[1] = 0;
    else *separator = 0;
    descriptor = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor >= 0) {
        (void)fsync(descriptor);
        close(descriptor);
    }
}

int tvb_registry_save_file(const char *path,
                           const tvb_device_registry *registry,
                           char *error,
                           size_t error_size) {
    char temporary[PATH_MAX];
    char *serialized;
    int descriptor = -1, ok = 0;
    if (!path || strlen(path) >= PATH_MAX - 32u ||
        snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path, (long)getpid()) >=
            (int)sizeof(temporary)) {
        set_error(error, error_size, "invalid registry path");
        return 0;
    }
    serialized = tvb_registry_serialize(registry);
    if (!serialized) {
        set_error(error, error_size, "cannot serialize registry");
        return 0;
    }
    descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0 || !write_all(descriptor, serialized, strlen(serialized)) ||
        write_all(descriptor, "\n", 1) == 0 || fsync(descriptor) != 0) {
        if (descriptor >= 0) close(descriptor);
        unlink(temporary);
        set_error(error, error_size, "cannot write registry atomically");
        goto done;
    }
    /* close() may release the descriptor even when reporting an error.
     * Never retry: another thread could already own the reused fd number. */
    if (close(descriptor) != 0) {
        descriptor = -1;
        unlink(temporary);
        set_error(error, error_size, "cannot close registry file");
        goto done;
    }
    descriptor = -1;
    if (rename(temporary, path) != 0) {
        unlink(temporary);
        set_error(error, error_size, "cannot replace registry file");
        goto done;
    }
    sync_parent_directory(path);
    set_error(error, error_size, "");
    ok = 1;
done:
    tvb_registry_serialized_free(serialized);
    return ok;
}

int tvb_registry_load_file(const char *path,
                           tvb_device_registry *registry,
                           char *error,
                           size_t error_size) {
    struct stat status;
    char *contents;
    size_t read_bytes = 0;
    int descriptor, ok;
    if (!path || !registry) {
        set_error(error, error_size, "invalid registry path or destination");
        return 0;
    }
    /* Inspect and read the same opened inode. NONBLOCK prevents a FIFO from
     * hanging startup before fstat can reject it; regular files ignore it. */
    descriptor = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0 || fstat(descriptor, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size < 2 ||
        status.st_size > (off_t)TVB_MAX_JSON_BYTES) {
        if (descriptor >= 0) close(descriptor);
        set_error(error, error_size, "registry file is missing or invalid");
        return 0;
    }
    contents = malloc((size_t)status.st_size + 1u);
    if (!contents) {
        close(descriptor);
        set_error(error, error_size, "out of memory loading registry");
        return 0;
    }
    while (read_bytes < (size_t)status.st_size) {
        ssize_t n = read(descriptor, contents + read_bytes,
                         (size_t)status.st_size - read_bytes);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto read_failed;
        read_bytes += (size_t)n;
    }
    /* Reject growth and embedded NULs rather than accepting a valid prefix. */
    char extra;
    ssize_t n;
    do { n = read(descriptor, &extra, 1); } while (n < 0 && errno == EINTR);
    if (n != 0 || memchr(contents, 0, read_bytes)) goto read_failed;
    close(descriptor);
    contents[read_bytes] = 0;
    ok = tvb_registry_deserialize(contents, registry, error, error_size);
    free(contents);
    return ok;
read_failed:
    close(descriptor);
    free(contents);
    set_error(error, error_size, "cannot read complete registry file");
    return 0;
}
