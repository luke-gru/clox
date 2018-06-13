CC=gcc
CFLAGS=-Wall -Wno-unused-label -Wno-unused-function
SRCS = main.c debug.c memory.c chunk.c value.c scanner.c compiler.c vm.c object.c options.c vec.c nodes.c parser.c
TEST_SRCS = debug.c memory.c chunk.c value.c scanner.c compiler.c vm.c object.c options.c vec.c nodes.c parser.c
TEST_FILES = test/test_object.c test/test_nodes.c test/test_compiler.c test/test_vm.c
DEBUG_FLAGS=-DDEBUG_TRACE_EXECUTION -g
TEST_FLAGS=-g -Itest/include -I.
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
	rm -f ${BUILD_DIR}
	rm *.o

.PHONY: test
test: build
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_object.c ${TEST_FLAGS} -o bin/test_object
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_nodes.c ${TEST_FLAGS} -o bin/test_nodes
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_compiler.c ${TEST_FLAGS} -o bin/test_compiler
	${CC} ${CFLAGS} $(TEST_SRCS) test/test_vm.c ${TEST_FLAGS} -o bin/test_vm

