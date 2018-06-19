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
    char **argvp = argv+1;
    int i = 0;
    int incrOpt = 0;
    bool interactive = false;
    while (argvp[i] != NULL) {
        if ((incrOpt = parseOption(argvp, i) > 0)) {
            i+=incrOpt;
            continue;
        }
        if (strcmp(argvp[i], "-f") == 0 && argvp[i+1]) {
            fname = argvp[i+1];
            i+=2;
        } else if (strcmp(argvp[i], "-i") == 0) {
            interactive = true;
            i+=1;
        } else {
            fprintf(stderr, "Invalid option: %s\n", argvp[i]);
            usage(1);
        }
    }

    CompileErr err = COMPILE_ERR_NONE;
    int compile_res = 0;
    if (fname == NULL || interactive) {
        repl();
        exit(0);
    }

    Chunk chunk;
    initChunk(&chunk);
    initVM();
    compile_res = compile_file(fname, &chunk, &err);

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
