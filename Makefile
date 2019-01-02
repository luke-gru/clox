CC=gcc
DEFINES=-D_GNU_SOURCE -DNAN_TAGGING
CFLAGS=-Wall -Wno-unused-label -Wno-unused-function -Wno-discarded-qualifiers -I. -Ivendor -pthread ${DEFINES}
SRCS = main.c debug.c memory.c chunk.c value.c scanner.c compiler.c vm.c object.c string.c array.c map.c options.c vendor/vec.c nodes.c parser.c table.c runtime.c process.c io.c file.c thread.c rand.c repl.c debugger.c vendor/linenoise.c
TEST_SRCS = debug.c   memory.c chunk.c value.c scanner.c compiler.c vm.c object.c string.c array.c map.c options.c vendor/vec.c nodes.c parser.c table.c runtime.c process.c io.c file.c thread.c rand.c debugger.c
TEST_FILES = test/test_object.c test/test_nodes.c test/test_compiler.c test/test_vm.c test/test_gc.c test/test_examples.c test/test_regex.c
DEBUG_FLAGS=-O0 -g -rdynamic
TEST_FLAGS=-O0 -g -rdynamic -Itest/include -I.
RELEASE_FLAGS=-O3 -DNDEBUG
BUILD_DIR=bin
BUILD_FILE_RELEASE=clox
BUILD_FILE_DEBUG=clox

# default
.PHONY: debug
debug: build
	${CC} ${CFLAGS} $(SRCS) ${DEBUG_FLAGS} -o ${BUILD_DIR}/${BUILD_FILE_DEBUG}

.PHONY: release
release: build
	${CC} ${CFLAGS} $(SRCS) ${RELEASE_FLAGS} -o ${BUILD_DIR}/${BUILD_FILE_RELEASE}

.PHONY: build
build:
	mkdir -p ${BUILD_DIR}

.PHONY: clean
clean:
	rm -rf ${BUILD_DIR}
	rm -f *.o

.PHONY: build_test_object
build_test_object:
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_object.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_object

.PHONY: run_test_object
run_test_object:
	@ ./bin/test_object

.PHONY: build_test_nodes
build_test_nodes:
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_nodes.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_nodes

.PHONY: run_test_nodes
run_test_nodes:
	@ ./bin/test_nodes

.PHONY: build_test_compiler
build_test_compiler:
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_compiler.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_compiler

.PHONY: run_test_compiler
run_test_compiler:
	@ ./bin/test_compiler

.PHONY: build_test_vm
build_test_vm:
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_vm.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_vm

.PHONY: run_test_vm
run_test_vm:
	@ ./bin/test_vm

.PHONY: build_test_gc
build_test_gc:
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_gc.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_gc

.PHONY: run_test_gc
run_test_gc:
	@ ./bin/test_gc

.PHONY: build_test_examples
build_test_examples:
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_examples.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_examples

.PHONY: run_test_examples
run_test_examples:
	@ ./bin/test_examples

.PHONY: build_test_regex
build_test_regex:
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_regex.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_regex

.PHONY: run_test_regex
run_test_regex:
	@ ./bin/test_regex

.PHONY: build_tests
build_tests: build build_test_object build_test_nodes build_test_compiler build_test_vm build_test_gc build_test_examples

.PHONY: run_tests
run_tests: run_test_object run_test_nodes run_test_compiler run_test_vm run_test_gc run_test_examples

.PHONY: test
test: build build_test_object build_test_nodes build_test_compiler build_test_vm build_test_gc build_test_examples run_tests

