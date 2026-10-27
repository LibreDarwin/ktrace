Copyright (C) 2026, LibreDarwin
SPDX-License-Identifier: BSD-3-Clause

# Reconstructed C ABI

The following signatures are confirmed by the local `libktrace.tbd` export
list and AArch64 call sites in `/usr/bin/ktrace`:

```c
typedef struct ktrace_session *ktrace_session_t;
typedef struct ktrace_recording *ktrace_recording_t;

ktrace_session_t ktrace_session_create(void);
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
