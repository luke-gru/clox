OS_NAME:=$(shell uname -s | tr A-Z a-z)
ifeq ($(strip $(CC)),cc)
CC=
endif
ifeq ($(strip $(CC)),)
	ifeq ($(OS_NAME),darwin)
		CC=clang
	else
		CC=gcc
	endif
endif
ROOT_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
DEFINES:=-DNAN_TAGGING -DCOMPUTED_GOTO -DLX_BUILT_DIR=$(ROOT_DIR)
GCC_DEFINES:=-D_GNU_SOURCE
GCC_CFLAGS:=-std=c99 -Wall -Wextra -Wmissing-prototypes -Wno-shadow -Wvla -Wno-unused-but-set-variable -Wno-unused-parameter -Wno-unused-label -I. -Ivendor -pthread ${DEFINES} ${GCC_DEFINES}
GPP_CFLAGS:=-std=c++11 -w -fpermissive -I. -Ivendor -pthread ${DEFINES} ${GCC_DEFINES}
# NOTE: clang++ doesn't compile yet, too many C++ type errors
CLANGPP_CFLAGS:=-std=c++11 -w -fpermissive -I. -Ivendor -pthread ${DEFINES}
CLANG_CFLAGS:=-std=c99 -Wall -Wextra -Wmissing-prototypes -I. -Ivendor -Wno-unused-but-set-variable -Wno-unused-parameter -Wno-unused-label -pthread ${DEFINES}
ifneq (,$(findstring clang,$(CC)))
	ifneq (,$(findstring clang++,$(CC)))
		CFLAGS:=${CLANGPP_CFLAGS}
	else
		CFLAGS:=${CLANG_CFLAGS}
	endif
else
  ifneq (,$(findstring g++,$(CC)))
		CFLAGS:=${GPP_CFLAGS}
  else
		CFLAGS:=${GCC_CFLAGS}
  endif
endif
SRCS=main.c debug.c memory.c chunk.c value.c scanner.c compiler.c vm.c object.c string.c array.c map.c options.c vendor/vec.c nodes.c parser.c table.c runtime.c process.c signal.c io.c file.c dir.c thread.c block.c rand.c time.c repl.c debugger.c regex_lib.c regex.c socket.c errors.c binding.c vendor/linenoise.c
TEST_SRCS=debug.c   memory.c chunk.c value.c scanner.c compiler.c vm.c object.c string.c array.c map.c options.c vendor/vec.c nodes.c parser.c table.c runtime.c process.c signal.c io.c file.c dir.c thread.c block.c rand.c time.c debugger.c regex_lib.c regex.c socket.c errors.c binding.c
TEST_FILES=test/test_object.c test/test_nodes.c test/test_compiler.c test/test_vm.c test/test_gc.c test/test_examples.c test/test_regex.c
DEBUG_FLAGS=-O2 -g -rdynamic
GPROF_FLAGS=-O3 -pg -DNDEBUG
TEST_FLAGS=-O0 -g -rdynamic -Itest/include -I. -DLOX_TEST
RELEASE_FLAGS=-O3 -DNDEBUG -Wno-unused-function -Wno-unused-variable
BUILD_DIR=bin
BUILD_DEBUG_DIR:=${BUILD_DIR}/debug
BUILD_DEBUG_FILE=clox
BUILD_RELEASE_DIR:=${BUILD_DIR}/release
BUILD_RELEASE_FILE=clox
BUILD_TEST_DIR=${BUILD_DIR}/test
SUFFIX_FLAGS=-ldl
ifeq ($(PREFIX),)
PREFIX=/usr/local/bin
endif

# default
.PHONY: debug
debug: create_debug_dir
	${CC} ${CFLAGS} $(SRCS) ${DEBUG_FLAGS} -o ${BUILD_DEBUG_DIR}/${BUILD_DEBUG_FILE} ${SUFFIX_FLAGS}

.PHONY: os
os:
	@echo $(OS_NAME)

.PHONY: profile
gprof: build
	${CC} ${CFLAGS} $(SRCS) ${GPROF_FLAGS} -o ${BUILD_DIR}/gprof ${SUFFIX_FLAGS}

.PHONY: release
release: create_release_dir
	${CC} ${CFLAGS} $(SRCS) ${RELEASE_FLAGS} -o ${BUILD_RELEASE_DIR}/${BUILD_RELEASE_FILE} ${SUFFIX_FLAGS}

.PHONY: create_prefix_dir
create_prefix_dir:
	mkdir -p ${PREFIX}

# Run with sudo
install: release create_prefix_dir
	cp ${BUILD_RELEASE_DIR}/${BUILD_RELEASE_FILE} ${PREFIX}/clox

.PHONY: create_debug_dir
create_debug_dir:
	mkdir -p ${BUILD_DEBUG_DIR}

.PHONY: create_release_dir
create_release_dir:
	mkdir -p ${BUILD_RELEASE_DIR}

.PHONY: create_test_dir
create_test_dir:
	mkdir -p ${BUILD_TEST_DIR}

.PHONY: clean
clean:
	rm -rf ${BUILD_DIR}
	rm -f *.o

.PHONY: build_test_object
build_test_object: create_test_dir
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_object.c ${TEST_FLAGS} -o ${BUILD_TEST_DIR}/test_object ${SUFFIX_FLAGS}

.PHONY: run_test_object
run_test_object:
	./${BUILD_TEST_DIR}/test_object

.PHONY: build_test_nodes
build_test_nodes: create_test_dir
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_nodes.c ${TEST_FLAGS} -o ${BUILD_TEST_DIR}/test_nodes ${SUFFIX_FLAGS}

.PHONY: run_test_nodes
run_test_nodes:
	./${BUILD_TEST_DIR}/test_nodes

# NOTE: the compiler tests are deprecated, and not maintained
.PHONY: build_test_compiler
build_test_compiler: create_test_dir
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_compiler.c ${TEST_FLAGS} -o ${BUILD_TEST_DIR}/test_compiler ${SUFFIX_FLAGS}

.PHONY: run_test_compiler
run_test_compiler:
	./${BUILD_TEST_DIR}/test_compiler

.PHONY: build_test_vm
build_test_vm: create_test_dir
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_vm.c ${TEST_FLAGS} -o ${BUILD_TEST_DIR}/test_vm ${SUFFIX_FLAGS}

.PHONY: run_test_vm
run_test_vm:
	./${BUILD_TEST_DIR}/test_vm

.PHONY: build_test_gc
build_test_gc: create_test_dir
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_gc.c ${TEST_FLAGS} -o ${BUILD_TEST_DIR}/test_gc ${SUFFIX_FLAGS}

.PHONY: run_test_gc
run_test_gc:
	./${BUILD_TEST_DIR}/test_gc

.PHONY: build_test_examples
build_test_examples: create_test_dir
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_examples.c ${TEST_FLAGS} -o ${BUILD_TEST_DIR}/test_examples ${SUFFIX_FLAGS}

.PHONY: run_test_examples
run_test_examples:
	./${BUILD_TEST_DIR}/test_examples

.PHONY: build_test_regex
build_test_regex: create_test_dir
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_regex.c ${TEST_FLAGS} -o ${BUILD_TEST_DIR}/test_regex ${SUFFIX_FLAGS}

.PHONY: run_test_regex
run_test_regex:
	./${BUILD_TEST_DIR}/test_regex

# NOTE: test_nodes and test_compiler aren't in the default tests right now
.PHONY: build_tests
build_tests: build_test_regex build_test_object build_test_vm build_test_gc build_test_examples

.PHONY: run_tests
run_tests:
	./scripts/run_all_tests; sh -c "exit $$?";\
	EXIT_CODE=$$?;\
	echo "Command exited with code $$EXIT_CODE";\
	exit $$EXIT_CODE

.PHONY: test
test: build_tests run_tests

