/* Copyright (C) 2026, LibreDarwin */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "ktrace.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct ktrace_session {
    uint64_t flags;
    int output_fd;
    int owns_output_fd;
    int started;
    uint64_t buffer_size;
    int default_event_names_enabled;
    int event_names_enabled;
    int continuous_timestamps;
    int coprocessor_tracing_enabled;
    int mode;
    int no_postprocessing;
    int thread_groups_enabled;
    int use_existing;
    int vnode_paths_enabled;
    FILE *file;
    void *ktrace_file;
    int close_ktrace_file;
};

struct ktrace_recording {
    char *name;
    int flags;
    FILE *stdout_stream;
    FILE *stderr_stream;
};

struct ktrace_file {
    int fd;
    int owns_fd;
    int writable;
};

ktrace_session_t ktrace_session_create_with_flags(uint64_t flags) {
    struct ktrace_session *session = calloc(1, sizeof(*session));
    if (session != NULL) {
        session->output_fd = -1;
        session->flags = flags;
    }
    return session;
}

ktrace_session_t ktrace_session_create(void) {
    return ktrace_session_create_with_flags(0);
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

static int ktrace_set_bool(int *slot, int enabled) {
    if (slot == NULL)
        return EINVAL;
    *slot = enabled != 0;
    return 0;
}

int ktrace_session_set_default_event_names_enabled(ktrace_session_t session,
                                                   int enabled) {
    return session == NULL ? EINVAL :
        ktrace_set_bool(&session->default_event_names_enabled, enabled);
}

int ktrace_session_set_event_names_enabled(ktrace_session_t session, int enabled) {
    return session == NULL ? EINVAL :
        ktrace_set_bool(&session->event_names_enabled, enabled);
}

int ktrace_session_get_default_event_names_enabled(ktrace_session_t session) {
    return session == NULL ? 0 : session->default_event_names_enabled;
}

int ktrace_session_get_event_names_enabled(ktrace_session_t session) {
    return session == NULL ? 0 : session->event_names_enabled;
}

int ktrace_session_getting_event_names_from_system(ktrace_session_t session) {
    return session == NULL ? 0 : session->event_names_enabled == 0;
}

int ktrace_session_has_callbacks(ktrace_session_t session) {
    (void)session;
    return 0;
}

int ktrace_set_buffer_size(ktrace_session_t session, uint64_t size) {
    if (session == NULL || size == 0)
        return EINVAL;
    session->buffer_size = size;
    return 0;
}

int ktrace_set_continuous_timestamps(ktrace_session_t session, int enabled) {
    return session == NULL ? EINVAL :
        ktrace_set_bool(&session->continuous_timestamps, enabled);
}

int ktrace_set_coprocessor_tracing_enabled(ktrace_session_t session, int enabled) {
    return session == NULL ? EINVAL :
        ktrace_set_bool(&session->coprocessor_tracing_enabled, enabled);
}

int ktrace_set_mode(ktrace_session_t session, int mode) {
    if (session == NULL)
        return EINVAL;
    session->mode = mode;
    return 0;
}

int ktrace_set_no_postprocessing(ktrace_session_t session) {
    return session == NULL ? EINVAL : ktrace_set_bool(&session->no_postprocessing, 1);
}

int ktrace_set_file(ktrace_session_t session, FILE *file) {
    if (session == NULL || file == NULL)
        return EINVAL;
    if (session->started)
        return EBUSY;
    session->file = file;
    return 0;
}

int ktrace_set_ktrace_file(ktrace_session_t session, ktrace_file_t file,
                           int close_file) {
    if (session == NULL || file == NULL)
        return EINVAL;
    if (session->started)
        return EBUSY;
    session->ktrace_file = file;
    session->close_ktrace_file = close_file != 0;
    return 0;
}

int ktrace_set_thread_groups_enabled(ktrace_session_t session, int enabled) {
    return session == NULL ? EINVAL :
        ktrace_set_bool(&session->thread_groups_enabled, enabled);
}

int ktrace_set_use_existing(ktrace_session_t session) {
    if (session == NULL)
        return EINVAL;
    session->use_existing = 1;
    return 1;
}

int ktrace_set_vnode_paths_enabled(ktrace_session_t session, int enabled) {
    return session == NULL ? EINVAL :
        ktrace_set_bool(&session->vnode_paths_enabled, enabled);
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
    int result;
    if (path == NULL)
        return EINVAL;
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0)
        return errno;
    result = ktrace_set_output(session, fd, 1);
    if (result != 0) {
        close(fd);
        return result;
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

ktrace_file_t ktrace_file_open_fd(int fd, int writable) {
    struct ktrace_file *file;
    if (fd < 0) {
        errno = EBADF;
        return NULL;
    }
    file = calloc(1, sizeof(*file));
    if (file == NULL)
        return NULL;
    file->fd = fd;
    file->writable = writable != 0;
    return file;
}

ktrace_file_t ktrace_file_open(const char *path, int writable) {
    int fd;
    struct ktrace_file *file;
    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    fd = open(path, writable ? O_RDWR : O_RDONLY);
    if (fd < 0)
        return NULL;
    file = ktrace_file_open_fd(fd, writable);
    if (file == NULL) {
        close(fd);
        return NULL;
    }
    file->owns_fd = 1;
    return file;
}

ktrace_file_t ktrace_file_create(void *machine, const char *path) {
    (void)machine;
    return ktrace_file_open(path, 1);
}

void ktrace_file_close(ktrace_file_t file) {
    if (file == NULL)
        return;
    if (file->owns_fd)
        close(file->fd);
    free(file);
}

int ktrace_file_earliest_timestamp(ktrace_file_t file, uint64_t *timestamp) {
    if (file == NULL || timestamp == NULL)
        return EINVAL;
    *timestamp = 0;
    return ENOTSUP;
}

int ktrace_file_latest_timestamp(ktrace_file_t file, uint64_t *timestamp) {
    if (file == NULL || timestamp == NULL)
        return EINVAL;
    *timestamp = 0;
    return ENOTSUP;
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
