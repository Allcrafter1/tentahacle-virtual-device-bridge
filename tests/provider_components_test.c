// SPDX-License-Identifier: MIT OR Apache-2.0
#include "provider_codec.h"
#include "provider_events.h"
#include "provider_state.h"
#include "control_parse.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void framing(void) {
    unsigned char bytes[33] = {7, 3, 0, 16};
    bytes[16] = 7;
    bytes[17] = 3;
    bytes[19] = 16;
    assert(provider_complete_batch(bytes, 16));
    assert(provider_complete_batch(bytes, 32));
    for (size_t size = 0; size < sizeof(bytes); ++size) {
        if (size != 16 && size != 32) assert(!provider_complete_batch(bytes, size));
    }
    bytes[19] = 0;
    assert(!provider_complete_batch(bytes, 32));
    bytes[19] = 17;
    assert(!provider_complete_batch(bytes, 32));
    bytes[19] = 16;
    bytes[17] = 4;
    assert(!provider_complete_batch(bytes, 32));
    /* Unaligned access and high bits must be portable to ARM. */
    write_be32(bytes + 1, UINT32_C(0xFEDCBA98));
    assert(!memcmp(bytes + 1, "\xfe\xdc\xba\x98", 4));
    assert(read_be32(bytes + 1) == UINT32_C(0xFEDCBA98));
    write_be16(bytes + 3, 0xFEDC);
    assert(read_be16(bytes + 3) == 0xFEDC);
}

static void hanfun_revisions(void) {
    struct provider_state state = PROVIDER_STATE_INITIALIZER;
    const uint64_t now = UINT64_C(1790000000123456);
    assert(provider_advance_hanfun_timestamp(&state, now));
    assert(state.hanfun_timestamp == now);
    assert(provider_advance_hanfun_timestamp(&state, now));
    assert(state.hanfun_timestamp == now + 1);
    assert(provider_advance_hanfun_timestamp(&state, now - UINT64_C(1000000)));
    assert(state.hanfun_timestamp == now + 2);
    assert(provider_advance_hanfun_timestamp(&state, now + UINT64_C(1000000)));
    assert(state.hanfun_timestamp == now + UINT64_C(1000000));
    state.hanfun_timestamp = UINT64_MAX - 1;
    assert(provider_advance_hanfun_timestamp(&state, now));
    assert(state.hanfun_timestamp == UINT64_MAX);
    assert(!provider_advance_hanfun_timestamp(&state, now));
    assert(state.hanfun_timestamp == UINT64_MAX);
    unsigned char bytes[9] = {0};
    write_be64(bytes + 1, UINT64_C(0x123456789ABCDEF0));
    assert(!memcmp(bytes + 1, "\x12\x34\x56\x78\x9a\xbc\xde\xf0", 8));
}

static void events(void) {
    struct command_event event = {.sequence = 42, .state = 1, .has_level = 1, .level = 60};
    strcpy(event.endpoint, "TVB0123456789ABCDEF");
    char message[224];
    const char *expected = "{\"event\":\"command\",\"endpoint\":\"TVB0123456789ABCDEF\",\"state\":1,\"level\":60,\"sequence\":42}\n";
    assert(provider_event_format(&event, message, sizeof(message)) == (int)strlen(expected));
    assert(!strcmp(message, expected));
    /* A too-small destination must never yield a length unsafe for send(). */
    assert(provider_event_format(&event, message, strlen(expected)) == -1);
    assert(provider_event_format(&event, NULL, 0) == -1);
    event.has_level = 0;
    event.has_thermostat_timer = 1;
    event.thermostat_timer_mode = 255;
    event.thermostat_timer_previous = 3;
    assert(provider_event_format(&event, message, sizeof(message)) > 0);
    assert(strstr(message, "\"timer_action\":\"cancel\",\"timer_previous\":\"cold\""));

    struct provider_event_queue queue = {0};
    assert(!provider_event_pop(&queue, &event));
    for (unsigned i = 0; i < 100; ++i) {
        event.sequence = i;
        provider_event_push(&queue, &event);
    }
    /* Existing overflow policy: keep the newest 31 commands, in order. */
    assert(queue.dropped == 69);
    for (unsigned i = 69; i < 100; ++i) {
        assert(provider_event_pop(&queue, &event));
        assert(event.sequence == i);
        assert(!strcmp(event.endpoint, "TVB0123456789ABCDEF"));
    }
    assert(!provider_event_pop(&queue, &event));
}

int main(void) {
    uint32_t timestamp;
    unsigned schedule[4];
    assert(tvb_parse_timestamp("4294967295", &timestamp) && timestamp == UINT32_MAX);
    assert(!tvb_parse_timestamp("4294967296", &timestamp));
    assert(!tvb_parse_timestamp("99999999999999999999999999", &timestamp));
    assert(!tvb_parse_timestamp("-1", &timestamp));
    assert(!tvb_parse_timestamp("0", &timestamp));
    assert(!tvb_parse_timestamp("12garbage", &timestamp));
    assert(tvb_parse_schedule("40 36 100 200", schedule));
    assert(!tvb_parse_schedule("4294967336 36 100 200", schedule));
    assert(!tvb_parse_schedule("40 36 100 100", schedule));
    assert(!tvb_parse_schedule("40 36 100 10080", schedule));
    assert(!tvb_parse_schedule("40 36 100 200 extra", schedule));
    assert(!tvb_parse_schedule("40 36 -1 200", schedule));
    hanfun_revisions();
    framing();
    events();
    puts("PASS provider codec, event formatting and bounded queue");
    return 0;
}
