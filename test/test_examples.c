#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "test.h"
#include "compiler.h"
#include "vm.h"
#include "memory.h"

#define FILENAME_BUFSZ (300)

/*
 * For each example file ending in .lox in the examples directory, start a new
 * lox VM, run the example file and compare the standard output with what's in
 * the commented --expect-- section of the example file.
 */

static char lineBuf[1024];

int mainArgc = 1;
char **mainArgv = NULL;

// like stopVM() but for this test file
static void _freeVM() {
    vm.exiting = true;
    terminateThreads();
    freeVM();
    vm.exited = true;
    vm.numLivingThreads = 0;
}

static DIR *getDir(const char *name) {
    DIR *dir = opendir(name);
    return dir; // can be NULL
}

// returns string containing lines of expected output after "__END__\n-- expect: --"
// in given file.
static ObjString *fileExpectStr(FILE *f) {
    bool inEnd = false;
    bool inExpect = false;
    ObjString *str = hiddenString("", 0, NEWOBJ_FLAG_OLD);
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
    return strcmp(actual->chars, expect->chars);
}

static int test_run_example_files(void) {
    DIR *d = getDir("./examples");
    char *onlyFile = NULL; // run only the given example file (cmdline option)
    if (mainArgc > 1) {
      onlyFile = mainArgv[mainArgc-1]; // last given cmdline word
    }
    char fbuf[FILENAME_BUFSZ] = { '\0' };
    if (d == NULL) {
        fprintf(stderr, "[ERROR]: Cannot open './examples' directory.\n");
        exit(1);
    }
    int numEntsFound = 0;
    struct dirent *ent = NULL;
    int numErrors = 0;
    int numSuccesses = 0;
    char *filePrefix = malloc(4096);
    memset(filePrefix, 0, 4096);
    char *res = getcwd(filePrefix, 4096);
    if (!res) {
        fprintf(stderr, "error in getcwd: %s\n", strerror(errno));
        ASSERT(res); // fail
    }
    const char *filePrefixAdd = "/examples/";
    strcat(filePrefix, filePrefixAdd);
    size_t filePrefixLen = strlen(filePrefix);
    vec_str_t vfiles_failed;
    vec_init(&vfiles_failed);
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
        if (onlyFile != NULL && strstr(ent->d_name, onlyFile) == NULL) {
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
        initVM();
        ObjFunction *func = compile_file(fbuf, &cerr);
        if (cerr != COMPILE_ERR_NONE || !func) {
            fprintf(stderr, "Error during compilation\n");
            vec_push(&vfiles_failed, strdup(fbuf));
            _freeVM();
            numErrors++;
            fclose(f);
            continue;
        }
        fprintf(stdout, "Running file '%s'...\n", ent->d_name);
        ObjString *outputStr = hiddenString("", 0, NEWOBJ_FLAG_OLD);
        setPrintBuf(outputStr, true);
        unhideFromGC((Obj*)outputStr);
        // TODO: instead of passing ent->d_name, it should be the full path to the file
        // so that __DIR__ is populated correctly for the script.
        InterpretResult ires = interpret(func, fbuf);
        if (ires != INTERPRET_OK) {
            fprintf(stderr, "Error during interpretation: (%d)\n", ires);
            vec_push(&vfiles_failed, strdup(fbuf));
            numErrors++;
            fclose(f);
            _freeVM();
            continue;
        }
        runAtExitHooks(); // run the hooks now so that any 'print's will appear in printBuf
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
            vec_push(&vfiles_failed, strdup(fbuf));
            numErrors++;
        }
        if (outputExpect) {
            unhideFromGC((Obj*)outputExpect);
        }
        fclose(f);
        _freeVM();
    }

    if (numEntsFound == 0) {
        fprintf(stderr, "[ERROR]: Cannot read directory entry, error: '%s'\n", strerror(errno));
        T_ASSERT(numEntsFound > 0);
    }

    if (numErrors > 0) {
        fprintf(stderr, "Errors found in the following files:\n");
        char *file = NULL; int fidx = 0;
        vec_foreach(&vfiles_failed, file, fidx) {
            fprintf(stderr, "  %d) error in file '%s'\n", fidx+1, file);
            free(file);
        }
    }

    T_ASSERT_EQ(0, numErrors);
    T_ASSERT(numSuccesses > 0);
cleanup:
    vec_deinit(&vfiles_failed);
    return 0;
}



static void copyArgv(int argc, char *argv[]) {
  mainArgv = malloc(argc * sizeof(char*));
  for (int i = 0; argv[i] != NULL; i++) {
    mainArgv[i] = malloc(strlen(argv[i])+1);
    strcpy(mainArgv[i], argv[i]);
  }
}

int main(int argc, char *argv[]) {
    mainArgc = argc;
    copyArgv(argc, argv);
    parseTestOptions(argc, argv);
    initCoreSighandlers();
    INIT_TESTS();
    RUN_TEST(test_run_example_files);
    END_TESTS();
}
