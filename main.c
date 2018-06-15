#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"
#include "options.h"
#include <stdio.h>

static void usage(int exitstatus) {
    fprintf(stdout, "Usage:\n"
        "clox -f FILE [-DDEBUG_PARSER] [-DTRACE_PARSER_CALLS] [-DTRACE_VM_EXECUTION]\n"
    );
    exit(exitstatus);
}

int main(int argc, char *argv[]) {

    initOptions();
    char *fname = NULL;
    char *bytecodeFname = NULL;
    char **argvp = argv+1;
    int i = 0;
    int incrOpt = 0;
    while (argvp[i] != NULL) {
        if ((incrOpt = parseOption(argvp, i) > 0)) {
            i+=incrOpt;
            continue;
        }
        if (strcmp(argvp[i], "-f") == 0 && argvp[i+1]) {
            fname = argvp[i+1];
            i+=2;
        } else if (strcmp(argvp[i], "--bytecode-f") == 0 && argvp[i+1]) {
            bytecodeFname = argvp[i+1];
            i+=2;
        } else {
            fprintf(stderr, "Invalid option: %s\n", argvp[i]);
            usage(1);
        }
    }

    CompileErr err = COMPILE_ERR_NONE;
    int compile_res = 0;
    // TODO: take src from stdin a line at a time
    if (fname == NULL && bytecodeFname == NULL) {
        usage(1);
    }

    Chunk chunk;
    initChunk(&chunk);
    initVM();
    if (fname != NULL) {
        compile_res = compile_file(fname, &chunk, &err);
    } else if (bytecodeFname != NULL) {
        FILE *f = fopen(bytecodeFname, "rb");
        if (f == NULL) {
            usage(1);
        }
        int loadErr = 0;
        int loadRes = loadChunk(&chunk, f, &loadErr);
        if (loadRes != 0) {
            fprintf(stderr, "Error loading bytecode!\n");
            exit(1);
        }
    }

    if (compile_res != 0) {
        if (err == COMPILE_ERR_SYNTAX) {
            freeVM();
            freeChunk(&chunk);
            die("%s", "Syntax error\n");
        } else {
            freeVM();
            freeChunk(&chunk);
            die("%s", "Compile error\n");
        }
    }

    if (CLOX_OPTION_T(debugBytecode) && bytecodeFname != NULL) {
        printDisassembledChunk(&chunk, "Bytecode:");
    }

    if (CLOX_OPTION_T(compileOnly)) {
        freeVM();
        freeChunk(&chunk);
        printf("No compilation errors\n");
        exit(0);
    }

    InterpretResult ires = interpret(&chunk);
    if (ires != INTERPRET_OK) {
        freeVM();
        freeChunk(&chunk);
        die("%s", "Interpreter runtime error\n");
    }

    freeVM();
    freeChunk(&chunk);
    return 0;
}
