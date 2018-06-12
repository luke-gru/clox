#include <stdio.h>
#include "nodes.h"
#include "scanner.h"
#include "common.h"
#include "memory.h"

#ifndef ASSERT_MEM
#define ASSERT_MEM(p)
#endif

Node *createNode(node_type_t type, Token tok, vec_nodep_t *children) {
    Node *n = ALLOCATE(Node, 1);
    ASSERT_MEM(n);
    n->type = type;
    n->tok = tok;
    n->data = NULL;
    if (children == NULL) {
        n->children = calloc(sizeof(vec_nodep_t), 1);
        ASSERT_MEM(n->children);
        vec_init(n->children);
    } else {
        n->children = children;
    }
    return n;
}

void nodeAddChild(Node *node, Node *child) {
    ASSERT_MEM(node->children);
    vec_push(node->children, child);
    child->parent = node;
}

void nodeAddData(Node *node, void *data) {
    node->data = data;
}

void nodeForeachChild(Node *node, NodeCallback cb) {
    int i = 0;
    Node *n = NULL;
    vec_foreach(node->children, n, i) {
        cb(n, i);
    }
}

// forward decl
void freeNode(Node *node, bool freeChildren);

static void freeChildNodeCb(Node *node, int idx) {
    if (node) freeNode(node, true);
}

void freeNode(Node *node, bool freeChildren) {
    if (freeChildren && node->children != NULL) {
        nodeForeachChild(node, freeChildNodeCb);
    }
    free(node);
}

static char *strAdd(char *a, char *b) {
    char *result = calloc(strlen(a)+strlen(b)+1, 1);
    ASSERT_MEM(result);
    strcpy(result, a);
    strcat(result, b);
    return result;
}

static char *i(int indentLevel) {
    char *buf = "";
    for (int i = 0; i < indentLevel; i++) {
        buf = strAdd(buf, "  ");
    }
    return buf;
}

static char *outputBinaryExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputLogicalExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputGroupingExpr(Node *n, int indentLevel) {
    Node *exprNode = vec_first(n->children);
    char *expr = outputASTString(exprNode, indentLevel);
    char *buf = calloc(strlen(expr)+1+8, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "(group %s)", expr);
    return buf;
}
static char *outputLiteralExpr(Node *n, int indentLevel) {
    switch (n->type.litKind) {
        case NUMBER_TYPE:
        case NIL_TYPE:
        case BOOL_TYPE: {
            char *buf = calloc(strlen(tokStr(&n->tok))+1, 1);
            ASSERT_MEM(buf);
            sprintf(buf, "%s", tokStr(&n->tok));
            return buf;
        }
        case STRING_TYPE: {
            char *buf = calloc(strlen(tokStr(&n->tok))+1+2, 1);
            ASSERT_MEM(buf);
            sprintf(buf, "\"%s\"", tokStr(&n->tok));
            return buf;
        }
        default:
            return "unreachable";
    }
}
static char *outputArrayExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputIndexGetExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputIndexSetExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputUnaryExpr(Node *n, int indentLevel) {
    const char *op = tokStr(&n->tok);
    char *expr = outputASTString(vec_first(n->children),
        indentLevel);
    char *buf = calloc(strlen(op)+strlen(expr)+1+3, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "(%s %s)", op, expr);
    return buf;
}
static char *outputVariableExpr(Node *n, int indentLevel) {
    const char *varName = tokStr(&n->tok);
    char *buf = calloc(strlen(varName)+1+6, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "(var %s)", varName);
    return buf;
}
static char *outputAssignExpr(Node *n, int indentLevel) {
    const char *varName = tokStr(&n->tok);
    char *value = outputASTString(n, indentLevel);
    char *buf = calloc(strlen(varName)+1+strlen(value)+10, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "(assign %s %s)", varName, value);
    return buf;
}
static char *outputCallExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputAnonFnExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputPropAccessExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputPropSetExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputThisExpr(Node *n, int indentLevel) {
    return "(var this)";
}
static char *outputSuperExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputSplatCallExpr(Node *n, int indentLevel) {
    return "";
}
static char *outputKeywordArgExpr(Node *n, int indentLevel) {
    return "";
}

static char *outputExpressionStmt(Node *n, int indentLevel) {
    Node *expr = vec_first(n->children);
    char *exprStr = outputASTString(expr, indentLevel);
    char *indent = i(indentLevel);
    char *buf = calloc(strlen(indent)+1+strlen(exprStr), 1);
    ASSERT_MEM(buf);
    sprintf(buf, "%s%s", indent, exprStr);
    return buf;
}
static char *outputPrintStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    Node *printExpr = vec_first(n->children);
    char *printStr = outputASTString(printExpr, indentLevel);
    char *buf = calloc(strlen(indent)+1+strlen(printStr)+8, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "%s(print %s)", indent, printStr);
    return buf;
}
static char *outputVarStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    const char *varName = tokStr(&n->tok);
    char *varExpr = outputASTString(vec_first(n->children), indentLevel);
    char *buf = calloc(strlen(indent)+1+strlen(varName)+strlen(varExpr)+12, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "%s(varDecl %s %s)\n", indent, varName, varExpr);
    return buf;
}
static char *outputBlockStmt(Node *n, int indentLevel) {
    char *buf = "";
    char *indent = i(indentLevel);
    Node *stmtListNode = vec_first(n->children);
    if (stmtListNode->children->length > 0) {
        // TODO: indent block properly
        buf = "(block\n";
        char *stmtListOutput = outputASTString(stmtListNode, indentLevel+1);
        buf = strAdd(buf, stmtListOutput);
        buf = strAdd(buf, "\n)\n");
    } else {
        char *bufFmt = "%s(block)";
        buf = calloc(strlen(indent)+1+7, 1);
        ASSERT_MEM(buf);
        sprintf(buf, bufFmt, indent);
    }
    return buf;
}
static char *outputIfStmt(Node *n, int indentLevel) {
    return "";
}
static char *outputWhileStmt(Node *n, int indentLevel) {
    return "";
}
static char *outputForStmt(Node *n, int indentLevel) {
    return "";
}
static char *outputForeachStmt(Node *n, int indentLevel) {
    return "";
}
static char *outputContinueStmt(Node *n, int indentLevel) {
    return "";
}
static char *outputBreakStmt(Node *n, int indentLevel) {
    return "";
}
// fn decl
static char *outputFunctionStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    char *startFmt = "%s(fnDecl ";
    char *buf = calloc(strlen(indent)+1+8, 1);
    ASSERT_MEM(buf);
    sprintf(buf, startFmt, indent);
    Token tokName = n->tok;
    char *name = tokStr(&tokName);
    buf = strAdd(buf, name);
    buf = strAdd(buf, " (");
    vec_nodep_t *params = (vec_nodep_t*)n->data;
    ASSERT_MEM(params);
    Node *param = NULL; int i = 0;
    int len = params->length;
    vec_foreach(params, param, i) {
        buf = strAdd(buf, tokStr(&param->tok));
        if (i != len-1) {
            buf = strAdd(buf, " ");
        }
    }
    buf = strAdd(buf, ") ");
    Node *blockStmt = vec_first(n->children);
    buf = strAdd(buf, outputASTString(blockStmt, indentLevel));
    buf = strAdd(buf, ")\n");
    return buf;
}
static char *outputReturnStmt(Node *n, int indentLevel) {
    return "";
}
static char *outputClassStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    char *className = tokStr(&n->tok);
    char *startFmt = "%s(classDecl %s";
    char *buf = calloc(strlen(indent)+1+11+strlen(className), 1);
    ASSERT_MEM(buf);
    sprintf(buf, startFmt, indent, className);
    // superclass token
    if (n->data != NULL) {
        char *superName = tokStr((Token*)n->data);
        buf = strAdd(buf, " ");
        buf = strAdd(buf, superName);
    }
    buf = strAdd(buf, "\n");
    Node *bodyNode = vec_first(n->children);
    char *bodyOut = outputASTString(bodyNode, indentLevel+1);
    buf = strAdd(buf, bodyOut);
    char *endFmt = "\n%s)\n";
    char *endBuf = calloc(strlen(indent)+1+3, 1);
    ASSERT_MEM(endBuf);
    sprintf(endBuf, endFmt, indent);
    buf = strAdd(buf, endBuf);
    return buf;
}

static char *outputModuleStmt(Node *n, int indentLevel) {
    return "";
}
static char *outputTryStmt(Node *n, int indentLevel) {
    return "";
}

static char *outputCatchStmt(Node *n, int indentLevel) {
    return "";
}

static char *outputThrowStmt(Node *n, int indentLevel) {
    Node *throwExpr = vec_first(n->children);
    char *indent = i(indentLevel);
    char *throwStr = outputASTString(throwExpr, indentLevel);
    char *buf = calloc(strlen(indent)+1+strlen(throwStr)+9, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "%s(throw %s)\n", indent, throwStr);
    return buf;
}

static char *outputInStmt(Node *n, int indentLevel) {
    Node *inExpr = vec_first(n->children);
    char *indent = i(indentLevel);
    char *inStr = outputASTString(inExpr, indentLevel);
    char *buf = calloc(strlen(indent)+1+strlen(inStr)+6, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "%s(in %s)\n", indent, inStr);
    Node *blockStmt = n->children->data[1];
    char *inBlockBuf = outputASTString(blockStmt, indentLevel+1);
    return strAdd(buf, inBlockBuf);
}

static char *outputStmtlistStmt(Node *n, int indentLevel) {
    char *buf = (char*)"";
    int i = 0;
    Node *child = NULL;
    vec_foreach(n->children, child, i) {
        char *childBuf = outputASTString(child, indentLevel);
        buf = strAdd(buf, childBuf);
    }
    return buf;
}

char *outputASTString(Node *node, int indentLevel) {
    switch (node->type.type) {
        case NODE_EXPR: {
            switch(node->type.kind) {
                case BINARY_EXPR:
                    return outputBinaryExpr(node, indentLevel);
                case LOGICAL_EXPR:
                    return outputLogicalExpr(node, indentLevel);
                case GROUPING_EXPR:
                    return outputGroupingExpr(node, indentLevel);
                case LITERAL_EXPR:
                    return outputLiteralExpr(node, indentLevel);
                case ARRAY_EXPR:
                    return outputArrayExpr(node, indentLevel);
                case INDEX_GET_EXPR:
                    return outputIndexGetExpr(node, indentLevel);
                case INDEX_SET_EXPR:
                    return outputIndexSetExpr(node, indentLevel);
                case UNARY_EXPR:
                    return outputUnaryExpr(node, indentLevel);
                case VARIABLE_EXPR:
                    return outputVariableExpr(node, indentLevel);
                case ASSIGN_EXPR:
                    return outputAssignExpr(node, indentLevel);
                case CALL_EXPR:
                    return outputCallExpr(node, indentLevel);
                case ANON_FN_EXPR:
                    return outputAnonFnExpr(node, indentLevel);
                case PROP_ACCESS_EXPR:
                    return outputPropAccessExpr(node, indentLevel);
                case PROP_SET_EXPR:
                    return outputPropSetExpr(node, indentLevel);
                case THIS_EXPR:
                    return outputThisExpr(node, indentLevel);
                case SUPER_EXPR:
                    return outputSuperExpr(node, indentLevel);
                case SPLAT_CALL_EXPR:
                    return outputSplatCallExpr(node, indentLevel);
                case KEYWORD_ARG_EXPR:
                    return outputKeywordArgExpr(node, indentLevel);
                default:
                    return "unreachable";
            }
        }
        case NODE_STMT: {
            switch(node->type.kind) {
                case EXPR_STMT:
                    return outputExpressionStmt(node, indentLevel);
                case PRINT_STMT:
                    return outputPrintStmt(node, indentLevel);
                case VAR_STMT:
                    return outputVarStmt(node, indentLevel);
                case BLOCK_STMT:
                    return outputBlockStmt(node, indentLevel);
                case IF_STMT:
                    return outputIfStmt(node, indentLevel);
                case WHILE_STMT:
                    return outputWhileStmt(node, indentLevel);
                case FOR_STMT:
                    return outputForStmt(node, indentLevel);
                case FOREACH_STMT:
                    return outputForeachStmt(node, indentLevel);
                case CONTINUE_STMT:
                    return outputContinueStmt(node, indentLevel);
                case BREAK_STMT:
                    return outputBreakStmt(node, indentLevel);
                case FUNCTION_STMT:
                    return outputFunctionStmt(node, indentLevel);
                case RETURN_STMT:
                    return outputReturnStmt(node, indentLevel);
                case CLASS_STMT:
                    return outputClassStmt(node, indentLevel);
                case MODULE_STMT:
                    return outputModuleStmt(node, indentLevel);
                case TRY_STMT:
                    return outputTryStmt(node, indentLevel);
                case CATCH_STMT:
                    return outputCatchStmt(node, indentLevel);
                case THROW_STMT:
                    return outputThrowStmt(node, indentLevel);
                case IN_STMT:
                    return outputInStmt(node, indentLevel);
                case STMTLIST_STMT:
                    return outputStmtlistStmt(node, indentLevel);
                default:
                    return "unreachable";
            }
        }
    }
    return "unreachable";
}
