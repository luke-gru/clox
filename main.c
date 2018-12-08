#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"
#include "options.h"
#include "runtime.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static void usage(int exitstatus) {
    fprintf(stdout, "Usage:\n"
        "clox [-f FILE] [OPTIONS]\n"
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
        if ((incrOpt = parseOption(argvp, i)) > 0) {
            i+=incrOpt;
            continue;
        }
        if (strncmp(argvp[i], "-i", 2) == 0) {
            interactive = true;
            i+=1;
        } else {
            fprintf(stderr, "Invalid option: %s\n", argvp[i]);
            usage(1);
        }
    }

    fname = GET_OPTION(initialScript);
    if (strlen(fname) == 0) {
        interactive = true;
    }

    // Normalize filename to full path to file. Don't yet check if the file
    // exists, though.
    if (!interactive && strlen(fname) > 0 && fname[0] != pathSeparator) {
        char dirbuf[350] = { '\0' };
        memset(dirbuf, 0, 350);
        char *cwdres = getcwd(dirbuf, 250);
        if (cwdres != NULL) {
            if (dirbuf[strlen(dirbuf)-1] != pathSeparator) {
                strncat(dirbuf, &pathSeparator, 1);
            }
            if (fname[0] == '.' && fname[1] && fname[1] == pathSeparator) {
                strcat(dirbuf, fname+2);
            } else {
                strcat(dirbuf, fname);
            }
            char *fullPath = calloc(strlen(dirbuf)+1, 1);
            ASSERT_MEM(fullPath);
            strcpy(fullPath, dirbuf);
            fname = fullPath;
            SET_OPTION(initialScript, fname);
        }
    }

    initSigHandlers();

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
            die("%s", "Syntax error");
        } else {
            freeVM();
            freeChunk(&chunk);
            if (err == COMPILE_ERR_ERRNO) {
                die("Compile error: %s", strerror(errno));
            } else {
                // error reported elsewhere, in compiler.c
                die("%s", "Compile error");
            }
        }
    }

    if (CLOX_OPTION_T(compileOnly)) {
        freeVM();
        freeChunk(&chunk);
        printf("No compilation errors\n");
        exit(0);
    }

    InterpretResult ires = interpret(&chunk, fname);
    if (ires != INTERPRET_OK) {
        freeVM();
        freeChunk(&chunk);
        // error message was already displayed
        exit(1);
    }

    freeVM();
    freeChunk(&chunk);
    return 0;
}
