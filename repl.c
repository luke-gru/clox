#include <stdio.h>
#include <string.h>
#include "vm.h"
#include "parser.h"
#include "compiler.h"
#include "scanner.h"
#include "object.h"
#include "debug.h"
#include "memory.h"
#include "linenoise.h"

static ObjFunction *Func = NULL;
static char *lines[50];
static int numLines = 0;
static const char *prompt = ">  ";

static void _freeFunc(void) {
    if (Func) {
        Func = NULL;
    }
}

static void scannerAddLine(Scanner *scan, char *src) {
    ASSERT(src);
    ASSERT(scan->source);
    size_t newsz = strlen(scan->source)+1+strlen(src)+1;
    char *buf = calloc(1, newsz);
    ASSERT_MEM(buf);
    strcpy(buf, scan->source);
    strcat(buf, src);
    strcat(buf, "\n");
    xfree(scan->source);
    scan->source = buf;
}

static void getMoreSourceFn(Scanner *scan, Parser *p) {
    char *line = NULL;

    line = linenoise(prompt);
    if (!line && p) { // CTRL-D
        p->aborted = true;
        return;
    }
    linenoiseHistoryAdd(line);
    /*fprintf(stderr, "add src called\n");*/
    scannerAddLine(scan, line);
    /*fprintf(stderr, "/add src called\n");*/
    lines[numLines++] = line;
}

static bool evalNode(Node *program) {
    resetStack();
    _freeFunc();
    vm.exited = false;
    THREAD()->hadError = false;
    CompileErr cerr = COMPILE_ERR_NONE;
    /*fprintf(stderr, "compiling code: '%s'", code);*/
    Func = compile_node(program, &cerr);
    if (cerr != COMPILE_ERR_NONE) {
        fprintf(stderr, "%s", "Compilation error\n");
        return false;
    }
    /*fprintf(stderr, "interpreting code\n");*/
    InterpretResult ires = interpret(Func, "(repl)");
    unhideFromGC(TO_OBJ(Func));
    resetStack();
    if (ires != INTERPRET_OK) {
        fprintf(stderr, "%s", "Error evaluating code\n");
        _freeFunc();
        return false;
    }
    return true;
}

static void freeLines(char *lines[], int numLines) {
    for (int i = 0; i < numLines; i++) {
        xfree(lines[i]);
    }
}

static void _resetScanner(void) {
    Scanner *scan = getScanner();
    if (scan->source) xfree(scan->source);
    initScanner(scan, strdup(""));
    scannerSetMoreSourceFn(scan, getMoreSourceFn);
}

NORETURN void repl(void) {
    setScanner(&scanner);
    _resetScanner();
    initVM();
    linenoiseHistorySetMaxLen(500);
    // we want to evaluate unused expressions, like the statement `1+1`,
    // because we print the last VM value after the user types in an
    // expression or statement.
    compilerOpts.noRemoveUnusedExpressions = true;

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
            _freeFunc();
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
        scannerAddLine(getScanner(), line);
        Parser p;
        initParser(&p);
        Node *n = parseMaybePartialStatement(&p, getMoreSourceFn);
        if (p.hadError) {
            fprintf(stderr, "%s", "Parser error\n");
            outputParserErrors(&p, stderr);
            freeParser(&p);
            freeLines(lines, numLines);
            numLines = 0;
            _resetScanner();
            line = NULL;
            continue;
        }
        freeParser(&p);
        ASSERT(n);
        bool compileOk = evalNode(n);
        if (!compileOk) {
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
            printInspectValue(stderr, *val);
        } else {
            printValue(stderr, NIL_VAL, false, -1);
        }

        fprintf(stderr, "%s", "\n");
        freeLines(lines, numLines);
        numLines = 0;
        _resetScanner();
        line = NULL;
    }
    stopVM(0);
}
