ifeq ($(strip $(CC)),)
CC=gcc
endif
ROOT_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
DEFINES=-D_GNU_SOURCE -DNAN_TAGGING -DCOMPUTED_GOTO -DLX_BUILT_DIR=$(ROOT_DIR)
GCC_CFLAGS=-std=c99 -Wall -Wextra -Wmissing-prototypes -Wno-shadow -Wvla -Wno-clobbered -Wno-unused-parameter -Wno-unused-label -I. -Ivendor -pthread ${DEFINES}
GPP_CFLAGS=-std=c++11 -w -fpermissive -I. -Ivendor -pthread ${DEFINES}
# NOTE: clang++ doesn't compile yet, too many C++ type errors
CLANGPP_CFLAGS=-std=c++11 -w -fpermissive -I. -Ivendor -pthread ${DEFINES}
CLANG_CFLAGS=-std=c99 -Wall -Wextra -Wmissing-prototypes -I. -Ivendor -Wno-unused-parameter -Wno-unused-label -pthread ${DEFINES}
ifneq (,$(findstring clang,$(CC)))
	ifneq (,$(findstring clang++,$(CC)))
		CFLAGS=${CLANGPP_CFLAGS}
	else
		CFLAGS=${CLANG_CFLAGS}
	endif
else
  ifneq (,$(findstring g++,$(CC)))
    CFLAGS=${GPP_CFLAGS}
  else
    CFLAGS=${GCC_CFLAGS}
  endif
endif
SRCS = main.c debug.c memory.c chunk.c value.c scanner.c compiler.c vm.c object.c string.c array.c map.c options.c vendor/vec.c nodes.c parser.c table.c runtime.c process.c signal.c io.c file.c dir.c thread.c block.c rand.c time.c repl.c debugger.c regex_lib.c regex.c socket.c errors.c binding.c vendor/linenoise.c
TEST_SRCS = debug.c   memory.c chunk.c value.c scanner.c compiler.c vm.c object.c string.c array.c map.c options.c vendor/vec.c nodes.c parser.c table.c runtime.c process.c signal.c io.c file.c dir.c thread.c block.c rand.c time.c debugger.c regex_lib.c regex.c socket.c errors.c binding.c
TEST_FILES = test/test_object.c test/test_nodes.c test/test_compiler.c test/test_vm.c test/test_gc.c test/test_examples.c test/test_regex.c
DEBUG_FLAGS=-O2 -g -rdynamic
GPROF_FLAGS=-O3 -pg -DNDEBUG
TEST_FLAGS=-O2 -g -rdynamic -Itest/include -I. -DLOX_TEST
RELEASE_FLAGS=-O3 -DNDEBUG -Wno-unused-function
BUILD_DIR=bin
BUILD_FILE_RELEASE=clox
BUILD_FILE_DEBUG=clox
SUFFIX = -ldl

# default
.PHONY: debug
debug: build
	${CC} ${CFLAGS} $(SRCS) ${DEBUG_FLAGS} -o ${BUILD_DIR}/${BUILD_FILE_DEBUG} ${SUFFIX}

# default
.PHONY: profile
gprof: build
	${CC} ${CFLAGS} $(SRCS) ${GPROF_FLAGS} -o ${BUILD_DIR}/gprof ${SUFFIX}

.PHONY: release
release: build
	${CC} ${CFLAGS} $(SRCS) ${RELEASE_FLAGS} -o ${BUILD_DIR}/${BUILD_FILE_RELEASE} ${SUFFIX}

.PHONY: build
build:
	mkdir -p ${BUILD_DIR}

.PHONY: clean
clean:
	rm -rf ${BUILD_DIR}
	rm -f *.o

.PHONY: build_test_object
build_test_object: build
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_object.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_object ${SUFFIX}

.PHONY: run_test_object
run_test_object:
	@ ./bin/test_object

.PHONY: build_test_nodes
build_test_nodes: build
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_nodes.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_nodes ${SUFFIX}

.PHONY: run_test_nodes
run_test_nodes:
	@ ./bin/test_nodes

# NOTE: the compiler tests are deprecated, and not maintained
.PHONY: build_test_compiler
build_test_compiler: build
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_compiler.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_compiler ${SUFFIX}

.PHONY: run_test_compiler
run_test_compiler:
	@ ./bin/test_compiler

.PHONY: build_test_vm
build_test_vm: build
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_vm.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_vm ${SUFFIX}

.PHONY: run_test_vm
run_test_vm:
	@ ./bin/test_vm

.PHONY: build_test_gc
build_test_gc: build
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_gc.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_gc ${SUFFIX}

.PHONY: run_test_gc
run_test_gc:
	@ ./bin/test_gc

.PHONY: build_test_examples
build_test_examples: build
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_examples.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_examples ${SUFFIX}

.PHONY: run_test_examples
run_test_examples:
	@ ./bin/test_examples

.PHONY: build_test_regex
build_test_regex: build
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_regex.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_regex ${SUFFIX}

.PHONY: run_test_regex
run_test_regex:
	@ ./bin/test_regex

# NOTE: test_nodes and test_compiler aren't in the default tests right now
.PHONY: build_tests
build_tests: build build_test_regex build_test_object build_test_vm build_test_gc build_test_examples

.PHONY: run_tests
run_tests: run_test_regex run_test_object run_test_vm run_test_gc run_test_examples

.PHONY: test
test: build_tests run_tests

