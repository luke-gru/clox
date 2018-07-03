#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h"
#include "debug.h"
#include "memory.h"

Chunk rChunk;

static void resetChunk(void) {
    initChunk(&rChunk); // NOTE: should call freeChunk to release the value array
}

static bool evalLines(char *lines[], int numLines) {
    resetStack();
    resetChunk();
    vm.hadError = false;
    ObjString *buf = hiddenString("", 0);
    for (int i = 0; i < numLines; i++) {
        char *line = lines[i];
        ASSERT(line);
        pushCString(buf, line, strlen(line));
    }
    char *code = buf->chars;
    CompileErr cerr = COMPILE_ERR_NONE;
    /*fprintf(stderr, "compiling code: '%s'", code);*/
    int res = compile_src(code, &rChunk, &cerr);
    unhideFromGC((Obj*)buf);
    freeObject((Obj*)buf, true);
    if (res != 0) {
        fprintf(stderr, "%s", "Compilation error\n");
        return false;
    }
    /*fprintf(stderr, "interpreting code\n");*/
    InterpretResult ires = interpret(&rChunk, "(repl)");
    if (ires != INTERPRET_OK) {
        fprintf(stderr, "%s", "Error evaluating code\n");
        return false;
    }
    return true;
}

static void freeLines(char *lines[], int numLines) {
    for (int i = 0; i < numLines; i++) {
        free(lines[i]);
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

void scannerAddSrc(char *src) {
    ASSERT(src);
    ASSERT(scanner.source);
    // TODO: use realloc to avoid leaking memory
    size_t newsz = strlen(scanner.source)+1+strlen(src);
    char *buf = calloc(newsz, 1);
    ASSERT_MEM(buf);
    strcpy(buf, scanner.source);
    strcat(buf, src);
    scanner.source = buf;
}

static void _resetScanner(void) {
    initScanner(&scanner, strdup(""));
}

NORETURN void repl(void) {
    const char *prompt = ">  ";
    _resetScanner();
    initChunk(&rChunk);
    initVM();
    // we want to evaluate unused expressions, like the statement `1+1`,
    // because we print the last VM value after the user types in an
    // expression or statement.
    compilerOpts.noRemoveUnusedExpressions = true;

    char *lines[50];
    int numLines = 0;
    char *line = NULL;
    size_t size;
    int getres = -1;
    fprintf(stderr, "%s", prompt);
    // NOTE: using getline() instead of fgets() so that we can save previous
    // lines if the user types in a statement that takes multiple lines, like
    // a class declaration or a for loop.
    while ((getres = getline(&line, &size, stdin)) != -1) {
        if (numLines == 0 && strcmp(line, "exit\n") == 0) {
            free(line);
            line = NULL;
            break;
        }
        if (numLines == 0 && strcmp(line, "pstack\n") == 0) {
            printVMStack(stderr);
            fprintf(stderr, "%s", prompt);
            free(line);
            line = NULL;
            continue;
        }
        // resets the VM, re-inits the code chunk
        if (numLines == 0 && strcmp(line, "reset\n") == 0) {
            fprintf(stderr, "Resetting VM... ");
            freeChunk(&rChunk); // re-initializes it too
            freeVM();
            initVM();
            _resetScanner();
            free(line);
            line = NULL;
            fprintf(stderr, "done.\n");
            fprintf(stderr, "%s", prompt);
            continue;
        }
        ASSERT(line);
        lines[numLines++] = line;
        scannerAddSrc(line);
        bool isOk = scanToEnd();
        if (!isOk) {
            fprintf(stderr, "%s", "Lexical error\n");
            freeLines(lines, numLines);
            numLines = 0;
            _resetScanner();
            fprintf(stderr, "%s", prompt);
            free(line);
            line = NULL;
            continue;
        }
        if (scanner.indent == 0) { // evaluate the statement/expression
            if (!evalLines(lines, numLines)) {
                freeLines(lines, numLines);
                numLines = 0;
                _resetScanner();
                fprintf(stderr, "%s", prompt);
                free(line);
                line = NULL;
                continue;
            }
            Value *val = getLastValue();
            fprintf(stderr, "%s", "  => ");
            if (val) {
                // Add first callframe in case there are none, because
                // printValue may call native methods (toString()), which
                // rely on the framecount to be at least 1.
                if (vm.ec->frameCount == 0) {
                    vm.ec->frameCount++;
                }
                printValue(stderr, *val, true);
            } else {
                printValue(stderr, NIL_VAL, false);
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
        fprintf(stderr, "%s", prompt);
    }
    exit(0);
}
