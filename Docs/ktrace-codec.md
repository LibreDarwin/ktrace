# Ktrace Codec Notes

The native `ktrace_file_earliest_timestamp` and
`ktrace_file_latest_timestamp` implementations scan decoded event chunks. They
do not use the `0x8013` file-boundary payload as an event timestamp.

## Event Chunks

- `0x1f00` contains compressed events after a 12-byte chunk-local prefix.
- The compressed stream decodes into 64-byte records.
- The first 8 bytes of each decoded record are the accumulated Mach timestamp.
- The remaining event state is a 48-byte row followed by 8 bytes of padding.

## Compression

The decoder maintains 17 adaptive rows of 48 bytes. Each compressed record has:

- A 16-bit control word containing six 2-bit field modes.
- A variable-length timestamp delta.
- Six field values, encoded as zero, copied row value, row value XOR 3, or a
  signed variable-length delta from a selected row.
- A 4-bit row selector derived from the control header and the previous row.

The implementation mirrors the native arm64e decoder and rejects records whose
encoded length exceeds `0x41` bytes or the remaining chunk payload. Native
captures may end with a terminal record whose timestamp delta is present but
whose event fields are truncated; the timestamp is still included in the
latest bound.

## Supported Timestamp Paths

- `0x1f00`: full adaptive decoder, matching native earliest/latest values on
  the captured macOS 26.5 trace.
- `0x8007` chunks with at least eight payload bytes: latest-bound marker only;
  the first 64-bit payload value is not used for the earliest bound.
- Other event chunk families (`0x1e00`, `0x51f6`) remain a follow-up phase;
  native code handles them through its trace stream abstraction.
