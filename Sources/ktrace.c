/* Copyright (C) 2026, LibreDarwin */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "ktrace.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct ktrace_session {
    int output_fd;
    int owns_output_fd;
    int started;
};

struct ktrace_recording {
    char *name;
    int flags;
    FILE *stdout_stream;
    FILE *stderr_stream;
};

ktrace_session_t ktrace_session_create(void) {
    struct ktrace_session *session = calloc(1, sizeof(*session));
    if (session != NULL)
        session->output_fd = -1;
    return session;
}

void ktrace_session_free(ktrace_session_t session) {
    if (session == NULL)
        return;
    if (session->output_fd >= 0 && session->owns_output_fd)
        close(session->output_fd);
    free(session);
}

void ktrace_session_destroy(ktrace_session_t session) {
    ktrace_session_free(session);
}

static int ktrace_set_output(ktrace_session_t session, int fd, int owns_fd) {
    if (session == NULL || fd < 0)
        return EINVAL;
    if (session->started)
        return EBUSY;
    if (session->output_fd >= 0 && session->owns_output_fd)
        close(session->output_fd);
    session->output_fd = fd;
    session->owns_output_fd = owns_fd;
    return 0;
}

int ktrace_start_writing_path(ktrace_session_t session, const char *path,
                              mode_t mode) {
    int fd;
    if (path == NULL)
        return EINVAL;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0)
        return errno;
    if (ktrace_set_output(session, fd, 1) != 0) {
        close(fd);
        return EBUSY;
    }
    return 0;
}

int ktrace_start_writing_fd(ktrace_session_t session, int close_fd, int fd,
                            uint64_t flags, uint64_t options) {
    (void)flags;
    (void)options;
    return ktrace_set_output(session, fd, close_fd != 0);
}

int ktrace_start_writing_file(ktrace_session_t session, FILE *file,
                              int fd_or_mode, uint64_t flags, uint64_t options) {
    int fd;
    (void)fd_or_mode;
    (void)flags;
    (void)options;
    if (file == NULL)
        return EINVAL;
    fd = fileno(file);
    if (fd < 0)
        return errno;
    return ktrace_set_output(session, fd, 0);
}

int ktrace_start(ktrace_session_t session) {
    if (session == NULL)
        return EINVAL;
    if (session->started)
        return EBUSY;
    session->started = 1;
    return 0;
}

int ktrace_end(ktrace_session_t session, int flush) {
    int result = 0;
    if (session == NULL)
        return EINVAL;
    if (!session->started)
        return EINVAL;
    if (flush && session->output_fd >= 0 && fsync(session->output_fd) != 0)
        result = errno;
    session->started = 0;
    if (session->output_fd >= 0 && session->owns_output_fd) {
        if (close(session->output_fd) != 0 && result == 0)
            result = errno;
        session->output_fd = -1;
        session->owns_output_fd = 0;
    }
    return result;
}

ktrace_recording_t ktrace_recording_create(const char *name, int flags) {
    struct ktrace_recording *recording;
    if (name == NULL)
        return NULL;
    recording = calloc(1, sizeof(*recording));
    if (recording == NULL)
        return NULL;
    recording->name = strdup(name);
    if (recording->name == NULL) {
        free(recording);
        return NULL;
    }
    recording->flags = flags;
    return recording;
}

void ktrace_recording_destroy(ktrace_recording_t recording) {
    if (recording == NULL)
        return;
    free(recording->name);
    free(recording);
}

void ktrace_recording_set_streams(ktrace_recording_t recording,
                                  FILE *stdout_stream, FILE *stderr_stream) {
    if (recording == NULL)
        return;
    recording->stdout_stream = stdout_stream;
    recording->stderr_stream = stderr_stream;
}
