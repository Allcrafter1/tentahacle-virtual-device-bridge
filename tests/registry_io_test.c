// SPDX-License-Identifier: MIT OR Apache-2.0
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int zero_write;
static int interrupt_write;
static int short_write;
static int fail_close;
static unsigned close_calls;

static ssize_t test_write(int fd, const void *data, size_t length) {
    if (interrupt_write) { interrupt_write = 0; errno = EINTR; return -1; }
    if (zero_write) return 0;
    if (short_write && length > 3) length = 3;
    return write(fd, data, length);
}

static int test_close(int fd) {
    ++close_calls;
    int result = close(fd);
    if (fail_close) { errno = EIO; return -1; }
    return result;
}

/* Fault injection at the OS boundary exercises the public atomic-save API. */
#define write test_write
#define close test_close
#include "../freetz/package/src/registry_store.c"
#undef write
#undef close

int main(void) {
    char path[] = "/tmp/tvb-registry-io-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, "original", 8) == 8);
    assert(close(fd) == 0);
    tvb_device_registry registry;
    tvb_registry_init(&registry);
    char error[128];

    zero_write = 1;
    assert(!tvb_registry_save_file(path, &registry, error, sizeof(error)));
    assert(close_calls == 1);
    zero_write = 0;
    fail_close = 1;
    close_calls = 0;
    assert(!tvb_registry_save_file(path, &registry, error, sizeof(error)));
    assert(close_calls == 1);
    fail_close = 0;
    FILE *file = fopen(path, "r");
    char contents[16] = {0};
    assert(file && fread(contents, 1, sizeof(contents), file) == 8);
    assert(!strcmp(contents, "original"));
    fclose(file);

    interrupt_write = short_write = 1;
    assert(tvb_registry_save_file(path, &registry, error, sizeof(error)));
    tvb_device_registry loaded;
    assert(tvb_registry_load_file(path, &loaded, error, sizeof(error)));
    assert(loaded.count == 0);
    unlink(path);
    puts("PASS atomic save: zero/short/interrupted writes and close error ownership");
    return 0;
}
