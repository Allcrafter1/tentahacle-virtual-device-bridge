// SPDX-License-Identifier: MIT OR Apache-2.0
/* Preloaded only into provider_startup_test to inject pthread_create failure. */
#include <errno.h>
#include <pthread.h>

int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                   void *(*start)(void *), void *argument) {
    (void)thread;
    (void)attributes;
    (void)start;
    (void)argument;
    return EAGAIN;
}
