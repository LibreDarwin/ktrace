/* Copyright (C) 2026, LibreDarwin */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "ktrace.h"

#include <errno.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
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
    uint32_t timebase_numerator;
    uint32_t timebase_denominator;
    unsigned char uuid[16];
    int uuid_valid;
    ktrace_chunk_t active_chunk;
};

struct ktrace_chunk {
    struct ktrace_file *file;
    off_t rollback_offset;
    off_t header_offset;
    off_t payload_offset;
    uint64_t payload_length;
    uint32_t tag;
    uint32_t version;
    unsigned char *mapped_data;
    uint64_t mapped_size;
};

struct ktrace_chunk_header {
    uint32_t tag;
    uint32_t version;
    uint64_t payload_length;
};

static const unsigned char ktrace_empty_header_plist[] = {
    0x62, 0x70, 0x6c, 0x69, 0x73, 0x74, 0x30, 0x30,
    0xd0, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x09,
};

static int ktrace_write_all(int fd, const void *data, size_t size) {
    const unsigned char *bytes = data;
    while (size != 0) {
        ssize_t written = write(fd, bytes, size);
        if (written < 0)
            return errno;
        if (written == 0)
            return EIO;
        bytes += written;
        size -= (size_t)written;
    }
    return 0;
}

static int ktrace_file_write_header(int fd, const unsigned char uuid[16]) {
    static const unsigned char prefix[32] = {
        0x7d, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    };
    unsigned char padding[8] = {0};
    uint64_t magic = 0x0000000155aa0300ULL;
    uint64_t header_length = 136;
    uint32_t zero = 0;
    uint32_t one = 1;
    uint32_t plist_tag = 0x8c00;
    uint32_t plist_type = 0x80001;
    uint32_t plist_length = sizeof(ktrace_empty_header_plist);
    uint32_t uuid_tag = 0x8c01;
    uint32_t uuid_length = 16;
    int result;

    result = ktrace_write_all(fd, &magic, sizeof(magic));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &header_length, sizeof(header_length));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, prefix, sizeof(prefix));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &zero, sizeof(zero));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &one, sizeof(one));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &plist_tag, sizeof(plist_tag));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &plist_type, sizeof(plist_type));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &plist_length, sizeof(plist_length));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &zero, sizeof(zero));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, ktrace_empty_header_plist,
                              sizeof(ktrace_empty_header_plist));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, padding, 6);
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &uuid_tag, sizeof(uuid_tag));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &zero, sizeof(zero));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &uuid_length, sizeof(uuid_length));
    if (result != 0)
        return result;
    result = ktrace_write_all(fd, &zero, sizeof(zero));
    if (result != 0)
        return result;
    return ktrace_write_all(fd, uuid, 16);
}

struct ktrace_compressor {
    uint32_t row;
    uint32_t mode;
    uint64_t timestamp;
    unsigned char rows[17][48];
};

/* Native 0x1f00 event chunks use a 17-row adaptive delta codec. */

static uint64_t ktrace_load_u64(const unsigned char *bytes) {
    uint64_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static void ktrace_store_u64(unsigned char *bytes, uint64_t value) {
    memcpy(bytes, &value, sizeof(value));
}

static size_t ktrace_compressor_decode(struct ktrace_compressor *state,
                                       const unsigned char *input,
                                       size_t length) {
    unsigned char short_input[72];
    unsigned char *data;
    uint16_t control;
    uint64_t timestamp_delta;
    uint64_t word;
    uint32_t selector;
    uint32_t previous;
    size_t offset;
    size_t field;

    if (length == 0)
        return 0;
    if (length <= 64) {
        memset(short_input, 0, sizeof(short_input));
        memcpy(short_input, input, length);
        data = short_input;
    } else {
        data = (unsigned char *)input;
    }
    control = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    selector = state->row + (data[1] >> 4);
    if (selector > 16)
        selector -= 17;

    word = ktrace_load_u64(data + 2);
    if ((word & 0xff) != 0) {
        unsigned int zeros = (unsigned int)__builtin_ctzll(word);
        word <<= 56 - (zeros << 3);
        word >>= 57 - (zeros * 7);
        timestamp_delta = word;
        offset = zeros + 3;
    } else {
        timestamp_delta = ktrace_load_u64(data + 3);
        offset = 11;
    }

    previous = state->row == 0 ? 16 : state->row - 1;
    state->timestamp += timestamp_delta;
    for (field = 0; field < 6; field++) {
        uint64_t value;
        unsigned int kind = control & 3;
        control >>= 2;
        if (kind == 0) {
            value = 0;
        } else if (kind == 1) {
            value = ktrace_load_u64(state->rows[selector] + field * 8);
        } else if (kind == 2) {
            value = ktrace_load_u64(state->rows[selector] + field * 8) ^ 3;
        } else {
            if (offset > length || length - offset < sizeof(uint64_t))
                return 0;
            word = ktrace_load_u64(data + offset);
            if ((word & 0xff) != 0) {
                unsigned int zeros = (unsigned int)__builtin_ctzll(word);
                unsigned int length_bytes = zeros + 1;
                word <<= (-(length_bytes << 3)) & 63;
                value = (uint64_t)((int64_t)word >> (57 - zeros * 7));
                offset += length_bytes;
            } else {
                if (offset > length || length - offset < sizeof(uint64_t) + 1)
                    return 0;
                value = ktrace_load_u64(data + offset + 1);
                offset += 9;
            }
            value += ktrace_load_u64(state->rows[selector] + field * 8);
        }
        ktrace_store_u64(state->rows[previous] + field * 8, value);
    }
    if (offset >= 0x42 || offset > length)
        return 0;
    state->row = previous;
    return offset;
}

static int ktrace_file_scan_timestamps(ktrace_file_t file, int latest,
                                        uint64_t *timestamp) {
    struct stat file_stat;
    unsigned char header[16];
    off_t offset = 0;
    off_t length;
    int found = 0;

    if (fstat(file->fd, &file_stat) != 0)
        return errno;
    length = file_stat.st_size;
    if (length < (off_t)sizeof(header) ||
        pread(file->fd, header, sizeof(header), 0) != (ssize_t)sizeof(header))
        return EINVAL;
    if (ktrace_load_u64(header) == 0x0000000155aa0300ULL) {
        uint64_t header_length = ktrace_load_u64(header + 8);
        if (header_length > (uint64_t)length - sizeof(header) ||
            header_length > SIZE_MAX - sizeof(header))
            return EINVAL;
        offset = (off_t)(sizeof(header) + header_length);
    }
    while (offset < length) {
        struct ktrace_chunk_header chunk;
        unsigned char *payload;
        size_t payload_length;
        ssize_t read_length;
        if (length - offset < (off_t)sizeof(chunk) ||
            pread(file->fd, &chunk, sizeof(chunk), offset) !=
                (ssize_t)sizeof(chunk))
            return EINVAL;
        if (chunk.payload_length > (uint64_t)(length - offset - sizeof(chunk)) ||
            chunk.payload_length > SIZE_MAX)
            return EINVAL;
        payload_length = (size_t)chunk.payload_length;
        payload = malloc(payload_length == 0 ? 1 : payload_length);
        if (payload == NULL)
            return ENOMEM;
        read_length = pread(file->fd, payload, payload_length,
                            offset + (off_t)sizeof(chunk));
        if (read_length != (ssize_t)payload_length) {
            free(payload);
            return EINVAL;
        }
        if (chunk.tag == 0x1f00 && payload_length > 12) {
            struct ktrace_compressor state = {0};
            size_t input_offset = 12;
            uint64_t first_timestamp = 0;
            while (input_offset < payload_length) {
                uint64_t previous_timestamp = state.timestamp;
                size_t consumed = ktrace_compressor_decode(
                    &state, payload + input_offset, payload_length - input_offset);
                if (consumed == 0) {
                    if (first_timestamp == 0 && state.timestamp != previous_timestamp)
                        first_timestamp = state.timestamp;
                    break;
                }
                input_offset += consumed;
                if (first_timestamp == 0)
                    first_timestamp = state.timestamp;
            }
            if (first_timestamp != 0) {
                uint64_t value = latest ? state.timestamp : first_timestamp;
                if (!found || (latest ? value > *timestamp : value < *timestamp))
                    *timestamp = value;
                found = 1;
            }
        } else if (chunk.tag == 0x8007 && latest &&
                   payload_length >= sizeof(uint64_t)) {
            uint64_t value = ktrace_load_u64(payload);
            if (!found || (latest ? value > *timestamp : value < *timestamp))
                *timestamp = value;
            found = 1;
        }
        free(payload);
        if (payload_length > SIZE_MAX - sizeof(chunk) - 7) return EINVAL;
        offset += (off_t)((sizeof(chunk) + payload_length + 7) & ~(size_t)7);
    }
    return found ? 0 : ENOTSUP;
}

static void ktrace_session_close_file(ktrace_session_t session) {
    if (session == NULL || session->ktrace_file == NULL)
        return;
    if (session->close_ktrace_file)
        ktrace_file_close(session->ktrace_file);
    session->ktrace_file = NULL;
    session->close_ktrace_file = 0;
}

static void ktrace_file_discard_active_chunk(ktrace_file_t file) {
    if (file == NULL || file->active_chunk == NULL)
        return;
    if (file->writable)
        (void)ftruncate(file->fd, file->active_chunk->rollback_offset);
    free(file->active_chunk);
    file->active_chunk = NULL;
}

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
    ktrace_session_close_file(session);
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
    mach_timebase_info_data_t timebase = {0, 0};
    if (mach_timebase_info(&timebase) == KERN_SUCCESS) {
        file->timebase_numerator = timebase.numer;
        file->timebase_denominator = timebase.denom;
    }
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
    int fd;
    struct ktrace_file *file;
    (void)machine;
    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }
    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return NULL;
    file = ktrace_file_open_fd(fd, 1);
    if (file == NULL) {
        close(fd);
        return NULL;
    }
    file->owns_fd = 1;
    arc4random_buf(file->uuid, sizeof(file->uuid));
    file->uuid_valid = 1;
    if (ktrace_file_write_header(fd, file->uuid) != 0) {
        ktrace_file_close(file);
        return NULL;
    }
    return file;
}

void ktrace_file_close(ktrace_file_t file) {
    if (file == NULL)
        return;
    ktrace_file_discard_active_chunk(file);
    if (file->owns_fd)
        close(file->fd);
    free(file);
}

ktrace_chunk_t ktrace_file_append_start(ktrace_file_t file, uint32_t tag,
                                        uint16_t major_version,
                                        uint16_t minor_version) {
    struct ktrace_chunk_header header = {
        .tag = tag,
        .version = (uint32_t)major_version | ((uint32_t)minor_version << 16),
        .payload_length = 0,
    };
    struct ktrace_chunk *chunk;
    off_t offset;
    unsigned char padding[8] = {0};
    size_t padding_length;
    if (file == NULL || !file->writable || file->active_chunk != NULL)
        return NULL;
    offset = lseek(file->fd, 0, SEEK_END);
    if (offset < 0)
        return NULL;
    padding_length = (size_t)((8 - (offset & 7)) & 7);
    if (padding_length != 0 &&
        write(file->fd, padding, padding_length) != (ssize_t)padding_length) {
        (void)ftruncate(file->fd, offset);
        return NULL;
    }
    if (write(file->fd, &header, sizeof(header)) != (ssize_t)sizeof(header)) {
        (void)ftruncate(file->fd, offset);
        return NULL;
    }
    chunk = calloc(1, sizeof(*chunk));
    if (chunk == NULL) {
        ftruncate(file->fd, offset);
        return NULL;
    }
    chunk->rollback_offset = offset;
    chunk->header_offset = offset + (off_t)padding_length;
    chunk->payload_offset = chunk->header_offset + (off_t)sizeof(header);
    chunk->file = file;
    chunk->tag = tag;
    chunk->version = header.version;
    file->active_chunk = chunk;
    return chunk;
}

int ktrace_file_append_data(ktrace_file_t file, ktrace_chunk_t chunk,
                            const void *data, size_t size) {
    const char *bytes = data;
    size_t remaining = size;
    if (file == NULL || chunk == NULL || file->active_chunk != chunk ||
        (data == NULL && size != 0))
        return EINVAL;
    if (!file->writable)
        return EBADF;
    while (remaining != 0) {
        ssize_t written = write(file->fd, bytes, remaining);
        if (written < 0)
            return errno;
        if (written == 0)
            return EIO;
        bytes += written;
        remaining -= (size_t)written;
    }
    chunk->payload_length += size;
    return 0;
}

int ktrace_file_append_finish(ktrace_file_t file, ktrace_chunk_t chunk) {
    struct ktrace_chunk_header header;
    if (file == NULL || chunk == NULL || file->active_chunk != chunk)
        return EINVAL;
    header.tag = chunk->tag;
    header.version = chunk->version;
    header.payload_length = chunk->payload_length;
    if (pwrite(file->fd, &header, sizeof(header), chunk->header_offset) !=
        (ssize_t)sizeof(header)) {
        int error = errno != 0 ? errno : EIO;
        ktrace_file_discard_active_chunk(file);
        return error;
    }
    file->active_chunk = NULL;
    free(chunk);
    return 0;
}

static int ktrace_file_read_chunk_header(ktrace_file_t file, off_t offset,
                                         struct ktrace_chunk_header *header) {
    if (pread(file->fd, header, sizeof(*header), offset) !=
        (ssize_t)sizeof(*header))
        return errno != 0 ? errno : EINVAL;
    return 0;
}

static int ktrace_file_iteration_start(ktrace_file_t file, uint64_t offset,
                                       off_t *start, off_t *end) {
    struct stat file_stat;
    unsigned char header[16];
    uint64_t header_length;

    if (fstat(file->fd, &file_stat) != 0)
        return errno;
    if (offset > (uint64_t)file_stat.st_size)
        return EINVAL;
    *start = (off_t)offset;
    *end = file_stat.st_size;
    if (offset != 0 || file_stat.st_size < (off_t)sizeof(header) ||
        pread(file->fd, header, sizeof(header), 0) != (ssize_t)sizeof(header) ||
        ktrace_load_u64(header) != 0x0000000155aa0300ULL)
        return 0;
    header_length = ktrace_load_u64(header + 8);
    if (header_length > (uint64_t)file_stat.st_size - sizeof(header) ||
        header_length > SIZE_MAX - sizeof(header))
        return EINVAL;
    *start = (off_t)(sizeof(header) + header_length);
    return 0;
}

static ktrace_chunk_t ktrace_chunk_create(struct ktrace_file *file,
                                          off_t header_offset,
                                          off_t payload_offset,
                                          const struct ktrace_chunk_header *header) {
    struct ktrace_chunk *chunk = calloc(1, sizeof(*chunk));
    if (chunk == NULL)
        return NULL;
    chunk->file = file;
    chunk->header_offset = header_offset;
    chunk->payload_offset = payload_offset;
    chunk->payload_length = header->payload_length;
    chunk->tag = header->tag;
    chunk->version = header->version;
    return chunk;
}

static int ktrace_file_iterate_range(ktrace_file_t file, off_t start, off_t end,
                                     ktrace_chunk_callback_t callback) {
    off_t offset = start;
    while (offset < end) {
        struct ktrace_chunk_header header;
        off_t next;
        off_t payload_end;
        ktrace_chunk_t chunk;
        int result;

        if (end - offset < (off_t)sizeof(header))
            return EINVAL;
        result = ktrace_file_read_chunk_header(file, offset, &header);
        if (result != 0)
            return result;
        if (header.payload_length > (uint64_t)(end - offset - sizeof(header)) ||
            header.payload_length > SIZE_MAX)
            return EINVAL;
        payload_end = offset + (off_t)sizeof(header) + (off_t)header.payload_length;
        next = payload_end;
        next = (off_t)(((uint64_t)next + 7) & ~UINT64_C(7));
        if (next > end && payload_end != end)
            return EINVAL;
        if (next > end)
            next = end;
        chunk = ktrace_chunk_create(file, offset,
                                    offset + (off_t)sizeof(header), &header);
        if (chunk == NULL)
            return ENOMEM;
        result = callback(chunk);
        free(chunk->mapped_data);
        free(chunk);
        if (result == 0)
            break;
        offset = next;
    }
    return 0;
}

int ktrace_file_iterate(ktrace_file_t file, uint64_t offset,
                        ktrace_chunk_callback_t callback) {
    off_t start;
    off_t end;
    int result;
    if (file == NULL || callback == NULL)
        return EINVAL;
    result = ktrace_file_iteration_start(file, offset, &start, &end);
    if (result != 0)
        return result;
    return ktrace_file_iterate_range(file, start, end, callback);
}

int ktrace_file_header_iterate(ktrace_file_t file, uint64_t offset,
                               ktrace_chunk_callback_t callback) {
    struct stat file_stat;
    unsigned char header[16];
    uint64_t header_length;
    off_t start;
    off_t end;
    (void)offset;
    if (file == NULL || callback == NULL)
        return EINVAL;
    if (fstat(file->fd, &file_stat) != 0)
        return errno;
    if (file_stat.st_size < (off_t)sizeof(header) ||
        pread(file->fd, header, sizeof(header), 0) != (ssize_t)sizeof(header) ||
        ktrace_load_u64(header) != 0x0000000155aa0300ULL)
        return ENOTSUP;
    header_length = ktrace_load_u64(header + 8);
    if (header_length > (uint64_t)file_stat.st_size - sizeof(header) ||
        header_length > SIZE_MAX - sizeof(header))
        return EINVAL;
    start = sizeof(header);
    end = start + (off_t)header_length;
    while (start + 24 <= end) {
        uint32_t tag;
        uint64_t size;
        struct ktrace_chunk_header chunk_header;
        ktrace_chunk_t chunk;
        int result;

        if (pread(file->fd, &tag, sizeof(tag), start + 8) != sizeof(tag) ||
            pread(file->fd, &size, sizeof(size), start + 16) != sizeof(size))
            return EINVAL;
        if ((tag != 0x8c00 && tag != 0x8c01 && tag != 0x1c00) ||
            size > (uint64_t)(end - start - 24)) {
            start += 8;
            continue;
        }
        chunk_header.tag = tag;
        chunk_header.version = 0;
        chunk_header.payload_length = size;
        chunk = ktrace_chunk_create(file, start, start + 24, &chunk_header);
        if (chunk == NULL)
            return ENOMEM;
        result = callback(chunk);
        free(chunk);
        if (result == 0)
            return 0;
        /* KCData containers may insert framing bytes between records. */
        start += 8;
    }
    return 0;
}

uint32_t ktrace_chunk_tag(ktrace_chunk_t chunk) {
    return chunk == NULL ? 0 : chunk->tag;
}

uint16_t ktrace_chunk_version_major(ktrace_chunk_t chunk) {
    return chunk == NULL ? 0 : (uint16_t)(chunk->version & 0x7fff);
}

uint16_t ktrace_chunk_version_minor(ktrace_chunk_t chunk) {
    return chunk == NULL ? 0 : (uint16_t)(chunk->version >> 16);
}

uint64_t ktrace_chunk_size(ktrace_chunk_t chunk) {
    return chunk == NULL ? 0 : chunk->payload_length;
}

uint64_t ktrace_chunk_size_t(ktrace_chunk_t chunk) {
    return ktrace_chunk_size(chunk);
}

const void *ktrace_chunk_map_data(ktrace_chunk_t chunk, uint64_t offset,
                                  uint64_t size) {
    unsigned char *data;
    if (chunk == NULL || offset > chunk->payload_length ||
        size > chunk->payload_length - offset)
        return NULL;
    data = malloc(size == 0 ? 1 : (size_t)size);
    if (data == NULL)
        return NULL;
    if (size != 0 && pread(chunk->file->fd, data, (size_t)size,
                            chunk->payload_offset + (off_t)offset) !=
                         (ssize_t)size) {
        free(data);
        return NULL;
    }
    chunk->mapped_data = data;
    chunk->mapped_size = size;
    return data;
}

void ktrace_chunk_unmap_data(ktrace_chunk_t chunk, const void *data,
                             uint64_t size) {
    (void)size;
    if (chunk == NULL || data == NULL || data != chunk->mapped_data)
        return;
    free(chunk->mapped_data);
    chunk->mapped_data = NULL;
    chunk->mapped_size = 0;
}

int ktrace_file_append_stackshot(ktrace_file_t file, const void *data, size_t size) {
    static const unsigned char kcdata_magic[] = {0x07, 0x58, 0xa2, 0x59};
    ktrace_chunk_t chunk;
    int result;
    if (file == NULL || data == NULL || size < sizeof(kcdata_magic) ||
        memcmp(data, kcdata_magic, sizeof(kcdata_magic)) != 0)
        return EINVAL;
    chunk = ktrace_file_append_start(file, 0x8002, 1, 0);
    if (chunk == NULL)
        return errno != 0 ? errno : EIO;
    result = ktrace_file_append_data(file, chunk, data, size);
    if (result == 0)
        result = ktrace_file_append_finish(file, chunk);
    else
        ktrace_file_discard_active_chunk(file);
    return result;
}

int ktrace_file_earliest_timestamp(ktrace_file_t file, uint64_t *timestamp) {
    if (file == NULL || timestamp == NULL)
        return EINVAL;
    *timestamp = 0;
    return ktrace_file_scan_timestamps(file, 0, timestamp);
}

int ktrace_file_latest_timestamp(ktrace_file_t file, uint64_t *timestamp) {
    if (file == NULL || timestamp == NULL)
        return EINVAL;
    *timestamp = 0;
    return ktrace_file_scan_timestamps(file, 1, timestamp);
}

int ktrace_file_timebase(ktrace_file_t file, uint32_t *numerator,
                         uint32_t *denominator) {
    if (file == NULL || numerator == NULL || denominator == NULL)
        return EINVAL;
    if (file->timebase_numerator == 0 || file->timebase_denominator == 0)
        return ENOTSUP;
    *numerator = file->timebase_numerator;
    *denominator = file->timebase_denominator;
    return 0;
}

int ktrace_file_supports_continuous_time(ktrace_file_t file) {
    __block int supported = 0;
    if (file == NULL)
        return 0;
    if (ktrace_file_iterate(file, 0, ^int(ktrace_chunk_t chunk) {
        if (ktrace_chunk_tag(chunk) == 0x1f00)
            supported = 1;
        return supported ? 0 : 1;
    }) != 0)
        return 0;
    return supported;
}

int ktrace_file_machtime_to_walltime(ktrace_file_t file,
                                     struct timeval *walltime,
                                     uint64_t machtime) {
    struct timeval now;
    uint64_t current_machtime;
    long double nanoseconds;
    long double seconds;
    long double microseconds;

    if (file == NULL || walltime == NULL)
        return EINVAL;
    if (file->timebase_numerator == 0 || file->timebase_denominator == 0)
        return ENOTSUP;
    if (gettimeofday(&now, NULL) != 0)
        return errno;
    current_machtime = mach_absolute_time();
    if (machtime == 0)
        machtime = current_machtime;
    nanoseconds = ((long double)machtime - (long double)current_machtime) *
                   file->timebase_numerator / file->timebase_denominator;
    seconds = (long double)now.tv_sec + (long double)now.tv_usec / 1000000.0L;
    seconds += nanoseconds / 1000000000.0L;
    microseconds = seconds * 1000000.0L;
    walltime->tv_sec = (time_t)(microseconds / 1000000.0L);
    walltime->tv_usec = (suseconds_t)(microseconds -
                                      (long double)walltime->tv_sec * 1000000.0L);
    if (walltime->tv_usec < 0) {
        walltime->tv_sec--;
        walltime->tv_usec += 1000000;
    }
    return 0;
}

int ktrace_file_uuid(ktrace_file_t file, unsigned char uuid[16]) {
    __block int found = 0;
    if (file == NULL || uuid == NULL)
        return EINVAL;
    if (file->uuid_valid) {
        memcpy(uuid, file->uuid, sizeof(file->uuid));
        return 0;
    }
    if (ktrace_file_header_iterate(file, 0, ^int(ktrace_chunk_t chunk) {
        const void *data;
        if (ktrace_chunk_tag(chunk) != 0x8c01 ||
            ktrace_chunk_size(chunk) != sizeof(file->uuid))
            return 1;
        data = ktrace_chunk_map_data(chunk, 0, sizeof(file->uuid));
        if (data == NULL)
            return 1;
        memcpy(uuid, data, sizeof(file->uuid));
        memcpy(file->uuid, data, sizeof(file->uuid));
        file->uuid_valid = 1;
        ktrace_chunk_unmap_data(chunk, data, sizeof(file->uuid));
        found = 1;
        return 0;
    }) != 0)
        return EINVAL;
    return found ? 0 : ENOTSUP;
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
    ktrace_session_close_file(session);
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
