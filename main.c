#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"
#include "options.h"
#include <stdio.h>

static void usage(int exitstatus) {
    fprintf(stdout, "Usage:\n"
        "clox [-f FILE] [-DDEBUG_PARSER]\n"
    );
    exit(exitstatus);
}

int main(int argc, char *argv[]) {
    Chunk chunk;
    initChunk(&chunk);

    initOptions();
    char *fname = NULL;
    char **argvp = argv+1;
    while (*argvp != NULL) {
        if (strcmp(*argvp, "-f") == 0 && argvp[1]) {
            fname = argvp[1];
            argvp += 2;
        } else if (strcmp(*argvp, "-DDEBUG_PARSER") == 0) {
            SET_OPTION(debugParser, true);
            argvp += 1;
        } else {
            fprintf(stderr, "Invalid option: %s\n", *argvp);
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
