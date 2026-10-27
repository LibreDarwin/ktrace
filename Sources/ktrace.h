/* Copyright (C) 2026, LibreDarwin */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef LIBREDARWIN_KTRACE_H
#define LIBREDARWIN_KTRACE_H

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ktrace_session *ktrace_session_t;
typedef struct ktrace_recording *ktrace_recording_t;

ktrace_session_t ktrace_session_create(void);
void ktrace_session_destroy(ktrace_session_t session);
void ktrace_session_free(ktrace_session_t session);

int ktrace_start(ktrace_session_t session);
int ktrace_end(ktrace_session_t session, int flush);

int ktrace_start_writing_path(ktrace_session_t session, const char *path,
                              mode_t mode);
int ktrace_start_writing_fd(ktrace_session_t session, int close_fd, int fd,
                            uint64_t flags, uint64_t options);
int ktrace_start_writing_file(ktrace_session_t session, FILE *file,
                              int fd_or_mode, uint64_t flags, uint64_t options);

ktrace_recording_t ktrace_recording_create(const char *name, int flags);
void ktrace_recording_destroy(ktrace_recording_t recording);
void ktrace_recording_set_streams(ktrace_recording_t recording,
                                  FILE *stdout_stream, FILE *stderr_stream);

#ifdef __cplusplus
}
#endif

#endif
