CC=gcc
CFLAGS=-Wall -Wno-unused-label -Wno-unused-function -Wno-discarded-qualifiers -I. -Ivendor
SRCS = main.c debug.c memory.c chunk.c value.c scanner.c compiler.c vm.c object.c options.c vec.c nodes.c parser.c table.c runtime.c repl.c debugger.c vendor/linenoise.c
TEST_SRCS = debug.c   memory.c chunk.c value.c scanner.c compiler.c vm.c object.c options.c vec.c nodes.c parser.c table.c runtime.c debugger.c
TEST_FILES = test/test_object.c test/test_nodes.c test/test_compiler.c test/test_vm.c test/test_gc.c test/test_examples.c test/test_regex.c
DEBUG_FLAGS=-O0 -DDEBUG_TRACE_EXECUTION -g
TEST_FLAGS=-O0 -Itest/include -I.
BUILD_DIR=bin
BUILD_FILE=clox
BUILD_FILE_DEBUG=clox_debug

.PHONY: clox
clox: build
	${CC} ${CFLAGS} $(SRCS) -o ${BUILD_DIR}/${BUILD_FILE}

.PHONY: debug
debug: build
	${CC} ${CFLAGS} $(SRCS) ${DEBUG_FLAGS} -o ${BUILD_DIR}/${BUILD_FILE_DEBUG}

.PHONY: build
build:
	mkdir -p ${BUILD_DIR}

.PHONY: clean
clean:
	rm -rf ${BUILD_DIR}
	rm -f *.o

.PHONY: test
test: build
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_object.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_object
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_nodes.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_nodes
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_compiler.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_compiler
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_vm.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_vm
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_gc.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_gc
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_examples.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_examples
	#${CC} ${CFLAGS} $(TEST_SRCS) test/test_regex.c ${TEST_FLAGS} -o ${BUILD_DIR}/test_regex
	@ ./bin/test_object && ./bin/test_nodes && ./bin/test_compiler && ./bin/test_vm && \
		./bin/test_gc && ./bin/test_examples
	#@ ./bin/test_regex

