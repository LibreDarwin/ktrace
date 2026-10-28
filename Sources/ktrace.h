/* Copyright (C) 2026, LibreDarwin */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef LIBREDARWIN_KTRACE_H
#define LIBREDARWIN_KTRACE_H

#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ktrace_session *ktrace_session_t;
typedef struct ktrace_recording *ktrace_recording_t;
typedef struct ktrace_file *ktrace_file_t;
typedef struct ktrace_chunk *ktrace_chunk_t;
typedef int (^ktrace_chunk_callback_t)(ktrace_chunk_t chunk);

ktrace_session_t ktrace_session_create(void);
ktrace_session_t ktrace_session_create_with_flags(uint64_t flags);
void ktrace_session_destroy(ktrace_session_t session);
void ktrace_session_free(ktrace_session_t session);

int ktrace_start(ktrace_session_t session);
int ktrace_end(ktrace_session_t session, int flush);

int ktrace_session_set_default_event_names_enabled(ktrace_session_t session,
                                                   int enabled);
int ktrace_session_set_event_names_enabled(ktrace_session_t session,
                                           int enabled);
int ktrace_session_get_default_event_names_enabled(ktrace_session_t session);
int ktrace_session_get_event_names_enabled(ktrace_session_t session);
int ktrace_session_getting_event_names_from_system(ktrace_session_t session);
int ktrace_session_has_callbacks(ktrace_session_t session);
int ktrace_set_buffer_size(ktrace_session_t session, uint64_t size);
int ktrace_set_continuous_timestamps(ktrace_session_t session, int enabled);
int ktrace_set_coprocessor_tracing_enabled(ktrace_session_t session, int enabled);
int ktrace_set_mode(ktrace_session_t session, int mode);
int ktrace_set_no_postprocessing(ktrace_session_t session);
int ktrace_set_file(ktrace_session_t session, FILE *file);
int ktrace_set_ktrace_file(ktrace_session_t session, ktrace_file_t file,
                           int close_file);
int ktrace_set_thread_groups_enabled(ktrace_session_t session, int enabled);
int ktrace_set_use_existing(ktrace_session_t session);
int ktrace_set_vnode_paths_enabled(ktrace_session_t session, int enabled);

int ktrace_start_writing_path(ktrace_session_t session, const char *path,
                              mode_t mode);
int ktrace_start_writing_fd(ktrace_session_t session, int close_fd, int fd,
                            uint64_t flags, uint64_t options);
int ktrace_start_writing_file(ktrace_session_t session, FILE *file,
                              int fd_or_mode, uint64_t flags, uint64_t options);

ktrace_file_t ktrace_file_open(const char *path, int writable);
ktrace_file_t ktrace_file_open_fd(int fd, int writable);
ktrace_file_t ktrace_file_create(void *machine, const char *path);
void ktrace_file_close(ktrace_file_t file);
ktrace_chunk_t ktrace_file_append_start(ktrace_file_t file, uint32_t tag,
                                        uint16_t major_version,
                                        uint16_t minor_version);
int ktrace_file_append_data(ktrace_file_t file, ktrace_chunk_t chunk,
                            const void *data, size_t size);
int ktrace_file_append_finish(ktrace_file_t file, ktrace_chunk_t chunk);
int ktrace_file_append_stackshot(ktrace_file_t file, const void *data, size_t size);
int ktrace_file_iterate(ktrace_file_t file, uint64_t offset,
                        ktrace_chunk_callback_t callback);
int ktrace_file_header_iterate(ktrace_file_t file, uint64_t offset,
                               ktrace_chunk_callback_t callback);
int ktrace_file_earliest_timestamp(ktrace_file_t file, uint64_t *timestamp);
int ktrace_file_latest_timestamp(ktrace_file_t file, uint64_t *timestamp);
int ktrace_file_timebase(ktrace_file_t file, uint32_t *numerator,
                         uint32_t *denominator);
int ktrace_file_supports_continuous_time(ktrace_file_t file);
int ktrace_file_machtime_to_walltime(ktrace_file_t file,
                                     struct timeval *walltime,
                                     uint64_t machtime);
int ktrace_file_uuid(ktrace_file_t file, unsigned char uuid[16]);
int64_t ktrace_file_ktrace_event_count(ktrace_file_t file);

uint32_t ktrace_chunk_tag(ktrace_chunk_t chunk);
uint16_t ktrace_chunk_version_major(ktrace_chunk_t chunk);
uint16_t ktrace_chunk_version_minor(ktrace_chunk_t chunk);
uint64_t ktrace_chunk_size(ktrace_chunk_t chunk);
uint64_t ktrace_chunk_size_t(ktrace_chunk_t chunk);
const void *ktrace_chunk_map_data(ktrace_chunk_t chunk, uint64_t offset,
                                  uint64_t size);
void ktrace_chunk_unmap_data(ktrace_chunk_t chunk, const void *data,
                             uint64_t size);

ktrace_recording_t ktrace_recording_create(const char *name, int flags);
void ktrace_recording_destroy(ktrace_recording_t recording);
void ktrace_recording_set_streams(ktrace_recording_t recording,
                                  FILE *stdout_stream, FILE *stderr_stream);

#ifdef __cplusplus
}
#endif

#endif
