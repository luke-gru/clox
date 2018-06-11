CC=gcc
SRCS = main.c debug.c memory.c chunk.c value.c scanner.c compiler.c vm.c options.c
TEST_SRCS = debug.c memory.c chunk.c value.c scanner.c compiler.c vm.c options.c
TEST_FILES = test/test_object.c test/vec.c
DEBUG_FLAGS=-DDEBUG_TRACE_EXECUTION -g
TEST_FLAGS=-g -Itest/include -I.
BUILD_DIR=bin
BUILD_FILE=clox
BUILD_FILE_DEBUG=clox_debug

.PHONY: clox
clox: build
	${CC} $(SRCS) -o ${BUILD_DIR}/${BUILD_FILE}

.PHONY: debug
debug: build
	${CC} $(SRCS) ${DEBUG_FLAGS} -o ${BUILD_DIR}/${BUILD_FILE_DEBUG}

.PHONY: build
build:
	mkdir -p ${BUILD_DIR}

.PHONY: clean
clean:
	rm -f ${BUILD_DIR}
	rm *.o

.PHONY: test
test: build
	${CC} $(TEST_SRCS) $(TEST_FILES) ${TEST_FLAGS} -o bin/test

