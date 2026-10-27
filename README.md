# LibreDarwin ktrace

This repository contains the clean-room `ktrace.framework` reconstruction.

The initial implementation provides the confirmed C session and output ABI:

- `ktrace_session_create`, `ktrace_session_destroy`, and `ktrace_session_free`
- `ktrace_start` and `ktrace_end`
- `ktrace_start_writing_path`, `ktrace_start_writing_fd`, and
  `ktrace_start_writing_file`
- `ktrace_set_file` and `ktrace_set_ktrace_file` output-object setters
- Confirmed scalar session controls for buffer size, mode, event names,
  timestamping, coprocessor tracing, thread groups, existing traces, vnode
  paths, and postprocessing.
- `ktrace_recording_create`, `ktrace_recording_destroy`, and
  `ktrace_recording_set_streams`

Event collection, kdebug configuration, postprocessing, and symbolication are
not implemented until their private layouts and callback contracts are
reconstructed from call sites. The implemented session layer deliberately
does not emit guessed trace bytes.
