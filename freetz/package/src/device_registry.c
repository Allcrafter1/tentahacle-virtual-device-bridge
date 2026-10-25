/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "device_registry.h"
#include <string.h>

static int hex_digit(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
static unsigned hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10u;
    return c - 'A' + 10u;
}
int tvb_registry_valid_uid(const char *s) {
    size_t i, length = 0;
    if (!s) return 0;
    while (length < TVB_DEVICE_UID_BYTES && s[length]) ++length;
    if (length != 19 || memcmp(s, "TVB", 3)) return 0;
    for (i = 3; i < 19; ++i) if (!hex_digit((unsigned char)s[i])) return 0;
    return 1;
}
static void canonical_uid(char out[20], const char *s) {
    size_t i;
    for (i = 0; i < 19; ++i)
        out[i] = s[i] >= 'a' && s[i] <= 'f' ? (char)(s[i] - 'a' + 'A') : s[i];
    out[19] = 0;
}
int tvb_registry_valid_name(const char *s) {
    size_t n = 0, i = 0;
    if (!s) return 0;
    while (n < TVB_DEVICE_NAME_BYTES && s[n]) ++n;
    if (!n || n == TVB_DEVICE_NAME_BYTES) return 0;
    while (i < n) {
        uint32_t cp;
        unsigned need, j;
        unsigned char c = (unsigned char)s[i++];
        if (c < 0x80) { cp = c; need = 0; }
        else if (c >= 0xc2 && c <= 0xdf) { cp = c & 31u; need = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15u; need = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7u; need = 3; }
        else return 0;
        if (need > n - i) return 0;
        for (j = 0; j < need; ++j) {
            c = (unsigned char)s[i++];
            if ((c & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (c & 63u);
        }
        if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) ||
            (need == 3 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp < 0x20 ||
            (cp >= 0x7f && cp <= 0x9f)) return 0;
    }
    return 1;
}
const char *tvb_profile_name(tvb_device_profile p) {
    static const char *const names[] = { "switch", "dimmable_light",
        "color_temperature_light", "cover", "thermostat" };
    return (unsigned)p < TVB_PROFILE_COUNT ? names[p] : NULL;
}
void tvb_registry_init(tvb_device_registry *r) {
    if (r) { memset(r, 0, sizeof(*r)); r->next_remote_id = TVB_FIRST_REMOTE_ID; }
}
const tvb_device *tvb_registry_find(const tvb_device_registry *r, const char *uid) {
    char canonical[20]; size_t i;
    if (!r || r->count > TVB_REGISTRY_CAPACITY || !tvb_registry_valid_uid(uid)) return NULL;
    canonical_uid(canonical, uid);
    for (i = 0; i < r->count; ++i)
        if (!strcmp(r->devices[i].uid, canonical)) return &r->devices[i];
    return NULL;
}
const tvb_device *tvb_registry_find_remote(const tvb_device_registry *r, uint16_t id) {
    size_t i;
    if (!r || r->count > TVB_REGISTRY_CAPACITY) return NULL;
    for (i = 0; i < r->count; ++i) if (r->devices[i].remote_id == id) return &r->devices[i];
    return NULL;
}
const tvb_device *tvb_registry_at(const tvb_device_registry *r, size_t i) {
    return r && r->count <= TVB_REGISTRY_CAPACITY && i < r->count ? &r->devices[i] : NULL;
}
size_t tvb_registry_count(const tvb_device_registry *r) {
    return r && r->count <= TVB_REGISTRY_CAPACITY ? r->count : 0;
}
static tvb_registry_result check(const tvb_device_registry *r, uint64_t rev) {
    if (!r || r->count > TVB_REGISTRY_CAPACITY || r->next_remote_id < TVB_FIRST_REMOTE_ID)
        return TVB_REGISTRY_INVALID;
    return r->revision == rev ? TVB_REGISTRY_OK : TVB_REGISTRY_CONFLICT;
}
tvb_registry_result tvb_registry_add(tvb_device_registry *r, uint64_t rev,
    const char *uid, const char *name, tvb_device_profile p, uint16_t *id) {
    tvb_device d; tvb_registry_result result = check(r, rev);
    if (result != TVB_REGISTRY_OK) return result;
    if (!tvb_registry_valid_uid(uid) || !tvb_registry_valid_name(name) || !tvb_profile_name(p))
        return TVB_REGISTRY_INVALID;
    if (tvb_registry_find(r, uid)) return TVB_REGISTRY_DUPLICATE;
    if (r->count == TVB_REGISTRY_CAPACITY) return TVB_REGISTRY_FULL;
    if (r->next_remote_id > UINT16_MAX || r->revision == UINT64_MAX)
        return TVB_REGISTRY_EXHAUSTED;
    if (tvb_registry_find_remote(r, (uint16_t)r->next_remote_id)) return TVB_REGISTRY_INVALID;
    memset(&d, 0, sizeof(d)); canonical_uid(d.uid, uid);
    memcpy(d.name, name, strlen(name) + 1);
    d.remote_id = (uint16_t)r->next_remote_id; d.profile = p; d.enabled = 1;
    if (p == TVB_PROFILE_DIMMABLE_LIGHT || p == TVB_PROFILE_COLOR_TEMPERATURE_LIGHT || p == TVB_PROFILE_COVER) {
        size_t i;
        /* The radio-side identity follows the random stable UID rather than
         * the bridge-local allocator, so two bridge boxes do not generate the
         * same HAN-FUN IPUI for their first device. */
        for (i = 0; i < 5; ++i)
            d.hanfun.ipui[i] = (uint8_t)((hex_value((unsigned char)d.uid[9 + i * 2]) << 4) |
                                         hex_value((unsigned char)d.uid[10 + i * 2]));
        d.hanfun.discriminator = d.remote_id; d.hanfun.unit_id = 1;
    }
    d.revision = ++r->revision; r->devices[r->count++] = d; ++r->next_remote_id;
    if (id) *id = d.remote_id;
    return TVB_REGISTRY_OK;
}
tvb_registry_result tvb_registry_rename(tvb_device_registry *r, uint64_t rev,
    const char *uid, const char *name) {
    const tvb_device *found; tvb_device *d; char copy[80];
    tvb_registry_result result = check(r, rev);
    if (result != TVB_REGISTRY_OK) return result;
    if (!tvb_registry_valid_uid(uid) || !tvb_registry_valid_name(name)) return TVB_REGISTRY_INVALID;
    found = tvb_registry_find(r, uid); if (!found) return TVB_REGISTRY_NOT_FOUND;
    if (!strcmp(found->name, name)) return TVB_REGISTRY_OK;
    if (r->revision == UINT64_MAX) return TVB_REGISTRY_EXHAUSTED;
    memcpy(copy, name, strlen(name) + 1); d = &r->devices[found - r->devices];
    memcpy(d->name, copy, strlen(copy) + 1); d->revision = ++r->revision;
    return TVB_REGISTRY_OK;
}
tvb_registry_result tvb_registry_set_enabled(tvb_device_registry *r, uint64_t rev,
    const char *uid, int enabled) {
    const tvb_device *found; tvb_device *d;
    tvb_registry_result result = check(r, rev);
    if (result != TVB_REGISTRY_OK) return result;
    if (!tvb_registry_valid_uid(uid) || (enabled != 0 && enabled != 1)) return TVB_REGISTRY_INVALID;
    found = tvb_registry_find(r, uid); if (!found) return TVB_REGISTRY_NOT_FOUND;
    if (found->enabled == enabled) return TVB_REGISTRY_OK;
    if (r->revision == UINT64_MAX) return TVB_REGISTRY_EXHAUSTED;
    d = &r->devices[found - r->devices]; d->enabled = (uint8_t)enabled;
    d->revision = ++r->revision; return TVB_REGISTRY_OK;
}
tvb_registry_result tvb_registry_remove(tvb_device_registry *r, uint64_t rev,
    const char *uid) {
    const tvb_device *found; size_t index;
    tvb_registry_result result = check(r, rev);
    if (result != TVB_REGISTRY_OK) return result;
    if (!tvb_registry_valid_uid(uid)) return TVB_REGISTRY_INVALID;
    found = tvb_registry_find(r, uid); if (!found) return TVB_REGISTRY_NOT_FOUND;
    if (r->revision == UINT64_MAX) return TVB_REGISTRY_EXHAUSTED;
    index = (size_t)(found - r->devices);
    if (index + 1 < r->count)
        memmove(&r->devices[index], &r->devices[index + 1],
                (r->count - index - 1) * sizeof(r->devices[0]));
    --r->count;
    memset(&r->devices[r->count], 0, sizeof(r->devices[0]));
    ++r->revision;
    return TVB_REGISTRY_OK;
}
