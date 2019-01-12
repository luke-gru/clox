#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include "regex.h"
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
    if (opts) {
        regex->opts = *opts;
    } else {
        regex->opts = DEFAULT_REGEX_OPTIONS;
    }
}

void regex_free(Regex *regex) {
    regex->node = NULL; // TODO: free nodes
    xfree(regex->src);
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
}

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
    if (parent) {
        parent->nodelen += toklen;
    }
    node->prev = prev;
    if (prev) {
        prev->next = node;
        node->prev = prev;
    }
    node->next = NULL;
    if (parent && !prev) { node_add_child(parent, node); }
    return node;
}

static void parse_error(char c, const char *at, const char *msg) {
    regex_debug(1, "%s", msg);
}

static RNode *regex_parse_node(RNode *parent, RNode *prev, char **src_p, int *err) {
    if (*err != 0) return NULL; // for recursive calls
    char c;
    while ((c = **src_p)) {
        if (c == '(') {
            RNode *grp = new_node(NODE_GROUP, *src_p, 1, parent, prev);
            (*src_p)++;
            RNode *grp_child = NULL;
            while (**src_p && **src_p != ')') {
                /*fprintf(stderr, "parsing %c\n", **src_p);*/
                grp_child = regex_parse_node(grp, grp_child, src_p, err);
            }
            if (*err != 0) {
                return NULL;
            }
            if (**src_p) {
                ASSERT(**src_p == ')');
                (*src_p)++; // ')'
                return grp;
            } else {
                *err = -1;
                parse_error(c, "Unmatched '(", (*src_p)-1);
                return NULL;
            }
        } else if (c == ')') {
            ASSERT(0); // FIXME: ')', unmatched '(', should be parse error
        } else if (c == '|') {
            if (!prev) {
                parse_error(c, *src_p, "Empty alternate");
                *err = -1;
                return NULL;
            }
            prev->parent = NULL;
            RNode *node = new_node(NODE_OR, *src_p, 1, parent, prev->prev);
            (*src_p)++;
            prev->next = NULL;
            node_add_child(node, prev);
            RNode *alt = regex_parse_node(node, prev, src_p, err);
            if (*err != 0) return NULL;
            if (alt == NULL) {
                parse_error(c, *src_p, "Alternate must have 2 choices");
                *err = -1;
                return NULL;
            }
            return node;
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
                    etype += 1;
                }
                node->eclass_type = etype;
            } else if (c == '.' || c == '*' || c == '+' || c == '[' || c == '|' ||
                    c == '?' || c == '{' || c == '}') {
                node = new_node(NODE_ATOM, *src_p, 1, parent, prev);
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

    bool debug = false;

    RNode *prev = NULL;
    char *src_p = regex->src;
    char **src_pout = &src_p;
    while (*src_p) {
        int err = 0;
        prev = regex_parse_node(parent, prev, src_pout, &err);
        if (err != 0) { return err; }
        if (debug) {
            regex_output_ast(regex);
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

static const char *i(int indent) {
    if (indent == 0) return "";
    const char *buf = malloc((indent*2)+1);
    memset(buf, ' ', indent*2);
    ((char*)buf)[indent*2] = '\0';
    return buf;
}

static void regex_output_ast_node(RNode *node, int indent) {
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
                regex_output_ast_node(child, indent+1);
                child = child->next;
            }
            fprintf(stderr, ")\n");
            break;
        }
        case NODE_ATOM: {
            fprintf(stderr, "%s(atom %.*s)\n", i(indent), 1, node->tok);
            break;
        }
        case NODE_OR: {
            fprintf(stderr, "%s(alt\n", i(indent));
            RNode *child = node->children;
            while (child) {
                regex_output_ast_node(child, indent+1);
                child = child->next;
            }
            fprintf(stderr, "%s)\n", i(indent));
            break;
        }
        case NODE_GROUP: {
            fprintf(stderr, "%s(group\n", i(indent));
            RNode *child = node->children;
            while (child) {
                regex_output_ast_node(child, indent+1);
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
                regex_output_ast_node(child, indent+1);
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
        default:
            ASSERT(0);
    }
}

void regex_output_ast(Regex *regex) {
    if (regex->node == NULL) { fprintf(stderr, "(unitialized)\n"); }
    regex_output_ast_node(regex->node, 0);
}

bool node_accepts_ch(RNode *node, RNode *parent, char **cptr_p, RNode **nnext) {
    switch (node->type) {
        case NODE_PROGRAM:
        case NODE_GROUP:
            ASSERT(node->children);
            if (node_accepts_ch(node->children, node, cptr_p, nnext)) {
                if (node->children->next) {
                    *nnext = node->children->next;
                } else {
                    *nnext = node->next;
                }
                return true;
            } else {
                return false;
            }
        case NODE_ATOM:
            ASSERT(node->tok);
            if (node->tok[0] == **cptr_p) {
                if (node->next) {
                    *nnext = node->next;
                } else {
                    *nnext = parent->next;
                }
                (*cptr_p)++;
                return true;
            } else {
                return false;
            }
        case NODE_OR: { // |
            RNode *child = node->children;
            while (child) {
                if (node_accepts_ch(child, node, cptr_p, nnext)) {
                    if (node->next) {
                        *nnext = node->next;
                    } else {
                        *nnext = parent->next;
                    }
                    return true;
                }
                child = child->next;
                if (child) {
                    ASSERT(child->next == NULL);
                }
            }
            return false;
        }
        case NODE_REPEAT: { // +
            int count = 0;
            RNode *child = node->children;
            RNode *parent = node;
            while (node_accepts_ch(child, parent, cptr_p, nnext)) {
                count += 1;
            }
            if (count >= 1) {
                *nnext = parent->next;
                return true;
            }
            return false;
        }
        case NODE_REPEAT_Z: {
            RNode *child = node->children;
            RNode *parent = node;
            while (node_accepts_ch(child, parent, cptr_p, nnext)) {
            }
            *nnext = parent->next;
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
                *nnext = parent->next;
            } else {
                *cptr_p = start;
            }
            return accepted;
        }
        case NODE_CCLASS: {
            char *start = node->tok+1;
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
                        return true;
                    }
                    return false;
                }
                case ECLASS_WORD: {
                    if (isalpha(**cptr_p) || **cptr_p == '_') {
                        (*cptr_p)++;
                        return true;
                    }
                    return false;
                }
                case ECLASS_SPACE: {
                    if (isspace(**cptr_p)) {
                        (*cptr_p)++;
                        return true;
                    }
                    return false;
                }
                default: {
                    UNREACHABLE("eclass type");
                }
            }
        }
        case NODE_DOT: {
            (*cptr_p)++;
            return true;
        }
        default:
            UNREACHABLE("node type");
            return false;
    }
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
    char *cptr = string;
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
    while (**cptr_p) {
        regex_debug(1, "matching '%c' at nodetype=%s", **cptr_p, nodeTypeName(node->type));
        RNode *nnext = NULL;
        char *match_start = *cptr_p;
        if (node_accepts_ch(node, nparent, cptr_p, &nnext)) {
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
        } else {
            regex_debug(1, "no match for '%c'", **cptr_p);
            (*cptr_p)++;
            start = *cptr_p;
        }
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
