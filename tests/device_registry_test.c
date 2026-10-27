/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "device_registry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
_Static_assert(TVB_REGISTRY_CAPACITY == 64u, "release capacity must remain synchronized with HA");
static const char *uid = "TVB00000000000000af";
static void validation(void) {
    char name[81];
    assert(!tvb_registry_valid_uid(NULL));
    assert(!tvb_registry_valid_uid(""));
    assert(!tvb_registry_valid_uid("F"));
    assert(!tvb_registry_valid_uid("FV"));
    assert(!tvb_registry_valid_uid("TVB"));
    assert(tvb_registry_valid_uid(uid));
    for (size_t length = 0; length < 19; ++length) {
        char *short_uid = malloc(length + 1);
        assert(short_uid);
        memcpy(short_uid, uid, length);
        short_uid[length] = 0;
        assert(!tvb_registry_valid_uid(short_uid));
        free(short_uid);
    }
    assert(!tvb_registry_valid_uid("TVB00000000000000ag"));
    assert(!tvb_registry_valid_uid("TVB00000000000000aff"));
    assert(!tvb_registry_valid_name(NULL)); assert(!tvb_registry_valid_name(""));
    assert(tvb_registry_valid_name("K\xc3\xbc" "che \xf0\x9f\x92\xa1"));
    assert(!tvb_registry_valid_name("line\nfeed"));
    assert(!tvb_registry_valid_name("\x7f"));
    assert(!tvb_registry_valid_name("\xc2\x85"));
    assert(!tvb_registry_valid_name("\xc0\xaf"));
    assert(!tvb_registry_valid_name("\xe0\x80\xaf"));
    assert(!tvb_registry_valid_name("\xed\xa0\x80"));
    assert(!tvb_registry_valid_name("\xf4\x90\x80\x80"));
    assert(!tvb_registry_valid_name("\xe2\x82"));
    assert(!tvb_registry_valid_name("\x80"));
    memset(name, 'a', 79); name[79] = 0; assert(tvb_registry_valid_name(name));
    name[79] = 'a'; name[80] = 0; assert(!tvb_registry_valid_name(name));
    assert(!tvb_profile_name((tvb_device_profile)-1));
}
static void mutations(void) {
    tvb_device_registry r, before; tvb_device identity; uint16_t id = 9;
    tvb_registry_init(&r); assert(r.next_remote_id == 456);
    assert(tvb_registry_add(&r, 1, uid, "Test", TVB_PROFILE_COVER, &id) == TVB_REGISTRY_CONFLICT);
    assert(id == 9 && !r.count);
    assert(tvb_registry_add(&r, 0, uid, "Test", TVB_PROFILE_COVER, &id) == TVB_REGISTRY_OK);
    assert(id == 456 && r.revision == 1 && r.count == 1);
    identity = *tvb_registry_find(&r, uid);
    assert(!strcmp(identity.uid, "TVB00000000000000AF"));
    assert(identity.hanfun.unit_id == 1 && identity.hanfun.discriminator == 456);
    assert(identity.hanfun.ipui[0] == 0 && identity.hanfun.ipui[1] == 0);
    assert(identity.hanfun.ipui[2] == 0 && identity.hanfun.ipui[3] == 0);
    assert(identity.hanfun.ipui[4] == 0xaf);
    assert(tvb_registry_find_remote(&r, id) == tvb_registry_at(&r, 0));
    assert(!tvb_registry_at(&r, 1)); assert(tvb_registry_count(&r) == 1);
    before = r;
    assert(tvb_registry_add(&r, 1, identity.uid, "Other", TVB_PROFILE_SWITCH, &id) == TVB_REGISTRY_DUPLICATE);
    assert(!memcmp(&before, &r, sizeof(r)));
    assert(tvb_registry_rename(&r, 1, uid, "New") == TVB_REGISTRY_OK);
    assert(tvb_registry_rename(&r, 1, uid, "Bad") == TVB_REGISTRY_CONFLICT);
    assert(tvb_registry_rename(&r, 2, uid, "New") == TVB_REGISTRY_OK && r.revision == 2);
    assert(tvb_registry_set_enabled(&r, 2, uid, 0) == TVB_REGISTRY_OK);
    assert(tvb_registry_set_enabled(&r, 3, uid, 0) == TVB_REGISTRY_OK && r.revision == 3);
    assert(tvb_registry_set_enabled(&r, 3, uid, 1) == TVB_REGISTRY_OK);
    assert(!strcmp(r.devices[0].uid, identity.uid));
    assert(r.devices[0].remote_id == identity.remote_id);
    assert(!memcmp(&r.devices[0].hanfun, &identity.hanfun, sizeof(identity.hanfun)));
    assert(r.devices[0].profile == identity.profile);
    before = r;
    assert(tvb_registry_set_enabled(&r, 4, uid, 2) == TVB_REGISTRY_INVALID);
    assert(tvb_registry_rename(&r, 4, uid, "\n") == TVB_REGISTRY_INVALID);
    assert(tvb_registry_rename(&r, 4, "TVB0000000000000000", "Absent") == TVB_REGISTRY_NOT_FOUND);
    assert(!memcmp(&before, &r, sizeof(r)));
    assert(tvb_registry_add(&r, 4, "TVB0000000000000001", "Second", TVB_PROFILE_SWITCH, NULL) == TVB_REGISTRY_OK);
    assert(tvb_registry_remove(&r, 5, uid) == TVB_REGISTRY_OK);
    assert(r.count == 1 && r.revision == 6 && !strcmp(r.devices[0].uid, "TVB0000000000000001"));
    assert(r.next_remote_id == 458 && r.devices[1].uid[0] == 0);
    assert(tvb_registry_remove(&r, 6, uid) == TVB_REGISTRY_NOT_FOUND);
    r.revision = UINT64_MAX;
    assert(tvb_registry_rename(&r, UINT64_MAX, "TVB0000000000000001", "Overflow") == TVB_REGISTRY_EXHAUSTED);
}
static void capacity(void) {
    tvb_device_registry r, before; char key[20]; size_t i;
    tvb_registry_init(&r);
    for (i = 0; i < TVB_REGISTRY_CAPACITY; ++i) {
        snprintf(key, sizeof(key), "TVB%016llX", (unsigned long long)i);
        assert(tvb_registry_add(&r, r.revision, key, "Device", (tvb_device_profile)(i % TVB_PROFILE_COUNT), NULL) == TVB_REGISTRY_OK);
        assert(r.devices[i].remote_id == 456 + i);
    }
    before = r;
    assert(tvb_registry_add(&r, r.revision, uid, "Overflow", TVB_PROFILE_SWITCH, NULL) == TVB_REGISTRY_FULL);
    assert(!memcmp(&before, &r, sizeof(r)));
    tvb_registry_init(&r); r.next_remote_id = UINT16_MAX;
    assert(tvb_registry_add(&r, 0, uid, "Last", TVB_PROFILE_SWITCH, NULL) == TVB_REGISTRY_OK);
    assert(r.devices[0].remote_id == UINT16_MAX && r.next_remote_id == 65536);
    assert(tvb_registry_add(&r, 1, "TVB0000000000000001", "No IDs", TVB_PROFILE_SWITCH, NULL) == TVB_REGISTRY_EXHAUSTED);
}
static void color_capability(void) {
    tvb_device_registry r, before;
    tvb_registry_init(&r);
    assert(tvb_registry_add(&r, 0, uid, "Colour", TVB_PROFILE_COLOR_TEMPERATURE_LIGHT, NULL) == TVB_REGISTRY_OK);
    assert(!r.devices[0].color_full);
    assert(tvb_registry_set_color_full(&r, 1, uid, 1) == TVB_REGISTRY_OK);
    assert(r.devices[0].color_full && r.revision == 2 && r.devices[0].revision == 2);
    before = r;
    assert(tvb_registry_set_color_full(&r, 2, uid, 1) == TVB_REGISTRY_OK);
    assert(!memcmp(&before, &r, sizeof(r)));
    assert(tvb_registry_set_color_full(&r, 1, uid, 0) == TVB_REGISTRY_CONFLICT);
    assert(tvb_registry_set_color_full(&r, 2, uid, 2) == TVB_REGISTRY_INVALID);
    assert(!memcmp(&before, &r, sizeof(r)));
    assert(tvb_registry_add(&r, 2, "TVB0000000000000001", "Switch", TVB_PROFILE_SWITCH, NULL) == TVB_REGISTRY_OK);
    before = r;
    assert(tvb_registry_set_color_full(&r, 3, "TVB0000000000000001", 1) == TVB_REGISTRY_INVALID);
    assert(!memcmp(&before, &r, sizeof(r)));
}
int main(void) { validation(); mutations(); capacity(); color_capability(); puts("device_registry_test: OK"); return 0; }
