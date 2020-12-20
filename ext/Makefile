ifeq ($(strip $(CC)),)
CC=gcc
endif

CFLAGS=-I/home/luke/workspace/clox

SRCS = testext.c

testext.so: testext.o
	${CC} -shared -o testext.so testext.o

testext.o:
	${CC} -c ${CFLAGS} -fpic ${SRCS}

