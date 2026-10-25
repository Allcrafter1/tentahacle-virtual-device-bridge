// SPDX-License-Identifier: MIT OR Apache-2.0
#include "provider_transport.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const unsigned char frame[] = {7, 3, 0, 8, 0xFF, 0x80, 0, 1};
static unsigned char delivered[sizeof(frame)];
static size_t offset;
static unsigned call_count;
static int actions[8];

static ssize_t native_write(int fd, const void *data, size_t size) {
    assert(fd == 42);
    assert(size == sizeof(frame) - offset);
    assert(!memcmp(data, frame + offset, size));
    assert(call_count < sizeof(actions) / sizeof(actions[0]));
    int action = actions[call_count++];
    if (action < 0) { errno = -action; return -1; }
    assert((size_t)action <= size);
    memcpy(delivered + offset, data, (size_t)action);
    offset += (size_t)action;
    return action;
}

static void reset(void) {
    offset = call_count = 0;
    memset(actions, 0, sizeof(actions));
    memset(delivered, 0, sizeof(delivered));
}

int main(void) {
    reset();
    actions[0] = -EINTR; actions[1] = 3; actions[2] = -EINTR; actions[3] = 5;
    assert(provider_write_frame(42, frame, sizeof(frame), native_write) == sizeof(frame));
    assert(call_count == 4 && !memcmp(delivered, frame, sizeof(frame)));
    reset();
    actions[0] = 2; actions[1] = -EAGAIN;
    assert(provider_write_frame(42, frame, sizeof(frame), native_write) == -1);
    assert(errno == EAGAIN && offset == 2 && call_count == 2);
    reset();
    assert(provider_write_frame(42, frame, sizeof(frame), native_write) == -1);
    assert(errno == EIO && call_count == 1);
    reset();
    actions[0] = -EPIPE;
    assert(provider_write_frame(42, frame, sizeof(frame), native_write) == -1);
    assert(errno == EPIPE && call_count == 1);
    puts("PASS frame writes: exact remainder, EINTR, zero progress, EAGAIN and errors");
    return 0;
}
