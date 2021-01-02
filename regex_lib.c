#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include "regex_lib.h"
#include "debug.h"
#include "memory.h"
#include "options.h"

#define DEFAULT_REGEX_OPTIONS (RegexOptions){\
    .case_insensitive = false,\
    .multiline = false\
}

#ifdef NDEBUG
#define regex_debug(lvl, ...) (void)0
#else
static void regex_debug(int lvl, const char *format, ...) {
    if (GET_OPTION(debugRegexLvl) < lvl) return;
    va_list ap;
    va_start(ap, format);
    fprintf(stderr, "[Regex]: ");
    vfprintf(stderr, format, ap);
    va_end(ap);
    fprintf(stderr, "\n");

}
#endif

void regex_init(Regex *regex, const char *src, RegexOptions *opts) {
    regex->node = NULL;
    regex->src = strdup(src);
    regex->ownsSrc = true;
    regex->groups = NULL;
    if (opts) {
        regex->opts = *opts;
    } else {
        regex->opts = DEFAULT_REGEX_OPTIONS;
    }
}

void regex_init_from(Regex *regex, const char *src, RegexOptions *opts) {
    regex->node = NULL;
    regex->src = src;
    regex->ownsSrc = false;
    regex->groups = NULL;
    if (opts) {
        regex->opts = *opts;
    } else {
        regex->opts = DEFAULT_REGEX_OPTIONS;
    }
}

void regex_free(Regex *regex) {
    regex->node = NULL; // TODO: free nodes
    if (regex->ownsSrc) {
        xfree((void*)regex->src);
    }
    regex->src = NULL;
}

static void node_add_child(RNode *parent, RNode *child) {
    ASSERT(parent);
    ASSERT(child);
    RNode *last_child = parent->children;
    child->parent = parent;
    if (last_child == NULL) {
        parent->children = child;
        return;
    }
    while (last_child) {
        if (last_child->next) {
            last_child = last_child->next;
        } else {
            break;
        }
    }
    last_child->next = child;
    child->prev = last_child;
}

static void node_remove_child(RNode *parent, RNode *child) {
    ASSERT(parent);
    ASSERT(child);
    RNode *childn = parent->children;
    child->parent = NULL;
    if (childn == NULL) {
        return;
    }
    while (childn) {
        if (childn == child) {
            if (child->prev) {
                child->prev->next = child->next;
            }
            if (child->next) {
                child->next->prev = child->prev;
            }
            if (!child->prev) {
                parent->children = child->next;
            }
            return;
        }
        if (childn->next) {
            childn = childn->next;
        } else {
            break;
        }
    }
    // node not found
    return;
}

static RNode *begOrNode = NULL; // beginning of what could be the child of an OR (|) node later on

static RNode *new_node(RNodeType type, const char *tok, int toklen, RNode *parent, RNode *prev) {
    RNode *node = ALLOCATE(RNode, 1);
    memset(node, 0, sizeof(*node));
    node->type = type;
    node->tok = tok;
    node->toklen = toklen;
    node->nodelen = toklen;
    node->parent = parent;
    node->repeat_min = -1;
    node->repeat_max = -1;
    node->eclass_type = ECLASS_NONE;
    node->anchor_type = ANCHOR_NONE;
    node->capture_beg = NULL;
    node->capture_end = NULL;
    if (parent) {
        parent->nodelen += toklen;
    }
    node->prev = prev;
    if (prev) {
        prev->next = node;
        node->prev = prev;
    }
    node->next = NULL;
    if (parent && !prev) {
        node_add_child(parent, node);
    }
    return node;
}

static void parse_error(char c, const char *at, const char *msg) {
    regex_debug(1, "%s", msg);
}

static void regex_output_ast_node(RNode *node, RNode *parent, int indent);

static void regex_add_group(Regex *regex, RNode *group) {
    GroupNode *gn = ALLOCATE(GroupNode, 1);
    gn->group = group;
    gn->next = NULL;
    GroupNode *cur = regex->groups;
    if (!cur) {
        regex->groups = gn;
        return;
    }
    while (cur->next) {
        cur = cur->next;
    }
    cur->next = gn;
}

static void regex_blank_out_group_captures(Regex *regex) {
    GroupNode *gn = regex->groups;
    while (gn) {
        gn->group->capture_beg = NULL;
        gn->group->capture_end = NULL;
        gn = gn->next;
    }
}

static RNode *regex_parse_node(Regex *regex, RNode *parent, RNode *prev, char **src_p, int *err) {
    if (*err != 0) return NULL; // for recursive calls
    char c;
    while ((c = **src_p)) {
         if (c == '^' || c == '$') {
            RNode *anch = new_node(NODE_ANCHOR, *src_p, 1, parent, prev);
            if (c == '^') {
              anch->anchor_type = ANCHOR_BOL;
            } else {
              anch->anchor_type = ANCHOR_EOL;
            }
            (*src_p)++;
            return anch;
         } else if (c == '(') {
            RNode *grp = new_node(NODE_GROUP, *src_p, 1, parent, prev);
            (*src_p)++;
            RNode *grp_child = NULL;
            while (**src_p && **src_p != ')') {
                /*fprintf(stderr, "parsing %c\n", **src_p);*/
                RNode *grp_child_old = grp_child;
                grp_child = regex_parse_node(regex, grp, grp_child, src_p, err);
                if (!grp_child_old) {
                    begOrNode = grp_child;
                }
            }
            if (*err != 0) {
                fprintf(stderr, "Error parsing NODE_GROUP\n");
                return NULL;
            }
            if (**src_p) {
                ASSERT(**src_p == ')');
                (*src_p)++; // ')'
                regex_add_group(regex, grp);
                return grp;
            } else {
                *err = -1;
                parse_error(c, "Unmatched '(", (*src_p)-1);
                return NULL;
            }
        } else if (c == ')') {
            ASSERT(0); // FIXME: ')', unmatched '(', should be parse error
        } else if (c == '|') {
            if (!begOrNode) {
                parse_error(c, *src_p, "Empty alternate");
                *err = -1;
                return NULL;
            }
            node_remove_child(parent, begOrNode);
            begOrNode->parent = NULL;
            RNode *orNode = new_node(NODE_OR, *src_p, 1, parent, begOrNode->prev);
            RNode *groupNode = new_node(NODE_GROUP, NULL, 0, orNode, NULL);
            begOrNode->parent = groupNode;

            if (!begOrNode->prev) {
                parent->children = orNode;
            } else {
                begOrNode->prev->next = orNode;
                orNode->prev = begOrNode->prev;
            }

            groupNode->parent = orNode;
            orNode->children = groupNode;

            groupNode->children = begOrNode;
            RNode *beg = begOrNode;
            node_remove_child(groupNode, orNode);
            ASSERT(beg != orNode);
            int idx = 0;
            while (beg) {
                ASSERT(beg != orNode);
                beg->parent = groupNode;
                beg = beg->next;
                idx++;
            }

            /*fprintf(stderr, "groupNode:==\n");*/
            /*regex_output_ast_node(groupNode, 0);*/
            /*fprintf(stderr, "/groupNode:==\n");*/
            (*src_p)++;
            RNode *lastBegOrNode = begOrNode;
            int lastIdx = 0;
            RNode *alt = NULL;
            RNode *altGroupNode = new_node(NODE_GROUP, NULL, 0, orNode, groupNode);

            while (**src_p) {
                if (**src_p == ')') break; // end of group, don't advance. TODO: check if inGroupLvl > 0
                alt = regex_parse_node(regex, altGroupNode, alt, src_p, err);
                if (*err != 0) return NULL;
                if (begOrNode != lastBegOrNode && lastIdx > 0) break;
                lastIdx++;
            }

            if (alt == NULL) {
                ASSERT(0);
                parse_error(c, *src_p, "Alternate must have 2 choices");
                *err = -1;
                return NULL;
            }

            /*fprintf(stderr, "node:==\n");*/
            /*regex_output_ast_node(orNode, 0);*/
            /*fprintf(stderr, "/node:==\n");*/
            begOrNode = orNode;
            return orNode;
        } else if (c == '+') {
            if (!prev) {
                parse_error(c, *src_p, "Empty + repeat");
                *err = -1;
                return NULL;
            }
            (*src_p)++;
            RNode *repeat = new_node(NODE_REPEAT, *src_p, 1, parent, prev->prev);
            if (prev->prev == NULL) {
                parent->children = repeat;
            }
            prev->next = NULL;
            prev->prev = NULL;
            node_add_child(repeat, prev);
            return repeat;
        } else if (c == '*') {
            if (!prev) {
                parse_error(c, *src_p, "Empty * repeat");
                *err = -1;
                return NULL;
            }
            (*src_p)++;
            RNode *repeat = new_node(NODE_REPEAT_Z, *src_p, 1, parent, prev->prev);
            if (prev->prev == NULL) {
                parent->children = repeat;
            }
            prev->next = NULL;
            prev->prev = NULL;
            node_add_child(repeat, prev);
            return repeat;
        } else if (c == '?') {
            if (!prev) {
                parse_error(c, *src_p, "Need atom before '?'");
                *err = -1;
                return NULL;
            }
            (*src_p)++;
            RNode *maybe = new_node(NODE_MAYBE, *src_p, 1, parent, prev->prev);
            if (prev->prev == NULL) {
                parent->children = maybe;
            }
            prev->next = NULL;
            prev->prev = NULL;
            node_add_child(maybe, prev);
            return maybe;
        } else if (c == '{') {
            regex_debug(2, "parsing repeat-n");
            if (!prev) {
                parse_error(c, *src_p, "Empty {} repeat");
                *err = -1;
                return NULL;
            }
            (*src_p)++;
            if (!*src_p) {
                parse_error(c, *(src_p-1), "Empty {} repeat");
                *err =  -1;
                return NULL;
            }
            char *endptr = NULL;
            errno = 0;
            long lval1 = strtol(*src_p, &endptr, 10);
            if ((errno == ERANGE && (lval1 == LONG_MAX || lval1 == LONG_MIN))
                    || (errno != 0 && lval1 == 0) || *endptr == '\0') {
                parse_error(c, *src_p, "Invalid {} repeat number, first number");
                *err = -1;
                return NULL;
            }
            *src_p = endptr;
            long lval2 = lval1;
            if (**src_p != '}') {
                if (**src_p != ',') {
                    parse_error(c, *src_p, "Invalid {} repeat number, expected ',' after first number");
                    *err = -1;
                    return NULL;
                }
                (*src_p)++; // ','
                endptr = NULL;
                errno = 0;
                lval2 = strtol(*src_p, &endptr, 10);
                if ((errno == ERANGE && (lval2 == LONG_MAX || lval2 == LONG_MIN))
                        || (errno != 0 && lval2 == 0) || lval2 < lval1 || *endptr == '\0') {
                    parse_error(c, *src_p, "Invalid {} repeat number, second number");
                    *err = -1;
                    return NULL;
                }
                *src_p = endptr;
            }
            (*src_p)++; // '}'
            RNode *repeat = new_node(NODE_REPEAT_N, *src_p, 1, parent, prev->prev);
            repeat->repeat_min = lval1;
            repeat->repeat_max = lval2;
            if (prev->prev == NULL) {
                parent->children = repeat;
            }
            prev->next = NULL;
            prev->prev = NULL;
            node_add_child(repeat, prev);
            return repeat;
        } else if (c == '[') { // character class, ex: [aeiou]
            RNode *cclass = new_node(NODE_CCLASS, *src_p, 1, parent, prev);
            (*src_p)++; // '['
            char lastc = **src_p;
            char *cclass_start = *src_p;
            while (**src_p && (lastc == '\\' || **src_p != ']')) {
                cclass->toklen++;
                lastc = **src_p;
                (*src_p)++;
            }
            if (!**src_p) {
                parse_error('[', cclass_start, "Unterminated character class");
                *err = -1;
                return NULL;
            }
            (*src_p)++; // ']'
            return cclass;
        } else if (c == '.') {
            RNode *dot = new_node(NODE_DOT, *src_p, 1, parent, prev);
            (*src_p)++;
            return dot;
        } else if (c == '\\') {
            (*src_p)++;
            c = **src_p;
            if (c == '\0') {
                parse_error('\\', *(src_p-1), "invalid escape sequence");
                *err = -1;
                return NULL;
            }
            RNode *node = NULL;
            char cl = tolower(c);
            if (cl == 'w' || cl == 'd' || cl == 's' || cl == 'b') {
                node = new_node(NODE_ECLASS, *src_p, 1, parent, prev);
                REClassType etype = ECLASS_NONE;
                switch (cl) {
                    case 'w':
                        etype = ECLASS_WORD;
                        break;
                    case 'd':
                        etype = ECLASS_DIGIT;
                        break;
                    case 's':
                        etype = ECLASS_SPACE;
                        break;
                    case 'b':
                        etype = ECLASS_WORD_BOUNDARY;
                        break;
                    default:
                        UNREACHABLE("bug");
                }
                if (isupper(c)) {
                    int eOld = (int)etype;
                    etype = (REClassType)eOld+1;
                }
                node->eclass_type = etype;
            } else if (c == '.' || c == '*' || c == '+' || c == '[' || c == '|' ||
                    c == '?' || c == '{' || c == '}' || c == '^' || c == '$') {
                node = new_node(NODE_ATOM, *src_p, 1, parent, prev);
            } else if (c == 'A' || c == 'Z') {
                node = new_node(NODE_ANCHOR, *src_p, 1, parent, prev);
                if (c == 'A') {
                    node->anchor_type = ANCHOR_BOS;
                } else if (c == 'Z') {
                    node->anchor_type = ANCHOR_EOS;
                }
            } else {
                // FIXME: could be a non-atom...
                (*src_p)++;
                node = new_node(NODE_ATOM, *src_p, 1, parent, prev);
                return node;
            }
            (*src_p)++;
            return node;
        } else { // atom
            RNode *atom = new_node(NODE_ATOM, *src_p, 1, parent, prev);
            (*src_p)++;
            return atom;
        }
    }
    ASSERT(0);
    return NULL;
}

static int regex_parse(Regex *regex) {
    RNode *program = new_node(NODE_PROGRAM, NULL, 0, NULL, NULL);
    regex->node = program;
    RNode *parent = program;

    begOrNode = NULL;
    RNode *prev = NULL;
    char *src_p = (char*)regex->src;
    char **src_pout = &src_p;
    while (*src_p) {
        int err = 0;
        prev = regex_parse_node(regex, parent, prev, src_pout, &err);
        if (err != 0) { return err; }
        if (!begOrNode) {
            begOrNode = prev;
        }
    }
    return 0;
}

RegexCompileResult regex_compile(Regex *regex) {
    if (!regex->src) {
       return REGEX_UNITIALIZED_ERR;
    }
    int parse_res = regex_parse(regex);
    if (parse_res != 0) {
       return REGEX_PARSE_ERR;
    }
    ASSERT(regex->node->type == NODE_PROGRAM);
    return REGEX_COMPILE_SUCCESS;
}

static char *i(int indent) {
    if (indent == 0) return (char*)"";
    char *buf = malloc((indent*2)+1);
    ASSERT_MEM(buf);
    memset(buf, ' ', indent*2);
    buf[indent*2] = '\0';
    return buf;
}

static void regex_output_ast_node(RNode *node, RNode *parent, int indent) {
    switch (node->type) {
        case NODE_PROGRAM: {
            fprintf(stderr, "(program");
            RNode *child = node->children;
            if (child == NULL) {
                fprintf(stderr, ")\n");
            } else {
                fprintf(stderr, "\n");
            }
            while (child) {
                regex_output_ast_node(child, node, indent+1);
                child = child->next;
            }
            fprintf(stderr, ")\n");
            break;
        }
        case NODE_ATOM: {
            fprintf(stderr, "%s(atom %.*s)", i(indent), 1, node->tok);
            fprintf(stderr, "  parent = %s, next = %s\n", nodeTypeName(parent->type),
                node->next == NULL ? "NULL" : nodeTypeName(node->next->type));
            break;
        }
        case NODE_OR: {
            fprintf(stderr, "%s(alt\n", i(indent));
            RNode *child = node->children;
            while (child) {
                regex_output_ast_node(child, node, indent+1);
                child = child->next;
            }
            fprintf(stderr, "%s)\n", i(indent));
            break;
        }
        case NODE_MAYBE: {
            fprintf(stderr, "%s(maybe\n", i(indent));
            RNode *child = node->children;
            while (child) {
                regex_output_ast_node(child, node, indent+1);
                child = child->next;
            }
            fprintf(stderr, "%s)\n", i(indent));
            break;
        }
        case NODE_GROUP: {
            fprintf(stderr, "%s(group\n", i(indent));
            RNode *child = node->children;
            while (child) {
                regex_output_ast_node(child, node, indent+1);
                child = child->next;
            }
            fprintf(stderr, "%s)\n", i(indent));
            break;
        }
        case NODE_REPEAT:
        case NODE_REPEAT_Z:
        case NODE_REPEAT_N: {
            fprintf(stderr, "%s%s", i(indent),
                    node->type == NODE_REPEAT ? "(repeat\n" :
                    (node->type == NODE_REPEAT_Z ? "(repeat-z\n" :
                     "(repeat-n "));
            if (node->type == NODE_REPEAT_N) {
                fprintf(stderr, "%ld-%ld\n", node->repeat_min, node->repeat_max);
            }
            RNode *child = node->children;
            while (child) {
                regex_output_ast_node(child, node, indent+1);
                child = child->next;
            }
            fprintf(stderr, "%s)\n", i(indent));
            break;
        }
        case NODE_CCLASS: {
            fprintf(stderr, "%s(cclass\n", i(indent));
            fprintf(stderr, "%s[%.*s]\n", i(indent+1), node->toklen-1, node->tok+1);
            fprintf(stderr, "%s)\n", i(indent));
            break;
        }
        case NODE_ECLASS: {
            fprintf(stderr, "%s(eclass ", i(indent));
            fprintf(stderr, "%s)\n", node->eclass_type == ECLASS_DIGIT ? "\\d" :
                    (node->eclass_type == ECLASS_WORD ? "\\w" : "\\s"));
            break;
        }
        case NODE_DOT: {
            fprintf(stderr, "%s(dot)\n", i(indent));
            break;
        }
        case NODE_ANCHOR: {
            fprintf(stderr, "%s(anchor %c)\n", i(indent), node->tok[0]);
            break;
        }
        default:
            ASSERT(0);
    }
}

void regex_output_ast(Regex *regex) {
    if (regex->node == NULL) { fprintf(stderr, "(uninitialized)\n"); }
    regex_output_ast_node(regex->node, NULL, 0);
}

static RNode *GET_NEXT_NODE(RNode *node) {
    ASSERT(node);
    if (node->next) {
        return node->next;
    } else {
        return node->parent ? node->parent->next : NULL;
    }
}

static RNode *SET_NEXT_NODE(RNode *node, char **cptr_p) {
    ASSERT(node);
    if (node->next) {
        return node->next;
    } else {
        if (node->parent->type == NODE_GROUP) {
            node->parent->capture_end = *cptr_p;
        }
    }
    return node->parent->next;
}

static bool regex_part_match_beg(RNode *node, const char *string);

static bool node_accepts_ch(RNode *node, RNode *parent, char **cptr_p, RNode **nnext) {
    DBG_ASSERT(node);
    DBG_ASSERT(*cptr_p);
    if (**cptr_p == '\0') {
        return false;
    }
    switch (node->type) {
        case NODE_PROGRAM:
        case NODE_GROUP:
            ASSERT(node->children);
            char *start = *cptr_p;
            if (node_accepts_ch(node->children, node, cptr_p, nnext)) {
                if (node->type == NODE_GROUP) {
                    node->capture_beg = start;
                }
                return true;
            } else {
                return false;
            }
        case NODE_ANCHOR: {
            if (node->anchor_type == ANCHOR_EOL) { // end of line (or string)
                if (!**cptr_p || **cptr_p == '\n' || **cptr_p == '\r') {
                    *nnext = node->next; // NULL
                    return true;
                } else {
                    return false;
                }
            } else if (node->anchor_type == ANCHOR_BOL) { // ^
                *nnext = node->next;
                return true;
            } else if (node->anchor_type == ANCHOR_EOS) { // \Z
              if (!**cptr_p) {
                *nnext = node->next; // NULL
                return true;
              } else {
                return false;
              }
            } else { // \A
              *nnext = node->next;
              return true;
            }
            break;
        }
        case NODE_ATOM:
            ASSERT(node->tok);
            if (node->tok[0] == **cptr_p) {
                (*cptr_p)++;
                *nnext = SET_NEXT_NODE(node, cptr_p);
                return true;
            } else {
                return false;
            }
        case NODE_OR: { // | (2 children)
            ASSERT(node->children);
            RNode *child = node->children;
            RNode *child2 = node->children->next;
            char *start = *cptr_p;
            bool accepted = false;
            while ((accepted = node_accepts_ch(child, node, cptr_p, nnext))) {
                /*fprintf(stderr, "OR accepted\n");*/
                if (!*nnext) break;
                child = *nnext;
                if (child) {
                    *nnext = child->next;
                } else {
                    *nnext = NULL;
                }
            }
            if (accepted) {
                /*fprintf(stderr, "OR success (1)\n");*/
                return true;
            }
            /*fprintf(stderr, "OR reset\n");*/
            *cptr_p = start;
            while ((accepted = node_accepts_ch(child2, node, cptr_p, nnext))) {
                /*fprintf(stderr, "OR accepted (2)\n");*/
                if (!*nnext) break;
                child2 = *nnext;
                if (child2) {
                    *nnext = child2->next;
                } else {
                    *nnext = NULL;
                }
            }
            if (accepted) {
                /*fprintf(stderr, "OR success (2)\n");*/
                return true;
            } else {
                *cptr_p = start;
                /*fprintf(stderr, "OR failed\n");*/
                return false;
            }

        }
        case NODE_REPEAT: { // +
            int count = 0;
            RNode *child = node->children;
            RNode *parent = node;
            RNode *next = GET_NEXT_NODE(node);
            char *before = *cptr_p;
            char *biggest_pre_next_match = NULL;
            while (node_accepts_ch(child, parent, cptr_p, nnext)) {
                if (next) {
                    regex_debug(1, "has next");
                    if (regex_part_match_beg(next, *cptr_p)) {
                        biggest_pre_next_match = *cptr_p;
                        regex_debug(1, "matched next: '%s'", biggest_pre_next_match);
                    }
                }
                count += 1;
            }
            if (count >= 1) {
                if (next && !biggest_pre_next_match) {
                    *cptr_p = before;
                    regex_debug(1, "resetting cptr_p: '%s'", *cptr_p);
                    return false;
                } else if (next && biggest_pre_next_match) {
                    *cptr_p = biggest_pre_next_match;
                    regex_debug(1, "setting cptr_p: '%s'", *cptr_p);
                }
                char *after = *cptr_p;
                regex_debug(1, "Matched REPEAT (+) for '%.*s'", after-before, before);
                *nnext = SET_NEXT_NODE(node, cptr_p);
                return true;
            } else {
                return false;
            }
        }
        case NODE_REPEAT_Z: {
            RNode *child = node->children;
            RNode *parent = node;
            RNode *next = GET_NEXT_NODE(node); // after *
            char *before = *cptr_p;
            char *biggest_pre_next_match = NULL;
            while (node_accepts_ch(child, parent, cptr_p, nnext)) {
              if (next) {
                  regex_debug(1, "has next");
                  if (regex_part_match_beg(next, *cptr_p)) {
                      biggest_pre_next_match = *cptr_p;
                      regex_debug(1, "matched next: '%s'", biggest_pre_next_match);
                  }
              }
            }
            if (next && !biggest_pre_next_match) { // matched nothing
                *cptr_p = before;
                regex_debug(1, "resetting cptr_p: '%s'", *cptr_p);
            } else if (next && biggest_pre_next_match) {
                *cptr_p = biggest_pre_next_match;
                regex_debug(1, "setting cptr_p: '%s'", *cptr_p);
            }
            char *after = *cptr_p;
            regex_debug(1, "Matched REPEAT_Z (*) for '%.*s'", after-before, before);
            *nnext = SET_NEXT_NODE(node, cptr_p);
            return true;
        }
        case NODE_REPEAT_N: {
            RNode *child = node->children;
            RNode *parent = node;
            char *start = *cptr_p;
            int matched = 0;
            bool accepted = false;
            while (node_accepts_ch(child, parent, cptr_p, nnext)) {
                matched++;
                if (matched >= node->repeat_min) {
                    accepted = true;
                    if (matched == node->repeat_max) {
                        break;
                    }
                }
            }
            if (accepted) {
                *nnext = SET_NEXT_NODE(node, cptr_p);
            } else {
                *cptr_p = start;
            }
            return accepted;
        }
        case NODE_MAYBE: {
            RNode *child = node->children;
            RNode *parent = node;
            if (node_accepts_ch(child, parent, cptr_p, nnext)) {
            }
            *nnext = SET_NEXT_NODE(node, cptr_p);
            return true;
        }
        case NODE_CCLASS: {
            char *start = (char*)node->tok+1;
            int len = node->toklen-1;
            while (len > 0) {
                char c = **cptr_p;
                char rc = *start;
                if (len > 2) {
                    char rnext = *(start+1);
                    char rend;
                    // range found
                    if (rnext == '-' && (rend = *(start+2))) {
                        if (c >= rc && c <= rend) {
                            (*cptr_p)++;
                            *nnext = SET_NEXT_NODE(node, cptr_p);
                            return true;
                        }
                        // skip over range chars
                        start += 3;
                        len -= 3;
                        continue;
                    }
                }
                // escape sequence in character class
                if (rc == '\\' && len > 1) {
                    char rnext = *(start+1);
                    bool accepted = false;
                    if (rnext == 'd' || rnext == 's' || rnext == 'w') {
                        switch (rnext) {
                            case 'd':
                                accepted = isdigit(c);
                                break;
                            case 's':
                                accepted = isspace(c);
                                break;
                            case 'w':
                                accepted = isalnum(c) || c == '_';
                                break;
                            default:
                                UNREACHABLE("bug");
                        }
                        if (accepted) {
                            (*cptr_p)++;
                            node = SET_NEXT_NODE(node, cptr_p);
                            return true;
                        } else {
                            start += 2;
                            len -= 2;
                            continue;
                        }
                    }
                }
                if (rc == c) {
                    (*cptr_p)++;
                    *nnext = SET_NEXT_NODE(node, cptr_p);
                    return true;
                }
                start++;
                len--;
            }
            return false;
        }
        case NODE_ECLASS: {
            switch (node->eclass_type) {
                case ECLASS_DIGIT: {
                    if (isdigit(**cptr_p)) {
                        (*cptr_p)++;
                        *nnext = SET_NEXT_NODE(node, cptr_p);
                        return true;
                    }
                    return false;
                }
                case ECLASS_WORD: {
                    if (isalpha(**cptr_p) || **cptr_p == '_') {
                        (*cptr_p)++;
                        *nnext = SET_NEXT_NODE(node, cptr_p);
                        return true;
                    }
                    return false;
                }
                case ECLASS_SPACE: {
                    if (isspace(**cptr_p)) {
                        (*cptr_p)++;
                        *nnext = SET_NEXT_NODE(node, cptr_p);
                        return true;
                    }
                    return false;
                }
                default: {
                    UNREACHABLE("eclass type");
                }
            }
        }
        case NODE_DOT: { // matches any char
            regex_debug(1, "Matched DOT at %c", **cptr_p);
            (*cptr_p)++;
            *nnext = SET_NEXT_NODE(node, cptr_p);
            return true;
        }
        default:
            UNREACHABLE("node type");
            return false;
    }
}

static RNode *insert_bol_anchor(RNode *prev) {
    RNode *anch = new_node(NODE_ANCHOR, "^", 1, NULL, NULL);
    anch->next = prev;
    prev->prev = anch;
    return anch;
}

static RNode *dup_node(RNode *old) {
    RNode *node = ALLOCATE(RNode, 1);
    memcpy(node, old, sizeof(*node));
    return node;
}

static RNode *new_program_node() {
    return new_node(NODE_PROGRAM, NULL, 0, NULL, NULL);
}

static bool regex_part_match_beg(RNode *node, const char *string) {
    Regex re;
    regex_init_from(&re, string, NULL); /* HACK: this regex has no source pattern, just use match pattern as the source pattern */
    RNode *beg = dup_node(node);
    RNode *bol = insert_bol_anchor(beg);
    RNode *program = new_program_node();
    bol->parent = program;
    beg->parent = program;
    program->children = bol;
    re.node = program;
    MatchData md = regex_match(&re, string);
    if (md.matched && md.match_start == 0 && md.match_len > 0) {
        /*regex_output_ast_node(program, NULL, 0);*/
        regex_debug(1, "regex_part_match matched for string: '%s'", string);
        regex_debug(1, "regex_part_match matched %d %*.s", md.match_len,
                md.match_len, string);
        return true;
    }
    return false;
}

MatchData regex_match(Regex *regex, const char *string) {
    ASSERT(regex);
    ASSERT(string);
    MatchData mres = {
        .matched = false,
        .match_start = -1,
        .match_len = -1
    };
    if (!regex->node || !regex->src) { // uninitialized regex
        return mres;
    }
    regex_blank_out_group_captures(regex); // from previous matches, if any
    char *cptr = (char*)string;
    char *start = cptr;
    char **cptr_p = &cptr;
    RNode *node = regex->node;
    // empty regex: always matches at 0
    // empty string: always matches at 0
    if (strlen(cptr) == 0 || !node->children) {
        mres.matched = true;
        mres.match_start = 0;
        mres.match_len = 0;
        return mres;
    }
    RNode *nparent = node->parent;
    int begAnchorFound = 0;
    bool bosAnchor = false;
    bool bolAnchor = false;
    bool lastAccept = false;
    while (**cptr_p) {
        regex_debug(1, "matching '%c' at nodetype=%s", **cptr_p, nodeTypeName(node->type));
        RNode *nnext = NULL;
        char *match_start = *cptr_p;
        (void)match_start;
        if (begAnchorFound == 0) {
          if (node->type == NODE_PROGRAM && node->children && node->children->anchor_type == ANCHOR_BOS) {
            bosAnchor = true;
          }
          if (node->type == NODE_PROGRAM && node->children && node->children->anchor_type == ANCHOR_BOL) {
            bolAnchor = true;
          }
          begAnchorFound++;
        }
        if ((lastAccept = node_accepts_ch(node, nparent, cptr_p, &nnext))) {
            if (**cptr_p) {
                regex_debug(1, "matched '%c'", *match_start);
            } else {
                regex_debug(1, "matched");
            }
            node = nnext;
            if (node == NULL) { // successful match
                char *match_end = *cptr_p;
                regex_debug(1, "Successful match starting at %d", (int)(start - string));
                mres.matched = true;
                mres.match_start = start - string;
                mres.match_len = match_end - start;
                return mres;
            }
            nparent = node->parent;
        // No match on node
        } else if (bosAnchor) {
            return mres; // no match period if no first match
        } else {
            regex_debug(1, "no match for '%c'", **cptr_p);
            if (bolAnchor) {
              char beg = **cptr_p;
              if (beg == '\r' || beg == '\n') {
                  // continue
              } else {
                  // advance cursor until hits beginning of line
                  while (beg != '\r' && beg != '\n') {
                      (*cptr_p)++;
                      beg = **cptr_p;
                      if (!beg) {
                        return mres; // no match, end of string
                      }
                  }
              }
            }

            (*cptr_p)++;
            start = *cptr_p;
        }
    }

    if (lastAccept && node && (node->anchor_type == ANCHOR_EOL || node->anchor_type == ANCHOR_EOS)) {
        char *match_end = *cptr_p;
        if (node->anchor_type == ANCHOR_EOS && **cptr_p) {
            return mres; // no match
        }
        if (node->anchor_type == ANCHOR_EOL) {
            if (**cptr_p == '\n' || **cptr_p == '\r' || !**cptr_p) {
                // continue
            } else {
                return mres; // no match
            }
        }
        regex_debug(1, "Successful match starting at %d", (int)(start - string));
        mres.matched = true;
        mres.match_start = start - string;
        mres.match_len = match_end - start;
    }
    return mres;
}


const char *nodeTypeName(RNodeType nodeType) {
    switch (nodeType) {
        case NODE_ATOM:
            return "ATOM";
        case NODE_GROUP:
            return "GROUP";
        case NODE_OR:
            return "OR";
        case NODE_REPEAT:
            return "REPEAT";
        case NODE_REPEAT_Z:
            return "REPEAT_Z";
        case NODE_REPEAT_N:
            return "REPEAT_N";
        case NODE_CCLASS:
            return "CCLASS";
        case NODE_ECLASS:
            return "ECLASS";
        case NODE_ANCHOR:
            return "ANCHOR";
        case NODE_DOT:
            return "DOT";
        case NODE_PROGRAM:
            return "PROGRAM";
        default:
            return NULL;
    }
}
