// SPDX-License-Identifier: MIT OR Apache-2.0
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int interrupted;
static ssize_t partial_stdout(int fd, const void *data, size_t size) {
    if (!interrupted++) { errno = EINTR; return -1; }
    return write(fd, data, size > 7 ? 7 : size);
}
#define main provider_ctl_main
#define write partial_stdout
#include "../freetz/package/src/provider_ctl.c"
#undef write
#undef main

static void exchange(unsigned reply_size, int expected) {
    char path[108];
    snprintf(path, sizeof(path), "/tmp/tvb-ctl-test-%ld.sock", (long)getpid());
    int listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    assert(listener >= 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, path);
    assert(!bind(listener, (struct sockaddr *)&address, sizeof(address)));
    assert(!listen(listener, 1));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        int fd = accept(listener, NULL, NULL);
        char request[256], reply[2050];
        assert(fd >= 0 && recv(fd, request, sizeof(request), 0) > 0);
        memset(reply, 'x', sizeof(reply));
        if (reply_size) assert(send(fd, reply, reply_size, MSG_NOSIGNAL) == reply_size);
        close(fd); close(listener); _exit(0);
    }
    int capture[2], saved_stdout = dup(STDOUT_FILENO);
    assert(saved_stdout >= 0 && !pipe(capture));
    assert(dup2(capture[1], STDOUT_FILENO) >= 0);
    close(capture[1]);
    char *args[] = {"provider_ctl", "GET", path, NULL};
    interrupted = 0;
    assert(provider_ctl_main(3, args) == expected);
    assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
    close(saved_stdout);
    char output[4096];
    ssize_t length = read(capture[0], output, sizeof(output));
    assert(length == (expected == 0 ? (ssize_t)reply_size : 0));
    for (ssize_t i = 0; i < length; ++i) assert(output[i] == 'x');
    close(capture[0]); close(listener); unlink(path);
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(void) {
    exchange(1800, 0);
    exchange(2049, 7);
    exchange(0, 7);
    puts("PASS control client: full replies, truncated packets, EOF, EINTR and short stdout");
    return 0;
}
