// SPDX-License-Identifier: MIT OR Apache-2.0
#include "provider_transport.h"
#include <errno.h>

ssize_t provider_write_frame_progress(int fd, const void *data, size_t size,
                             ssize_t (*write_native)(int, const void *, size_t), size_t *progress) {
    const unsigned char *bytes = data;
    size_t offset = 0;
    *progress = 0;
    while (offset < size) {
        ssize_t written = write_native(fd, bytes + offset, size - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (written == 0) {
            errno = EIO;
            return -1;
        }
        offset += (size_t)written;
        *progress = offset;
    }
    return (ssize_t)size;
}

ssize_t provider_write_frame(int fd, const void *data, size_t size,
                             ssize_t (*write_native)(int, const void *, size_t)) {
    size_t progress;
    return provider_write_frame_progress(fd, data, size, write_native, &progress);
}
