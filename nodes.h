#ifndef clox_nodes_h
#define clox_nodes_h

#include <stdbool.h>
#include "vec.h"
#include "scanner.h"
#include "debug.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sNode;
typedef vec_t(struct sNode*) vec_nodep_t;

typedef enum eExprType {
    BINARY_EXPR = 1,
    LOGICAL_EXPR,
    GROUPING_EXPR,
    LITERAL_EXPR, // numbers, strings
    ARRAY_EXPR, // []
    MAP_EXPR, // %{}
    INDEX_GET_EXPR,
    INDEX_SET_EXPR,
    UNARY_EXPR,
    VARIABLE_EXPR,
    CONSTANT_EXPR,
    CONSTANT_LOOKUP_EXPR,
    ASSIGN_EXPR,
    CALL_EXPR,
    CALL_BLOCK_EXPR,
    TO_BLOCK_EXPR,
    ANON_FN_EXPR,
    PROP_ACCESS_EXPR,
    PROP_SET_EXPR,
    PROP_SET_BINOP_EXPR,
    THIS_EXPR,
    SUPER_EXPR,
    SPLAT_EXPR,
    BINARY_ASSIGN_EXPR, // 24
} ExprType;

static const char *exprTypeNames[] = {
    "BINARY_EXPR",
    "LOGICAL_EXPR",
    "GROUPING_EXPR",
    "LITERAL_EXPR",
    "ARRAY_EXPR",
    "MAP_EXPR",
    "INDEX_GET_EXPR",
    "INDEX_SET_EXPR",
    "UNARY_EXPR",
    "VARIABLE_EXPR",
    "CONSTANT_EXPR",
    "ASSIGN_EXPR",
    "CALL_EXPR",
    "CALL_BLOCK_EXPR",
    "TO_BLOCK_EXPR",
    "ANON_FN_EXPR",
    "PROP_ACCESS_EXPR",
    "PROP_SET_EXPR",
    "THIS_EXPR",
    "SUPER_EXPR",
    "SPLAT_EXPR",
    "BINARY_ASSIGN_EXPR",
    NULL
};

#define STMT_TYPE_ENUM_FIRST 25
typedef enum eStmtType {
    EXPR_STMT = STMT_TYPE_ENUM_FIRST,
    PRINT_STMT,
    VAR_STMT,
    BLOCK_STMT,
    IF_STMT,
    WHILE_STMT,
    FOR_STMT,
    FOREACH_STMT,
    CONTINUE_STMT,
    BREAK_STMT,
    FUNCTION_STMT,
    METHOD_STMT,
    CLASS_METHOD_STMT,
    GETTER_STMT,
    SETTER_STMT,
    RETURN_STMT,
    CLASS_STMT,
    MODULE_STMT,
    TRY_STMT,
    CATCH_STMT,
    THROW_STMT,
    IN_STMT,
    STMTLIST_STMT,
    KWARG_IN_CALL_STMT
} StmtType;

static const char *stmtTypeNames[] = {
    "EXPR_STMT",
    "PRINT_STMT",
    "VAR_STMT",
    "BLOCK_STMT",
    "IF_STMT",
    "WHILE_STMT",
    "FOR_STMT",
    "FOREACH_STMT",
    "CONTINUE_STMT",
    "BREAK_STMT",
    "FUNCTION_STMT",
    "RETURN_STMT",
    "CLASS_STMT",
    "MODULE_STMT",
    "TRY_STMT",
    "CATCH_STMT",
    "THROW_STMT",
    "IN_STMT",
    "STMTLIST_STMT",
    "KWARG_IN_CALL_STMT",
    NULL
};

typedef enum eOtherType {
    PARAM_NODE_REGULAR = 1,
    PARAM_NODE_DEFAULT_ARG,
    PARAM_NODE_KWARG,
    PARAM_NODE_SPLAT,
    PARAM_NODE_BLOCK,
    TOKEN_NODE
} OtherType;

typedef struct ParamNodeInfo {
    size_t defaultArgIPOffset;
} ParamNodeInfo;

typedef enum eNodeType {
    NODE_EXPR = 1,
    NODE_STMT,
    NODE_OTHER // ex: param nodes
} NodeType;

typedef enum eLiteralType {
    LIT_TYPE_NONE = 0,
    NUMBER_TYPE,
    STRING_TYPE,
    STATIC_STRING_TYPE,
    REGEX_TYPE,
    NIL_TYPE,
    BOOL_TYPE,

    // TODO: put somewhere else, these aren't literals
    SUPER_CALL,
    SUPER_PROP
} LiteralType;

typedef struct sNodeType {
    NodeType type; // stmt or expr
    int kind; // what kind of stmt/expr/other
    int litKind; // if a literal expr, what kind?
} node_type_t;

typedef struct sNode {
    void *data; // used in some circumstances, but try to avoid it
    node_type_t type;
    Token tok;
    vec_nodep_t *children;
    struct sNode *parent;
} Node;

extern int astDetailLevel; // for printing AST

typedef void (*NodeCallback)(Node *n, int idx);

Node *createNode(node_type_t type, Token tok, vec_nodep_t *children);
void nodeAddChild(Node *node, Node *child);
static inline void *nodeGetData(Node *node) {
    return node->data;
}
static inline void nodeAddData(Node *node, void *data) {
    node->data = data;
}
void nodeForeachChild(Node *node, NodeCallback cb);
void freeNode(Node *node, bool freeChildren);

static inline NodeType nodeType(Node *n) {
    DBG_ASSERT(n);
    return n->type.type;
}

static inline int nodeKind(Node *n) {
    DBG_ASSERT(n);
    return n->type.kind;
}

char *outputASTString(Node *node, int indentLevel);

static inline const char *nodeKindStr(int nKind) {
    if (nKind >= STMT_TYPE_ENUM_FIRST) {
        return stmtTypeNames[nKind-STMT_TYPE_ENUM_FIRST];
    } else {
        return exprTypeNames[nKind-1];
    }
}

#ifdef __cplusplus
}
#endif

#endif
