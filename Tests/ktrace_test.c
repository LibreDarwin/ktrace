/* Copyright (C) 2026, LibreDarwin */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "ktrace.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

struct test_chunk_header {
    unsigned int tag;
    unsigned int version;
    unsigned long long payload_length;
};

static int test_chunk_count;
static int test_header_count;
static int test_chunk_callback(ktrace_chunk_t chunk) {
    const unsigned char *data;
    test_chunk_count++;
    assert(ktrace_chunk_tag(chunk) == (test_chunk_count == 1 ? 0x8013 : 0x8014));
    assert(ktrace_chunk_version_major(chunk) == (test_chunk_count == 1 ? 1 : 2));
    assert(ktrace_chunk_version_minor(chunk) == (test_chunk_count == 1 ? 0 : 3));
    data = ktrace_chunk_map_data(chunk, 0, ktrace_chunk_size_t(chunk));
    assert(data != NULL);
    if (test_chunk_count == 1)
        assert(memcmp(data, "abc", 3) == 0);
    else
        assert(memcmp(data, "de", 2) == 0);
    ktrace_chunk_unmap_data(chunk, data, ktrace_chunk_size(chunk));
    return 1;
}

static int test_header_callback(ktrace_chunk_t chunk) {
    test_header_count++;
    assert(ktrace_chunk_tag(chunk) == (test_header_count == 1 ? 0x8c00 : 0x8c01));
    return 1;
}

int main(void) {
    char path[] = "/tmp/libredarwin-ktrace.XXXXXX";
    ktrace_session_t session = ktrace_session_create();
    ktrace_session_t flagged_session = ktrace_session_create_with_flags(0);
    ktrace_session_t owned_session = ktrace_session_create();
    ktrace_file_t trace_file;
    FILE *file;
    const off_t file_header_size = 0x98;
    int fd = mkstemp(path);
    assert(session != NULL);
    assert(flagged_session != NULL);
    assert(owned_session != NULL);
    assert(fd >= 0);
    trace_file = ktrace_file_open(path, 0);
    assert(trace_file != NULL);
    assert(ktrace_file_earliest_timestamp(trace_file, NULL) == EINVAL);
    ktrace_file_close(trace_file);
    trace_file = ktrace_file_create(NULL, path);
    assert(trace_file != NULL);
    ktrace_chunk_t chunk = ktrace_file_append_start(trace_file, 0x8013, 1, 0);
    assert(chunk != NULL);
    assert(ktrace_file_append_data(trace_file, chunk, "abc", 3) == 0);
    assert(ktrace_file_append_finish(trace_file, chunk) == 0);
    chunk = ktrace_file_append_start(trace_file, 0x8014, 2, 3);
    assert(chunk != NULL);
    assert(ktrace_file_append_data(trace_file, chunk, "de", 2) == 0);
    assert(ktrace_file_append_finish(trace_file, chunk) == 0);
    test_chunk_count = 0;
    int iterate_result = ktrace_file_iterate(trace_file, 0, ^int(ktrace_chunk_t item) {
        return test_chunk_callback(item);
    });
    assert(iterate_result == 0);
    assert(test_chunk_count == 2);
    test_header_count = 0;
    assert(ktrace_file_header_iterate(trace_file, 0, ^int(ktrace_chunk_t item) {
        return test_header_callback(item);
    }) == 0);
    assert(test_header_count == 2);
    ktrace_file_close(trace_file);
    fd = open(path, O_RDONLY);
    assert(fd >= 0);
    struct test_chunk_header header;
    assert(lseek(fd, file_header_size, SEEK_SET) == file_header_size);
    assert(read(fd, &header, sizeof(header)) == (ssize_t)sizeof(header));
    assert(header.tag == 0x8013);
    assert(header.version == 1);
    assert(header.payload_length == 3);
    unsigned char padding[5];
    assert(lseek(fd, file_header_size + sizeof(header) + 3, SEEK_SET) ==
           file_header_size + (off_t)sizeof(header) + 3);
    assert(read(fd, padding, sizeof(padding)) == (ssize_t)sizeof(padding));
    for (size_t i = 0; i < sizeof(padding); i++)
        assert(padding[i] == 0);
    assert(lseek(fd, file_header_size + 24, SEEK_SET) == file_header_size + 24);
    assert(read(fd, &header, sizeof(header)) == (ssize_t)sizeof(header));
    assert(header.tag == 0x8014);
    assert(header.version == (2U | (3U << 16)));
    assert(header.payload_length == 2);
    trace_file = ktrace_file_create(NULL, path);
    assert(trace_file != NULL);
    const unsigned char stackshot[] = {0x07, 0x58, 0xa2, 0x59, 0x00};
    assert(ktrace_file_append_stackshot(trace_file, stackshot,
                                        sizeof(stackshot)) == 0);
    uint64_t timestamp_value = 123456789;
    chunk = ktrace_file_append_start(trace_file, 0x8007, 0, 0);
    assert(chunk != NULL);
    assert(ktrace_file_append_data(trace_file, chunk, &timestamp_value,
                                   sizeof(timestamp_value)) == 0);
    assert(ktrace_file_append_finish(trace_file, chunk) == 0);
    ktrace_file_close(trace_file);
    trace_file = ktrace_file_open(path, 0);
    assert(trace_file != NULL);
    uint32_t timebase_numerator = 0;
    uint32_t timebase_denominator = 0;
    assert(ktrace_file_timebase(trace_file, &timebase_numerator,
                                &timebase_denominator) == 0);
    assert(timebase_numerator != 0);
    assert(timebase_denominator != 0);
    assert(ktrace_file_supports_continuous_time(trace_file) == 0);
    struct timeval walltime = {0, 0};
    assert(ktrace_file_machtime_to_walltime(trace_file, &walltime, 0) == 0);
    assert(walltime.tv_usec >= 0 && walltime.tv_usec < 1000000);
    unsigned char uuid[16] = {0};
    assert(ktrace_file_uuid(trace_file, uuid) == 0);
    int uuid_nonzero = 0;
    for (size_t i = 0; i < sizeof(uuid); i++)
        uuid_nonzero |= uuid[i] != 0;
    assert(uuid_nonzero);
    assert(ktrace_file_earliest_timestamp(trace_file, &timestamp_value) == ENOTSUP);
    assert(ktrace_file_latest_timestamp(trace_file, &timestamp_value) == 0);
    assert(timestamp_value == 123456789);
    ktrace_file_close(trace_file);
    fd = open(path, O_RDONLY);
    assert(fd >= 0);
    assert(lseek(fd, file_header_size, SEEK_SET) == file_header_size);
    assert(read(fd, &header, sizeof(header)) == (ssize_t)sizeof(header));
    assert(header.tag == 0x8002);
    assert(header.version == 1);
    assert(header.payload_length == sizeof(stackshot));
    close(fd);
    trace_file = ktrace_file_create(NULL, path);
    assert(trace_file != NULL);
    const unsigned char partial_compressed[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0xf7, 0x65, 0x0f,
    };
    chunk = ktrace_file_append_start(trace_file, 0x1f00, 0, 0);
    assert(chunk != NULL);
    assert(ktrace_file_append_data(trace_file, chunk, partial_compressed,
                                   sizeof(partial_compressed)) == 0);
    assert(ktrace_file_append_finish(trace_file, chunk) == 0);
    ktrace_file_close(trace_file);
    trace_file = ktrace_file_open(path, 0);
    assert(trace_file != NULL);
    assert(ktrace_file_supports_continuous_time(trace_file) == 1);
    assert(ktrace_file_ktrace_event_count(trace_file) == 1);
    assert(ktrace_file_is_64_bit(trace_file) == 1);
    assert(ktrace_file_get_format(trace_file) == 4);
    assert(ktrace_file_earliest_timestamp(trace_file, &timestamp_value) == 0);
    assert(timestamp_value == 7);
    assert(ktrace_file_latest_timestamp(trace_file, &timestamp_value) == 0);
    assert(timestamp_value == 7);
    ktrace_file_close(trace_file);
    trace_file = ktrace_file_create(NULL, path);
    assert(trace_file != NULL);
    chunk = ktrace_file_append_start(trace_file, 0x8002, 1, 0);
    assert(chunk != NULL);
    assert(ktrace_file_append_data(trace_file, chunk, "partial", 7) == 0);
    ktrace_file_close(trace_file);
    fd = open(path, O_RDONLY);
    assert(fd >= 0);
    assert(lseek(fd, 0, SEEK_END) == file_header_size);
    trace_file = ktrace_file_create(NULL, path);
    assert(trace_file != NULL);
    assert(ktrace_set_ktrace_file(owned_session, trace_file, 1) == 0);
    assert(ktrace_start(owned_session) == 0);
    assert(ktrace_end(owned_session, 0) == 0);
    assert(ktrace_set_buffer_size(session, 64) == 0);
    assert(ktrace_set_continuous_timestamps(session, 1) == 0);
    assert(ktrace_set_thread_groups_enabled(session, 1) == 0);
    assert(ktrace_set_use_existing(session) == 1);
    assert(ktrace_set_no_postprocessing(session) == 0);
    assert(ktrace_session_set_default_event_names_enabled(session, 1) == 0);
    assert(ktrace_session_set_event_names_enabled(session, 1) == 0);
    assert(ktrace_session_get_default_event_names_enabled(session) == 1);
    assert(ktrace_session_get_event_names_enabled(session) == 1);
    assert(ktrace_session_getting_event_names_from_system(session) == 0);
    assert(ktrace_session_has_callbacks(session) == 0);
    file = fdopen(dup(fd), "r");
    assert(file != NULL);
    assert(ktrace_set_file(session, file) == 0);
    trace_file = ktrace_file_open_fd(fd, 0);
    assert(trace_file != NULL);
    assert(ktrace_set_ktrace_file(session, trace_file, 0) == 0);
    ktrace_file_close(trace_file);
    assert(ktrace_start_writing_fd(session, 0, fd, 0, 0) == 0);
    assert(ktrace_start(session) == 0);
    assert(ktrace_start(session) == EBUSY);
    assert(ktrace_end(session, 1) == 0);
    assert(ktrace_end(session, 0) == EINVAL);
    ktrace_session_destroy(session);
    ktrace_session_free(flagged_session);
    ktrace_session_free(owned_session);
    fclose(file);
    close(fd);
    unlink(path);
    return 0;
}
