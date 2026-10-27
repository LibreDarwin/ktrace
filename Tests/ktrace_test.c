/* Copyright (C) 2026, LibreDarwin */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "ktrace.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/libredarwin-ktrace.XXXXXX";
    ktrace_session_t session = ktrace_session_create();
    int fd = mkstemp(path);
    assert(session != NULL);
    assert(fd >= 0);
    assert(ktrace_set_buffer_size(session, 64) == 0);
    assert(ktrace_set_continuous_timestamps(session, 1) == 0);
    assert(ktrace_set_thread_groups_enabled(session, 1) == 0);
    assert(ktrace_set_use_existing(session) == 1);
    assert(ktrace_set_no_postprocessing(session) == 0);
    assert(ktrace_start_writing_fd(session, 0, fd, 0, 0) == 0);
    assert(ktrace_start(session) == 0);
    assert(ktrace_start(session) == EBUSY);
    assert(ktrace_end(session, 1) == 0);
    assert(ktrace_end(session, 0) == EINVAL);
    ktrace_session_destroy(session);
    close(fd);
    unlink(path);
    return 0;
}
