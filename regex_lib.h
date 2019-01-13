#ifndef clox_regex_h
#define clox_regex_h
#include <stdbool.h>

typedef enum RNodeType {
    NODE_ATOM = 1, // a,b,c,etc.
    NODE_GROUP, // ()
    NODE_OR, // |
    NODE_REPEAT, // +
    NODE_REPEAT_Z, // *
    NODE_REPEAT_N, // {n[,m]}
    NODE_CCLASS, // [aeiou]
    NODE_ECLASS, // \d,\w,\s
    NODE_ANCHOR, // ^,$
    NODE_DOT, // .
    NODE_PROGRAM // top-most node
} RNodeType;

// escape class types
typedef enum REClassType {
    ECLASS_NONE = 0, // not an escape class
    ECLASS_DIGIT, // \d
    ECLASS_NON_DIGIT, // \D
    ECLASS_SPACE, // \s
    ECLASS_NON_SPACE, // \S
    ECLASS_WORD, // \w
    ECLASS_NON_WORD, // \W
    ECLASS_WORD_BOUNDARY, // \b
    ECLASS_NON_WORD_BOUNDARY // \B
} REClassType;

typedef enum RAnchorType {
    ANCHOR_NONE = 0, // not an anchor
    ANCHOR_BOS, // \A
    ANCHOR_EOS, // \Z
    ANCHOR_BOL, // ^
    ANCHOR_EOL // $
} RAnchorType;

typedef struct RNode {
    const char *tok; // not owned
    int toklen;
    int nodelen; // includes child tokens, if any
    long repeat_min;
    long repeat_max;
    REClassType eclass_type;
    RNodeType type;
    RAnchorType anchor_type;
    struct RNode *next;
    struct RNode *prev;
    struct RNode *parent;
    struct RNode *children;
} RNode;

typedef struct RegexOptions {
    bool case_insensitive;
    bool multiline; // don't end on \n
} RegexOptions;

typedef struct Regex {
    RNode *node;
    const char *src; // owned
    RegexOptions opts;
} Regex;

typedef enum RegexCompileResult {
    REGEX_UNITIALIZED_ERR,
    REGEX_PARSE_ERR,
    REGEX_COMPILE_ERR,
    REGEX_COMPILE_SUCCESS
} RegexCompileResult;

typedef struct MatchData {
    bool matched;
    int match_start;
    int match_len;
} MatchData;

void regex_init(Regex *regex, const char *src, RegexOptions *opts);
void regex_free(Regex *regex);
RegexCompileResult regex_compile(Regex *regex);
MatchData regex_match(Regex *regex, const char *string);
void regex_output_ast(Regex *regex);
const char *nodeTypeName(RNodeType nodeType);

#endif
