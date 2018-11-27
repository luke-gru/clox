#ifndef clox_nodes_h
#define clox_nodes_h

#include <stdbool.h>
#include "vec.h"
#include "scanner.h"

struct sNode;
typedef vec_t(struct sNode*) vec_nodep_t;

typedef enum eExprType {
    BINARY_EXPR = 1,
    LOGICAL_EXPR,
    GROUPING_EXPR,
    LITERAL_EXPR,
    ARRAY_EXPR,
    INDEX_GET_EXPR,
    INDEX_SET_EXPR,
    UNARY_EXPR,
    VARIABLE_EXPR,
    ASSIGN_EXPR,
    CALL_EXPR,
    ANON_FN_EXPR,
    PROP_ACCESS_EXPR,
    PROP_SET_EXPR,
    THIS_EXPR,
    SUPER_EXPR,
    SPLAT_EXPR,
} ExprType;

static const char *exprTypeNames[] = {
    "BINARY_EXPR",
    "LOGICAL_EXPR",
    "GROUPING_EXPR",
    "LITERAL_EXPR",
    "ARRAY_EXPR",
    "INDEX_GET_EXPR",
    "INDEX_SET_EXPR",
    "UNARY_EXPR",
    "VARIABLE_EXPR",
    "ASSIGN_EXPR",
    "CALL_EXPR",
    "ANON_FN_EXPR",
    "PROP_ACCESS_EXPR",
    "PROP_SET_EXPR",
    "THIS_EXPR",
    "SUPER_EXPR",
    "SPLAT_EXPR",
    NULL
};

typedef enum eStmtType {
    EXPR_STMT = 20,
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
    PARAM_NODE_SPLAT,
    PARAM_NODE_KWARG,
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
void nodeAddData(Node *node, void *data);
void *nodeGetData(Node *node);
void nodeForeachChild(Node *node, NodeCallback cb);
void freeNode(Node *node, bool freeChildren);

NodeType nodeType(Node *n); // expr or stmt or other
int nodeKind(Node *n); // the actual node kind

char *outputASTString(Node *node, int indentLevel);

static inline const char *nodeKindStr(int nKind) {
    if (nKind >= 20) {
        return stmtTypeNames[nKind-20];
    } else {
        return exprTypeNames[nKind-1];
    }
}

#endif
