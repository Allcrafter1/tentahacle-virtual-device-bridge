/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef TVB_DEVICE_REGISTRY_H
#define TVB_DEVICE_REGISTRY_H
#include <stddef.h>
#include <stdint.h>
#define TVB_REGISTRY_CAPACITY 32u
#define TVB_DEVICE_NAME_BYTES 80u
#define TVB_DEVICE_UID_BYTES 20u
#define TVB_FIRST_REMOTE_ID 456u

typedef enum {
    TVB_PROFILE_SWITCH, TVB_PROFILE_DIMMABLE_LIGHT,
    TVB_PROFILE_COLOR_TEMPERATURE_LIGHT, TVB_PROFILE_COVER,
    TVB_PROFILE_THERMOSTAT, TVB_PROFILE_COUNT
} tvb_device_profile;
typedef enum {
    TVB_REGISTRY_OK, TVB_REGISTRY_INVALID, TVB_REGISTRY_CONFLICT,
    TVB_REGISTRY_DUPLICATE, TVB_REGISTRY_FULL, TVB_REGISTRY_NOT_FOUND,
    TVB_REGISTRY_EXHAUSTED
} tvb_registry_result;
typedef struct {
    uint8_t ipui[5];
    uint16_t discriminator;
    uint16_t unit_id;
} tvb_hanfun_identity;
typedef struct {
    char uid[TVB_DEVICE_UID_BYTES];
    char name[TVB_DEVICE_NAME_BYTES];
    uint16_t remote_id;
    tvb_device_profile profile;
    tvb_hanfun_identity hanfun;
    uint8_t enabled;
    uint64_t revision;
} tvb_device;
/* Caller owns storage and serializes access. Read devices through const accessors.
 * Deleted remote IDs are never recycled; the allocator watermark remains durable.
 * Persistence must preserve the entire logical registry, including next_remote_id.
 * init is for a NEW registry only. No pointers or dynamic allocation are stored.
 */
typedef struct {
    uint64_t revision;
    uint32_t next_remote_id;
    size_t count;
    tvb_device devices[TVB_REGISTRY_CAPACITY];
} tvb_device_registry;
void tvb_registry_init(tvb_device_registry *registry);
int tvb_registry_valid_uid(const char *uid);
int tvb_registry_valid_name(const char *name);
const char *tvb_profile_name(tvb_device_profile profile);
const tvb_device *tvb_registry_find(const tvb_device_registry *, const char *uid);
const tvb_device *tvb_registry_find_remote(const tvb_device_registry *, uint16_t id);
const tvb_device *tvb_registry_at(const tvb_device_registry *, size_t index);
size_t tvb_registry_count(const tvb_device_registry *);
/* All mutations require the exact current GLOBAL revision. No-op mutations do
 * not increment revisions. Failed calls leave registry and output ID unchanged.
 * UID is supplied by the owner, canonicalized to uppercase hex. Identity and
 * profile cannot be changed by any update operation. */
tvb_registry_result tvb_registry_add(tvb_device_registry *, uint64_t expected_revision,
    const char *uid, const char *name, tvb_device_profile, uint16_t *remote_id);
tvb_registry_result tvb_registry_rename(tvb_device_registry *, uint64_t expected_revision,
    const char *uid, const char *name);
tvb_registry_result tvb_registry_set_enabled(tvb_device_registry *, uint64_t expected_revision,
    const char *uid, int enabled);
tvb_registry_result tvb_registry_remove(tvb_device_registry *, uint64_t expected_revision,
    const char *uid);
#endif
