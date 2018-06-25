#include <stdio.h>
#include "nodes.h"
#include "scanner.h"
#include "common.h"
#include "memory.h"
#include "debug.h"

// every time the "--print-ast" option is given, this is incremented. Given once, = 1.
int astDetailLevel = 0;

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
    if (child != NULL)
        child->parent = node;
}

void nodeAddData(Node *node, void *data) {
    node->data = data;
}

void *nodeGetData(Node *node) {
    return node->data;
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
    char *op = tokStr(&n->tok);
    Node *lhs = vec_first(n->children);
    Node *rhs = n->children->data[1];
    char *lhsOut = outputASTString(lhs, indentLevel);
    char *rhsOut = outputASTString(rhs, indentLevel);
    char *fmt = "(%s %s %s)";
    char *buf = calloc(strlen(op)+1+strlen(lhsOut)+strlen(rhsOut)+4, 1);
    ASSERT_MEM(buf);
    sprintf(buf, fmt, op, lhsOut, rhsOut);
    return buf;
}

// or/and
static char *outputLogicalExpr(Node *n, int indentLevel) {
    return outputBinaryExpr(n, indentLevel);
}

static char *outputGroupingExpr(Node *n, int indentLevel) {
    Node *exprNode = vec_first(n->children);
    char *exprOut = outputASTString(exprNode, indentLevel);
    char *buf = calloc(strlen(exprOut)+1+8, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "(group %s)", exprOut);
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
            char *buf = calloc(strlen(tokStr(&n->tok))+1, 1);
            ASSERT_MEM(buf);
            sprintf(buf, "%s", tokStr(&n->tok));
            return buf;
        }
        default: {
            UNREACHABLE("litkind=%d", n->type.litKind);
        }
    }
}

static char *outputArrayExpr(Node *n, int indentLevel) {
    char *buf = "(array";
    Node *el = NULL;
    int i = 0;
    vec_foreach(n->children, el, i) {
        buf = strAdd(buf, " ");
        buf = strAdd(buf, outputASTString(el, indentLevel));
    }
    buf = strAdd(buf, ")");
    return buf;
}

static char *outputIndexGetExpr(Node *n, int indentLevel) {
    char *buf = (char*)"(idxGet ";
    Node *left = vec_first(n->children);
    char *leftOut = outputASTString(left, indentLevel);
    buf = strAdd(buf, leftOut);
    buf = strAdd(buf, " ");
    Node *right = n->children->data[1];
    char *rightOut = outputASTString(right, indentLevel);
    buf = strAdd(buf, rightOut);
    buf = strAdd(buf, ")");
    return buf;
}

static char *outputIndexSetExpr(Node *n, int indentLevel) {
    char *buf = (char*)"(idxSet ";
    Node *left = vec_first(n->children);
    char *leftOut = outputASTString(left, indentLevel);
    buf = strAdd(buf, leftOut);
    buf = strAdd(buf, " ");
    Node *index = n->children->data[1];
    char *indexOut = outputASTString(index, indentLevel);
    buf = strAdd(buf, indexOut);
    buf = strAdd(buf, " ");
    Node *right = n->children->data[2];
    char *rightOut = outputASTString(right, indentLevel);
    buf = strAdd(buf, rightOut);
    buf = strAdd(buf, ")");
    return buf;
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
    char *lhsOut = outputASTString(vec_first(n->children), indentLevel);
    char *rhsOut = outputASTString(n->children->data[1], indentLevel);
    char *buf = calloc(strlen(lhsOut)+1+strlen(rhsOut)+10, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "(assign %s %s)", lhsOut, rhsOut);
    return buf;
}

static char *outputCallExpr(Node *n, int indentLevel) {
    Node *expr = vec_first(n->children);
    char *exprOut = outputASTString(expr, indentLevel);
    char *startFmt = "(call %s (";
    char *buf = calloc(strlen(exprOut)+1+8, 1);
    ASSERT_MEM(buf);
    sprintf(buf, startFmt, exprOut);
    int i = 0;
    Node *arg = NULL;
    vec_foreach(n->children, arg, i) {
        if (i == 0) continue;
        char *argOut = outputASTString(arg, indentLevel);
        buf = strAdd(buf, argOut);
        buf = strAdd(buf, " ");
    }
    buf = strAdd(buf, ")");
    return buf;
}

static char *outputFunctionStmt(Node *n, int indentLevel);

static char *outputAnonFnExpr(Node *n, int indentLevel) {
    return outputFunctionStmt(n, indentLevel);
}

static char *outputPropAccessExpr(Node *n, int indentLevel) {
    char *buf =  (char*)"(propGet ";
    char *lhsOut = outputASTString(vec_first(n->children), indentLevel);
    buf = strAdd(buf, lhsOut);
    buf = strAdd(buf, " ");
    char *propName = tokStr(&n->tok);
    buf = strAdd(buf, propName);
    buf = strAdd(buf, ")");
    return buf;
}

static char *outputPropSetExpr(Node *n, int indentLevel) {
    char *buf =  (char*)"(propSet ";
    char *lhsOut = outputASTString(vec_first(n->children), indentLevel);
    buf = strAdd(buf, lhsOut);
    buf = strAdd(buf, " ");
    char *propName = tokStr(&n->tok);
    buf = strAdd(buf, propName);
    buf = strAdd(buf, " ");
    char *rhsOut = outputASTString(n->children->data[1], indentLevel);
    buf = strAdd(buf, rhsOut);
    buf = strAdd(buf, ")");
    return buf;
}

static char *outputThisExpr(Node *n, int indentLevel) {
    return "(var this)";
}

static char *outputSuperExpr(Node *n, int indentLevel) {
    char *fmt = (char*)"(propGet super %s)";
    Node *tokNode = vec_first(n->children);
    char *propName = tokStr(&tokNode->tok);
    char *buf = calloc(strlen(propName)+1+16, 1);
    ASSERT_MEM(buf);
    sprintf(buf, fmt, propName);
    return buf;
}

static char *outputSplatCallExpr(Node *n, int indentLevel) {
    return ""; // TODO
}

static char *outputKeywordArgExpr(Node *n, int indentLevel) {
    return ""; // TODO
}

static char *outputExpressionStmt(Node *n, int indentLevel) {
    char *pre = "";
    if (astDetailLevel > 1) {
        pre = "(exprStmt ";
    }
    Node *expr = vec_first(n->children);
    char *exprStr = outputASTString(expr, indentLevel);
    char *indent = i(indentLevel);
    char *post = "";
    if (astDetailLevel > 1) {
        post = ")";
    }
    char *buf = calloc(strlen(indent)+1+strlen(exprStr)+strlen(pre)+strlen(post)+1, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "%s%s%s%s\n", indent, pre, exprStr, post);
    return buf;
}

static char *outputPrintStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    Node *printExpr = vec_first(n->children);
    char *printStr = outputASTString(printExpr, indentLevel);
    char *buf = calloc(strlen(indent)+1+strlen(printStr)+9, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "%s(print %s)\n", indent, printStr);
    return buf;
}

static char *outputVarStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    const char *varName = tokStr(&n->tok);
    char *varExpr = (char*)"";
    if (n->children->length > 0) {
        Node *val = vec_first(n->children);
        varExpr = outputASTString(val, indentLevel);
        varExpr = strAdd(" ", varExpr);
    }
    char *buf = calloc(strlen(indent)+1+strlen(varName)+
        strlen(varExpr)+11, 1);
    ASSERT_MEM(buf);
    sprintf(buf, "%s(varDecl %s%s)\n", indent, varName, varExpr);
    return buf;
}

static char *outputBlockStmt(Node *n, int indentLevel) {
    char *buf;
    char *indent = i(indentLevel);
    Node *stmtListNode = vec_first(n->children);
    if (stmtListNode->children->length > 0) {
        char *bufFmt = "%s(block\n";
        buf = calloc(strlen(indent)+1+7, 1);
        ASSERT_MEM(buf);
        sprintf(buf, bufFmt, indent);
        char *stmtListOutput = outputASTString(stmtListNode, indentLevel+1);
        buf = strAdd(buf, stmtListOutput);
        char *endFmt = "%s)\n";
        char *endBuf = calloc(strlen(indent)+1+2, 1);
        ASSERT_MEM(endBuf);
        sprintf(endBuf, endFmt, indent);
        buf = strAdd(buf, endBuf); // TODO: add indent
    } else {
        char *bufFmt = "%s(block)\n";
        buf = calloc(strlen(indent)+1+8, 1);
        ASSERT_MEM(buf);
        sprintf(buf, bufFmt, indent);
    }
    return buf;
}

static char *outputIfStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    Node *cond = vec_first(n->children);
    char *condOutput = outputASTString(cond, indentLevel);
    char *startFmt =  "%s(if %s\n";
    char *buf = calloc(strlen(condOutput)+1+strlen(indent)+5, 1);
    ASSERT_MEM(buf);
    sprintf(buf, startFmt, indent, condOutput);
    buf = strAdd(buf, outputASTString(n->children->data[1], indentLevel+1));
    // has else
    if (n->children->length > 2) {
        char *elseFmt = "%s(else\n";
        char *elseBuf = calloc(strlen(indent)+1+6, 1);
        ASSERT_MEM(elseBuf);
        sprintf(elseBuf, elseFmt, indent);
        buf = strAdd(buf, elseBuf);
        buf = strAdd(buf, outputASTString(n->children->data[2], indentLevel+1));
        buf = strAdd(buf, ")\n"); // TODO: add indent
    }
    return buf;
}

static char *outputWhileStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    char *startFmt = "%s(while %s\n";
    Node *cond = vec_first(n->children);
    char *condOutput = outputASTString(cond, indentLevel);
    char *buf = calloc(strlen(indent)+1+8+strlen(condOutput), 1);
    ASSERT_MEM(buf);
    sprintf(buf, startFmt, indent, condOutput);
    Node *body = n->children->data[1];
    buf = strAdd(buf, outputASTString(body, indentLevel+1));
    buf = strAdd(buf, ")\n"); // TODO: add indent
    return buf;
}

static char *outputForStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    Node *init = vec_first(n->children);
    Node *test = n->children->data[1];
    Node *incr = n->children->data[2];
    char *initOutput = "nil";
    char *testOutput = "true";
    char *incrOutput = "nil";
    if (init != NULL) {
        initOutput = outputASTString(init, indentLevel);
    }
    if (test != NULL) {
        testOutput = outputASTString(test, indentLevel);
    }
    if (incr != NULL) {
        incrOutput = outputASTString(incr, indentLevel);
    }
    char *startFmt = "%s(for %s %s %s\n";
    char *buf = calloc(strlen(indent)+1+strlen(initOutput)+
            strlen(testOutput)+strlen(incrOutput)+8, 1);
    ASSERT_MEM(buf);
    sprintf(buf, startFmt, indent, initOutput, testOutput, incrOutput);
    Node *blockNode = n->children->data[3];
    buf = strAdd(buf, outputASTString(blockNode, indentLevel+1));
    buf = strAdd(buf, ")\n"); // TODO: add indent
    return buf;
}

static char *outputForeachStmt(Node *n, int indentLevel) {
    return ""; // TODO
}

static char *outputContinueStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    char *startFmt = "%s(continue)\n";
    char *buf = calloc(strlen(indent)+1+11, 1);
    ASSERT_MEM(buf);
    sprintf(buf, startFmt, indent);
    return buf;
}

static char *outputBreakStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    char *startFmt = "%s(break)\n";
    char *buf = calloc(strlen(indent)+1+8, 1);
    ASSERT_MEM(buf);
    sprintf(buf, startFmt, indent);
    return buf;
}

// fn decl
static char *outputFunctionStmt(Node *n, int indentLevel) {
    char *buf;
    if (nodeKind(n) == FUNCTION_STMT) {
        char *indent = i(indentLevel);
        char *startFmt = "%s(fnDecl ";
        buf = calloc(strlen(indent)+1+8, 1);
        ASSERT_MEM(buf);
        sprintf(buf, startFmt, indent);
        Token tokName = n->tok;
        char *name = tokStr(&tokName);
        buf = strAdd(buf, name);
    } else if (nodeKind(n) == ANON_FN_EXPR) {
        buf = "(fnAnon";
    } else {
        UNREACHABLE("node kind: %d", nodeKind(n));
    }

    // parameters
    buf = strAdd(buf, " (");
    vec_nodep_t *params = (vec_nodep_t*)n->data;
    ASSERT_MEM(params);
    Node *param = NULL; int j = 0;
    int len = params->length;
    vec_foreach(params, param, j) {
        buf = strAdd(buf, tokStr(&param->tok));
        if (j != len-1) {
            buf = strAdd(buf, " ");
        }
    }
    buf = strAdd(buf, ")\n"); // end of params

    Node *blockStmt = vec_first(n->children);
    buf = strAdd(buf, outputASTString(blockStmt, indentLevel+1));

    buf = strAdd(buf, strAdd(i(indentLevel), ")\n"));
    return buf;
}

static char *outputReturnStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    char *fmt = "%s(return";
    char *buf = calloc(strlen(indent)+1+7, 1);
    ASSERT_MEM(buf);
    sprintf(buf, fmt, indent);
    if (n->children->length > 0) {
        Node *expr = vec_first(n->children);
        buf = strAdd(buf, " ");
        buf = strAdd(buf, outputASTString(expr, indentLevel));
    }
    buf = strAdd(buf, ")\n"); // TODO: add indent
    return buf;
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
    return ""; // TODO
}

static char *outputTryStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    char *startFmt = "%s(try\n";
    char *buf = calloc(strlen(indent)+1+5, 1);
    ASSERT_MEM(buf);
    sprintf(buf, startFmt, indent);
    Node *tryBlk = vec_first(n->children);
    buf = strAdd(buf, outputASTString(tryBlk, indentLevel+1));
    if (n->children->length > 1) {
        Node *catchStmt = NULL;
        int i = 0;
        vec_foreach(n->children, catchStmt, i) {
            if (i == 0) continue;
            buf = strAdd(buf, outputASTString(catchStmt, indentLevel));
        }
    }
    buf = strAdd(buf, ")\n"); // TODO: indent
    return buf;
}

// NOTE: 2 or 3 children, depending on if variableexpr (child 2) is given
static char *outputCatchStmt(Node *n, int indentLevel) {
    char *indent = i(indentLevel);
    Node *catchExpr = vec_first(n->children);
    char *catchExprOut = outputASTString(catchExpr, indentLevel);
    char *catchVarOut = "";
    bool catchVarGiven = n->children->length > 2;
    if (catchVarGiven) {
        catchVarOut = outputASTString(n->children->data[1], indentLevel);
        catchVarOut = strAdd(" ", catchVarOut);
    }
    char *startFmt = "%s(catch %s%s\n";
    char *buf = calloc(strlen(indent)+1+strlen(catchExprOut)+
            strlen(catchVarOut)+8, 1);
    ASSERT_MEM(buf);
    sprintf(buf, startFmt, indent, catchExprOut, catchVarOut);
    Node *block = vec_last(n->children);
    buf = strAdd(buf, outputASTString(block, indentLevel+1));
    buf = strAdd(buf, ")\n"); // TODO: indent
    return buf;
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
    char *pre = "";
    if (astDetailLevel > 1) {
        pre = strAdd(i(indentLevel), "(stmtList\n");
    }
    char *post = "";
    if (astDetailLevel > 1) {
        post = strAdd(i(indentLevel), ")\n");
        indentLevel++;
    }
    char *buf = "";
    int i = 0;
    Node *child = NULL;
    vec_foreach(n->children, child, i) {
        char *childBuf = outputASTString(child, indentLevel);
        buf = strAdd(buf, childBuf);
    }
    if (astDetailLevel > 1) {
        buf = strAdd(pre, buf);
        buf = strAdd(buf, post);
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
                    UNREACHABLE("invalid expr node kind: %d", node->type.kind);
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
                    UNREACHABLE("invalid stmt node kind: %d", node->type.kind);
            }
        }
        case NODE_OTHER: {
            UNREACHABLE("%s", "node type: other");
        }
    }
    UNREACHABLE("%s", "invalid node type");
}

NodeType nodeType(Node *n) {
    return n->type.type;
}
int nodeKind(Node *n) {
    return n->type.kind;
}
