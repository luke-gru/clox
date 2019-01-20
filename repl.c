#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h"
#include "debug.h"
#include "memory.h"
#include "linenoise.h"

static Chunk *chunk = NULL;

static void _freeChunk(void) {
    if (chunk) {
        freeChunk(chunk);
        chunk = NULL;
    }
}

static bool evalLines(char *lines[], int numLines) {
    resetStack();
    _freeChunk();
    vm.exited = false;
    ObjString *buf = hiddenString("", 0, NEWOBJ_FLAG_NONE);
    for (int i = 0; i < numLines; i++) {
        char *line = lines[i];
        ASSERT(line);
        pushCString(buf, line, strlen(line));
    }
    char *code = buf->chars;
    CompileErr cerr = COMPILE_ERR_NONE;
    /*fprintf(stderr, "compiling code: '%s'", code);*/
    chunk = compile_src(code, &cerr);
    unhideFromGC((Obj*)buf);
    if (cerr != COMPILE_ERR_NONE) {
        fprintf(stderr, "%s", "Compilation error\n");
        return false;
    }
    /*fprintf(stderr, "interpreting code\n");*/
    InterpretResult ires = interpret(chunk, "(repl)");
    resetStack();
    if (ires != INTERPRET_OK) {
        fprintf(stderr, "%s", "Error evaluating code\n");
        _freeChunk();
        return false;
    }
    return true;
}

static void freeLines(char *lines[], int numLines) {
    for (int i = 0; i < numLines; i++) {
        xfree(lines[i]);
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
    char *buf = calloc(newsz, 1);
    ASSERT_MEM(buf);
    strcpy(buf, scanner.source);
    strcat(buf, src);
    xfree(scanner.source);
    scanner.source = buf;
}

static void _resetScanner(void) {
    if (scanner.source) xfree(scanner.source);
    initScanner(&scanner, strdup(""));
}

NORETURN void repl(void) {
    const char *prompt = ">  ";
    _resetScanner();
    initVM();
    linenoiseHistorySetMaxLen(500);
    // we want to evaluate unused expressions, like the statement `1+1`,
    // because we print the last VM value after the user types in an
    // expression or statement.
    compilerOpts.noRemoveUnusedExpressions = true;

    char *lines[50];
    int numLines = 0;
    char *line = NULL;

    while ((line = linenoise(prompt)) != NULL) { // NOTE: chomps newline
        linenoiseHistoryAdd(line);
        if (numLines == 0 && strcmp(line, "exit") == 0) {
            xfree(line);
            line = NULL;
            break;
        }
        if (numLines == 0 && strcmp(line, "pstack") == 0) {
            printVMStack(stderr, THREAD());
            xfree(line);
            line = NULL;
            continue;
        }
        // resets the VM, re-inits the code chunk
        if (numLines == 0 && strcmp(line, "reset") == 0) {
            fprintf(stderr, "Resetting VM... ");
            _freeChunk();
            freeVM();
            initVM();
            _resetScanner();
            xfree(line);
            line = NULL;
            fprintf(stderr, "done.\n");
            continue;
        }
        ASSERT(line);
        if (numLines == 50) {
            fprintf(stderr, "Too many lines");
            ASSERT(0); // FIXME: don't just error out
        }
        lines[numLines++] = line;
        scannerAddSrc(line);
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
            xfree(scanner.source);
            if (!evalLines(lines, numLines)) {
                freeLines(lines, numLines);
                numLines = 0;
                _resetScanner();
                line = NULL;
                continue;
            }
            Value *val = getLastValue();
            fprintf(stderr, "%s", "  => ");
            if (val) {
                // Add first callframe in case there are none, because
                // printValue may call native methods (toString()), which
                // rely on the framecount to be at least 1.
                if (THREAD()->ec->frameCount == 0) {
                    THREAD()->ec->frameCount++;
                }
                printValue(stderr, *val, true, -1);
            } else {
                printValue(stderr, NIL_VAL, false, -1);
            }
            fprintf(stderr, "%s", "\n");
            freeLines(lines, numLines);
            numLines = 0;
            _resetScanner();
        } else {
            /*fprintf(stderr, "(waiting for more input)\n");*/
            // wait until more input
        }
        line = NULL;
    }
    stopVM(0);
}
