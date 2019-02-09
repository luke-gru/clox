#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"
#include "options.h"
#include "runtime.h"
#include "memory.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

static void usage(int exitstatus) {
    fprintf(stdout, "Usage:\n"
        "clox [-f FILE] [OPTIONS]\n"
    );
    exit(exitstatus);
}

int main(int argc, char *argv[]) {
    initOptions(argc, argv);
    char *fname = NULL;
    char **argvp = argv+1;
    int i = 0;
    int incrOpt = 0;
    bool interactive = false;
    bool useStdin = false;
    while (argvp[i] != NULL) {
        if ((incrOpt = parseOption(argvp, i)) > 0) {
            i+=incrOpt;
            continue;
        }
        if (strncmp(argvp[i], "-i", 2) == 0) {
            interactive = true;
            i+=1;
        } else if (strncmp(argvp[i], "-", 1) == 0) {
            useStdin = true;
            i+=1;
        } else {
            fprintf(stderr, "Invalid option: %s\n", argvp[i]);
            usage(1);
        }
    }

    fname = GET_OPTION(initialScript); // set to "" if not given
    if (strlen(fname) > 0) {
        useStdin = false;
        interactive = false;
    } else if (!interactive && !useStdin && strlen(fname) == 0) {
        interactive = true;
    }

    // Normalize filename to full path to file. Don't yet check if the file
    // exists, though.
    if (!interactive && strlen(fname) > 0 && fname[0] != pathSeparator) {
        char dirbuf[350];
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
            char *fullPath = (char*)calloc(1, strlen(dirbuf)+1);
            ASSERT_MEM(fullPath);
            strcpy(fullPath, dirbuf);
            fname = fullPath;
            SET_OPTION(initialScript, fname);
        }
    }

    initCoreSighandlers();

    CompileErr err = COMPILE_ERR_NONE;
    if (interactive) {
        repl();
        exit(0);
    }

    if (useStdin) {
        const char *tmpTemplate = "/tmp/clox-stdin-XXXXXX";
        char tmpNameBuf[32];
        strncpy(tmpNameBuf, tmpTemplate, strlen(tmpTemplate));
        int tmpfno = mkstemp(tmpNameBuf);
        if (tmpfno < 0) {
            die("mkstemp error while creating tempfile name: %s", strerror(errno));
        }
        char buf[4096];
        ssize_t nread = 0;
        int stdinno = fileno(stdin);
        while ((nread = read(stdinno, buf, 4096)) > 0) {
            ssize_t writeRes = write(tmpfno, buf, nread);
            if (writeRes < 0) {
                unlink(tmpNameBuf);
                die("Error writing to tmpfile: %s", strerror(errno));
            }
        }
        if (nread < 0) {
            unlink(tmpNameBuf);
            die("Error reading from stdin: %s", strerror(errno));
        }
        fname = strdup(tmpNameBuf);
    }

    initVM();
    Chunk *chunk = compile_file(fname, &err);
    if (useStdin) {
        unlink(fname);
    }

    if (err != COMPILE_ERR_NONE || !chunk) {
        if (err == COMPILE_ERR_SYNTAX) {
            freeVM();
            die("%s", "Syntax error");
        } else {
            freeVM();
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
        freeChunk(chunk);
        FREE(Chunk, chunk);
        printf("No compilation errors\n");
        exit(0);
    }

    InterpretResult ires = interpret(chunk, fname);
    if (ires != INTERPRET_OK) {
        // error message was already displayed
        stopVM(1);
    }

    stopVM(0);
}
