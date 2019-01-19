#include <stdio.h>
#include "jit_compiler.hpp"
#include "scanner.h"
#include "parser.h"
#include "vm.h"
#include "object.h"
#include "debug.h"
#include "memory.h"
#include "linenoise.h"

static void freeLines(const char *lines[], int numLines) {
    for (int i = 0; i < numLines; i++) {
        xfree((void*)lines[i]);
    }
}

static bool scanToEnd(void) {
    Token tok;
    resetScanner(&scanner);
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

// Adds copied chars in `src` to `scanner.source`
void scannerAddSrc(char *src) {
    ASSERT(src);
    ASSERT(scanner.source);
    size_t newsz = strlen(scanner.source)+1+strlen(src);
    char *buf = (char*)calloc(newsz, 1);
    ASSERT_MEM(buf);
    strcpy(buf, scanner.source);
    strcat(buf, src);
    xfree((void*)scanner.source);
    scanner.source = buf;
}

static void _resetScanner(void) {
    if (scanner.source) xfree((void*)scanner.source);
    initScanner(&scanner, strdup(""));
}

static bool dumpLines(const char *lines[], int numLines) {
    ObjString *buf = hiddenString("", 0, NEWOBJ_FLAG_NONE);
    for (int i = 0; i < numLines; i++) {
        const char *line = lines[i];
        ASSERT(line);
        pushCString(buf, (char*)line, strlen(line));
    }
    char *code = buf->chars;
    initScanner(&scanner, code);
    Parser p;
    initParser(&p);
    Node *program = parse(&p);
    freeScanner(&scanner);
    if (p.hadError) {
        outputParserErrors(&p, stderr);
        freeParser(&p);
        return false;
    }
    ASSERT(program);
    fprintf(stderr, "Dumping lines\n");
    llvm::Value *val = jitNode(program);
    ASSERT(val);
    jitEmitValueIR(val);
    fprintf(stderr, "\n");
    return true;
}

extern "C" NORETURN void jit_repl(void) {
    fprintf(stderr, "JIT REPL\n");
    const char *prompt = ">  ";
    _resetScanner();
    initVM();
    initJit();
    linenoiseHistorySetMaxLen(500);
    const char *lines[50];
    int numLines = 0;
    const char *line = NULL;
    while ((line = linenoise(prompt)) != NULL) { // NOTE: chomps newline
        lines[numLines++] = line;
        scannerAddSrc((char*)line);
        bool isOk = scanToEnd();
        if (!isOk) {
            fprintf(stderr, "%s", "Lexical error\n");
            freeLines(lines, numLines);
            numLines = 0;
            _resetScanner();
            line = NULL;
            continue;
        }
        if (scanner.indent == 0) { // evaluate the statement/expression
            dumpLines(lines, numLines);
            freeLines(lines, numLines);
            numLines = 0;
            _resetScanner();
        }
        line = NULL;
    }
}
