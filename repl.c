#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h"
#include "debug.h"
#include "memory.h"

Chunk rChunk;

static void resetChunk() {
    initChunk(&rChunk);
}

static bool evalLines(char *lines[], int numLines) {
    resetStack();
    resetChunk();
    vm.hadError = false;
    ObjString *buf = copyString("", 0);
    hideFromGC((Obj*)buf);
    for (int i = 0; i < numLines; i++) {
        char *line = lines[i];
        ASSERT(line);
        pushCString(buf, line, strlen(line));
    }
    char *code = buf->chars;
    CompileErr cerr = COMPILE_ERR_NONE;
    /*fprintf(stderr, "compiling code: '%s'", code);*/
    int res = compile_src(code, &rChunk, &cerr);
    if (res != 0) {
        fprintf(stderr, "Compilation error\n");
        return false;
    }
    /*fprintf(stderr, "interpreting code\n");*/
    InterpretResult ires = interpret(&rChunk);
    if (ires != INTERPRET_OK) {
        fprintf(stderr, "Error evaluating code\n");
        return false;
    }
    return true;
}

static void freeLines(char *lines[], int numLines) {
    for (int i = 0; i < numLines; i++) {
        free(lines[i]);
    }
}

static bool scanToEnd() {
    Token tok;
    while (true) {
        tok = scanToken();
        if (tok.type == TOKEN_EOF) {
            return true;
        } else if (tok.type == TOKEN_ERROR) {
            return false;
        }
        // otherwise continue
    }
    ASSERT(0);
}

void scannerAddSrc(char *src) {
    ASSERT(src);
    ASSERT(scanner.source);
    size_t newsz = strlen(scanner.source)+1+strlen(src);
    char *buf = calloc(newsz, 1);
    ASSERT_MEM(buf);
    strcpy(buf, scanner.source);
    strcat(buf, src);
    scanner.source = buf;
}

void repl(void) {
    initScanner("");
    initChunk(&rChunk);
    initVM();
    turnGCOff();

    char *lines[50];
    int numLines = 0;
    char *line = NULL;
    size_t size;
    int getres = -1;
    fprintf(stderr, ">  ");
    while ((getres = getline(&line, &size, stdin)) != -1) {
        ASSERT(line);
        lines[numLines++] = line;
        scannerAddSrc(line);
        bool isOk = scanToEnd();
        if (!isOk) {
            fprintf(stderr, "Lexical error\n");
            freeLines(lines, numLines);
            numLines = 0;
            initScanner("");
            fprintf(stderr, ">  ");
            continue;
        }
        if (scanner.indent == 0) { // evaluate the statement/expression
            /*fprintf(stderr, "Evaluating lines\n");*/
            evalLines(lines, numLines);
            Value *val = getLastValue();
            fprintf(stderr, "  => ");
            if (val) {
                printValue(stderr, *val);
            } else {
                printValue(stderr, NIL_VAL);
            }
            fprintf(stderr, "\n");
            freeLines(lines, numLines);
            numLines = 0;
            initScanner("");
        } else {
            // wait until more input
        }
        line = NULL;
        fprintf(stderr, ">  ");
    }
    exit(0);
}
