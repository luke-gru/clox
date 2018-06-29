#include <dirent.h>
#include <errno.h>
#include <string.h>
#include "test.h"
#include "compiler.h"
#include "vm.h"

#define FILENAME_BUFSZ (300)

DIR *getDir(const char *name) {
    DIR *dir = opendir(name);
    return dir; // can be NULL
}

static int test_run_example_files(void) {
    DIR *d = getDir("./examples");
    char fbuf[FILENAME_BUFSZ] = { '\0' };
    if (d == NULL) {
        fprintf(stderr, "[ERROR]: Cannot open './examples' directory.\n");
        exit(1);
    }
    int numEntsFound = 0;
    struct dirent *ent = NULL;
    int numErrors = 0;
    int numSuccesses = 0;
    Chunk chunk;
    while ((ent = readdir(d))) {
        if (ent->d_type != DT_REG) { // reg. file
            continue;
        }
        if (strstr(ent->d_name, ".lox") == NULL) {
            continue;
        }
        memset(fbuf, 0, FILENAME_BUFSZ);
        strcat(fbuf, "./examples/");
        strcat(fbuf, ent->d_name);
        fprintf(stderr, "Opening file '%s'\n", fbuf);
        FILE *f = fopen(fbuf, "r");
        if (!f) {
            fprintf(stderr, "[ERROR]: Cannot open file '%s'\n", fbuf);
            exit(1);
        }
        numEntsFound++;
        CompileErr cerr = COMPILE_ERR_NONE;
        fprintf(stdout, "Compiling file '%s'...\n", ent->d_name);
        initChunk(&chunk);
        initVM();
        int compres = compile_file(fbuf, &chunk, &cerr);
        if (compres != 0 || cerr != COMPILE_ERR_NONE) {
            fprintf(stderr, "Error during compilation\n");
            freeVM();
            numErrors++;
            continue;
        }
        fprintf(stdout, "Running file '%s'...\n", ent->d_name);
        InterpretResult ires = interpret(&chunk);
        freeVM();
        if (ires != INTERPRET_OK) {
            fprintf(stderr, "Error during interpretation: (%d)\n", ires);
            numErrors++;
            continue;
        }
        fprintf(stdout, "Success\n");
        numSuccesses++;
        T_ASSERT(true);
    }

    if (numEntsFound == 0) {
        fprintf(stderr, "[ERROR]: Cannot read directory entry, error: '%s'\n", strerror(errno));
        T_ASSERT(numEntsFound > 0);
    }

    T_ASSERT_EQ(0, numErrors);
    T_ASSERT(numSuccesses > 0);
cleanup:
    return 0;
}

int main(int argc, char *argv[]) {
    parseTestOptions(argc, argv);
    initVM();
    INIT_TESTS();
    RUN_TEST(test_run_example_files);
    END_TESTS();
}
