Copyright (C) 2026, LibreDarwin
SPDX-License-Identifier: BSD-3-Clause

# Reconstructed C ABI

The following signatures are confirmed by the local `libktrace.tbd` export
list and AArch64 call sites in `/usr/bin/ktrace`:

```c
typedef struct ktrace_session *ktrace_session_t;
typedef struct ktrace_recording *ktrace_recording_t;

ktrace_session_t ktrace_session_create(void);
ktrace_session_t ktrace_session_create_with_flags(uint64_t flags);
void ktrace_session_destroy(ktrace_session_t);
int ktrace_start(ktrace_session_t);
int ktrace_end(ktrace_session_t, int flush);
int ktrace_start_writing_path(ktrace_session_t, const char *, mode_t);
int ktrace_start_writing_fd(ktrace_session_t, int close_fd, int fd,
                            uint64_t flags, uint64_t options);
int ktrace_start_writing_file(ktrace_session_t, FILE *, int fd_or_mode,
                              uint64_t flags, uint64_t options);
ktrace_recording_t ktrace_recording_create(const char *, int flags);
void ktrace_recording_set_streams(ktrace_recording_t, FILE *, FILE *);
typedef struct ktrace_file *ktrace_file_t;
typedef struct ktrace_chunk *ktrace_chunk_t;
typedef int (^ktrace_chunk_callback_t)(ktrace_chunk_t);
ktrace_file_t ktrace_file_open(const char *, int writable);
ktrace_file_t ktrace_file_create(void *, const char *);
void ktrace_file_close(ktrace_file_t);
int ktrace_file_earliest_timestamp(ktrace_file_t, uint64_t *);
int ktrace_file_latest_timestamp(ktrace_file_t, uint64_t *);
int ktrace_file_timebase(ktrace_file_t, uint32_t *, uint32_t *);
int ktrace_file_supports_continuous_time(ktrace_file_t);
int ktrace_file_machtime_to_walltime(ktrace_file_t, struct timeval *, uint64_t);
int ktrace_file_uuid(ktrace_file_t, unsigned char uuid[16]);
int64_t ktrace_file_ktrace_event_count(ktrace_file_t);
ktrace_chunk_t ktrace_file_append_start(ktrace_file_t, uint32_t, uint16_t, uint16_t);
int ktrace_file_append_data(ktrace_file_t, ktrace_chunk_t, const void *, size_t);
int ktrace_file_append_finish(ktrace_file_t, ktrace_chunk_t);
int ktrace_file_append_stackshot(ktrace_file_t, const void *, size_t);
int ktrace_file_iterate(ktrace_file_t, uint64_t, ktrace_chunk_callback_t);
int ktrace_file_header_iterate(ktrace_file_t, uint64_t, ktrace_chunk_callback_t);
uint32_t ktrace_chunk_tag(ktrace_chunk_t);
uint16_t ktrace_chunk_version_major(ktrace_chunk_t);
uint16_t ktrace_chunk_version_minor(ktrace_chunk_t);
uint64_t ktrace_chunk_size(ktrace_chunk_t);
uint64_t ktrace_chunk_size_t(ktrace_chunk_t);
const void *ktrace_chunk_map_data(ktrace_chunk_t, uint64_t offset, uint64_t size);
void ktrace_chunk_unmap_data(ktrace_chunk_t, const void *, uint64_t size);
int ktrace_session_get_default_event_names_enabled(ktrace_session_t);
int ktrace_session_get_event_names_enabled(ktrace_session_t);
```

The scalar session controls are called with the session in `x0` and their
scalar value in `x1`. The framework call sites do not consume their return
values, so the implementation exposes deterministic integer status results
for local callers while preserving the argument ABI:

```c
int ktrace_set_buffer_size(ktrace_session_t, uint64_t);
int ktrace_set_mode(ktrace_session_t, int);
int ktrace_set_continuous_timestamps(ktrace_session_t, int);
int ktrace_set_coprocessor_tracing_enabled(ktrace_session_t, int);
int ktrace_set_thread_groups_enabled(ktrace_session_t, int);
int ktrace_set_vnode_paths_enabled(ktrace_session_t, int);
int ktrace_set_no_postprocessing(ktrace_session_t);
int ktrace_set_use_existing(ktrace_session_t);
int ktrace_set_file(ktrace_session_t, FILE *);
int ktrace_set_ktrace_file(ktrace_session_t, void *, int close_file);
```

Callback-bearing setters remain unimplemented until their block layouts and
ownership rules are reconstructed.
