// SPDX-License-Identifier: MIT OR Apache-2.0
/* Host-only glibc fault injection; never part of the router package. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>

void *dlsym(void *handle, const char *name) {
    if (handle == RTLD_NEXT &&
        (!strcmp(name, "send") || !strcmp(name, "socketpair") ||
         !strcmp(name, "write") || !strcmp(name, "close"))) return NULL;
    void *(*native_dlsym)(void *, const char *) = dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
    return native_dlsym ? native_dlsym(handle, name) : NULL;
}
