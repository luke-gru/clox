#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"
#include "options.h"
#include <stdio.h>

static void usage(int exitstatus) {
    fprintf(stdout, "Usage:\n"
        "clox [-f FILE] [-DDEBUG_PARSER] [-DTRACE_PARSER_CALLS]\n"
    );
    exit(exitstatus);
}

int main(int argc, char *argv[]) {
    Chunk chunk;
    initChunk(&chunk);

    initOptions();
    char *fname = NULL;
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
        } else {
            fprintf(stderr, "Invalid option: %s\n", argvp[i]);
            usage(1);
        }
    }

    if (fname != NULL) {
        CompileErr err = COMPILE_ERR_NONE;
        return compile_file(fname, &chunk, &err);
    }

    initVM();
    // -((1.2 + 3.4) / 5.6)
    int constant = addConstant(&chunk, NUMBER_VAL(1.2));
    writeChunk(&chunk, OP_CONSTANT, 1);
    writeChunk(&chunk, constant, 1); // 1.2
    int constant2 = addConstant(&chunk, NUMBER_VAL(3.4));
    writeChunk(&chunk, OP_CONSTANT, 1);
    writeChunk(&chunk, constant2, 1); // 3.4
    writeChunk(&chunk, OP_ADD, 1); // +
    int constant3 = addConstant(&chunk, NUMBER_VAL(5.6));
    writeChunk(&chunk, OP_CONSTANT, 1);
    writeChunk(&chunk, constant3, 1); // 5.6
    writeChunk(&chunk, OP_DIVIDE, 1);
    writeChunk(&chunk, OP_NEGATE, 1);
    writeChunk(&chunk, OP_RETURN, 1);
    interpret(&chunk);

    freeVM();
    freeChunk(&chunk);
    return 0;
}
