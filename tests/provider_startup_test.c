// SPDX-License-Identifier: MIT OR Apache-2.0
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

static unsigned open_descriptors(void) {
    DIR *directory = opendir("/proc/self/fd");
    assert(directory);
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] != '.') ++count;
    }
    closedir(directory);
    return count;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    char path[256];
    if (!strcmp(argv[2], "long")) {
        memset(path, 'x', sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
    } else if (!strcmp(argv[2], "bind")) {
        snprintf(path, sizeof(path), "/tmp/tvb-missing-%ld/provider.sock", (long)getpid());
    } else {
        assert(!strcmp(argv[2], "thread") || !strcmp(argv[2], "symbols"));
        snprintf(path, sizeof(path), "/tmp/tvb-startup-%ld.sock", (long)getpid());
    }
    setenv("AHA_VIRTUAL_LAB", "1", 1);
    setenv("AHA_VIRTUAL_CONTROL_PATH", path, 1);
    unsigned before = open_descriptors();
    /* This test loads only failure paths: no surviving thread may reference
     * the library, and startup must return every descriptor it acquired. */
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    assert(open_descriptors() == before);
    assert(access(path, F_OK) != 0);
    if (!strcmp(argv[2], "symbols")) {
        ssize_t (*send_hook)(int, const void *, size_t, int) = dlsym(library, "send");
        int (*pair_hook)(int, int, int, int[2]) = dlsym(library, "socketpair");
        assert(send_hook && pair_hook);
        assert(send_hook(-1, "x", 1, 0) == -1 && errno == ENOSYS);
        int pair[2];
        assert(pair_hook(AF_UNIX, SOCK_STREAM, 0, pair) == -1 && errno == ENOSYS);
    }
    dlclose(library);
    puts("PASS failed provider startup leaves no descriptors or control socket");
    return 0;
}
