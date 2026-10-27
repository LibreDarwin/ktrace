# Copyright (C) 2026, LibreDarwin
# SPDX-License-Identifier: BSD-3-Clause

CC ?= clang
SDK ?= /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
CFLAGS ?= -O2 -std=c11 -D_DARWIN_C_SOURCE -Wall -Wextra
CFLAGS += -isysroot "$(SDK)" -I Sources
BUILD_DIR ?= build

LIB := $(BUILD_DIR)/libktrace.dylib
STATIC_LIB := $(BUILD_DIR)/libktrace.a
OBJECT := $(BUILD_DIR)/ktrace.o
TEST := $(BUILD_DIR)/ktrace_test

all: $(LIB) $(STATIC_LIB)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(OBJECT): Sources/ktrace.c Sources/ktrace.h $(BUILD_DIR)
	$(CC) $(CFLAGS) -fPIC -c -o $@ Sources/ktrace.c

$(LIB): $(OBJECT)
	$(CC) $(CFLAGS) -dynamiclib -install_name /usr/lib/libktrace.dylib -o $@ $^

$(STATIC_LIB): $(OBJECT)
	ar rcs $@ $^

$(TEST): Tests/ktrace_test.c $(OBJECT) Sources/ktrace.h $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ Tests/ktrace_test.c $(OBJECT)

test: $(TEST)
	$(TEST)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all test clean
