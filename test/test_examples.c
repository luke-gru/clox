#include <dirent.h>
#include <errno.h>
#include <string.h>
#include "test.h"
#include "compiler.h"
#include "vm.h"
#include "memory.h"

#define FILENAME_BUFSZ (300)

static char lineBuf[1024];

DIR *getDir(const char *name) {
    DIR *dir = opendir(name);
    return dir; // can be NULL
}

// returns string containing lines of expected output after "__END__\n-- expect: --"
// in given file.
static ObjString *fileExpectStr(FILE *f) {
    bool inEnd = false;
    bool inExpect = false;
    ObjString *str = hiddenString("", 0);
    while (fgets(lineBuf, sizeof(lineBuf), f) != NULL) {
        if (inEnd) {
            if (inExpect) {
                pushCString(str, lineBuf, strlen(lineBuf));
                memset(lineBuf, 0, 1024);
            } else if (strncmp(lineBuf, "-- expect: --", 13) == 0) {
                inExpect = true;
            } else if (strncmp(lineBuf, "-- noexpect: --", 15) == 0) {
                unhideFromGC((Obj*)str);
                return NULL;
            }
        } else if (strncmp(lineBuf, "__END__", 7) == 0) {
            inEnd = true;
        } else { continue; }
    }
    return str;
}

static int stringDiff(ObjString *actual, ObjString *expect) {
    if (strlen(actual->chars) != strlen(expect->chars)) {
        return -1;
    }
    return strcmp(actual->chars, expect->chars);
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
    const char *filePrefix = "./examples/";
    size_t filePrefixLen = strlen(filePrefix);
    while ((ent = readdir(d))) {
        if (ent->d_type != DT_REG) { // reg. file
            continue;
        }
        char *extBeg = strstr(ent->d_name, ".lox");
        size_t entSz = strlen(ent->d_name);
        if (extBeg == NULL || extBeg != ent->d_name+entSz-4) {
            fprintf(stderr, "Skipping file '%s', not '.lox' extension\n", ent->d_name);
            continue;
        }
        if (entSz+filePrefixLen+1 > FILENAME_BUFSZ) {
            fprintf(stderr, "Skipping file '%s', filename too long\n", ent->d_name);
            continue;
        }
        memset(fbuf, 0, FILENAME_BUFSZ);
        strcat(fbuf, filePrefix);
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
            fclose(f);
            continue;
        }
        fprintf(stdout, "Running file '%s'...\n", ent->d_name);
        ObjString *outputStr = hiddenString("", 0);
        setPrintBuf(outputStr, true);
        InterpretResult ires = interpret(&chunk, ent->d_name);
        if (ires != INTERPRET_OK) {
            fprintf(stderr, "Error during interpretation: (%d)\n", ires);
            numErrors++;
            fclose(f);
            unhideFromGC((Obj*)outputStr);
            freeObject((Obj*)outputStr, true);
            freeVM();
            continue;
        }
        runAtExitHooks();
        resetStack();
        ObjString *outputExpect = fileExpectStr(f);
        if (outputExpect == NULL || stringDiff(outputStr, outputExpect) == 0) {
            fprintf(stdout, "Success\n");
            numSuccesses++;
            T_ASSERT(true);
        } else {
            fprintf(stderr, "---- Expected: ----\n");
            fprintf(stderr, "%s\n", outputExpect->chars);
            fprintf(stderr, "---- Actual: ----\n");
            fprintf(stderr, "%s\n", outputStr->chars);
            fprintf(stderr, "Failure\n");
            numErrors++;
        }
        unhideFromGC((Obj*)outputStr);
        freeObject((Obj*)outputStr, true);
        if (outputExpect) {
            unhideFromGC((Obj*)outputExpect);
            freeObject((Obj*)outputExpect, true);
        }
        fclose(f);
        freeVM();
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
    initSighandlers();
    initVM();
    INIT_TESTS();
    RUN_TEST(test_run_example_files);
    END_TESTS();
}
