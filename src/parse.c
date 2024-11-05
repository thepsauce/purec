#include "purec.h"
#include "parse.h"
#include "xalloc.h"
#include "util.h"

#include <ctype.h>
#include <string.h>

struct parser Parser;

static const int Precedences[] = {
    [GROUP_NULL] = 0,

    [GROUP_ROUND] = 0,
    [GROUP_DOUBLE_CORNER] = 0,
    [GROUP_CORNER] = 0,
    [GROUP_SQUARE] = 0,
    [GROUP_CURLY] = 0,
    [GROUP_DOUBLE_BAR] = 0,
    [GROUP_BAR] = 0,

    [GROUP_SEMICOLON] = 1,
    [GROUP_COMMA] = 2,

    [GROUP_IF] = 3,
    [GROUP_DO] = 3,
    [GROUP_ELSE] = 3,

    [GROUP_NOT] = 4,

    [GROUP_AND] = 4,
    [GROUP_OR] = 4,
    [GROUP_XOR] = 4,

    [GROUP_LESS] = 5,
    [GROUP_LESS_EQUAL] = 5,
    [GROUP_GREATER] = 5,
    [GROUP_GREATER_EQUAL] = 5,
    [GROUP_EQUAL] = 5,
    [GROUP_NOT_EQUAL] = 5,

    [GROUP_POSITIVE] = 6,
    [GROUP_NEGATE] = 6,

    [GROUP_PLUS] = 6,
    [GROUP_MINUS] = 6,

    [GROUP_MULTIPLY] = 7,
    [GROUP_DIVIDE] = 7,
    [GROUP_MOD] = 7,

    [GROUP_IMPLICIT] = 7,

    [GROUP_SQRT] = 9,
    [GROUP_CBRT] = 9,

    [GROUP_RAISE] = 9,
    [GROUP_RAISE2] = 9,
    [GROUP_RAISE3] = 9,

    [GROUP_LOWER] = 10,

    [GROUP_EXCLAM] = 11,
    [GROUP_PERCENT] = 11,

    [GROUP_VARIABLE] = INT_MAX,
    [GROUP_NUMBER] = INT_MAX,
    [GROUP_STRING] = INT_MAX,
};

static size_t skip_space(void)
{
    size_t n = 0;
    while (isspace(Parser.p[0])) {
        Parser.p++;
        n++;
    }
    return n;
}

static int read_word(void)
{
    if (!isalpha(Parser.p[0])) {
        set_error("expected word");
        return -1;
    }
    Parser.w = Parser.p;
    while (isalpha(Parser.p[0])) {
        Parser.p++;
    }
    Parser.n = Parser.p - Parser.w;
    return 0;
}

int parse_number(char *s, size_t n, char **p_end, long double *p_f)
{
    long double     s1, f;
    long double     base;
    long double     div;
    long double     d;
    long double     s2;
    long double     e;

    if (n == 0) {
        return -1;
    }

    if (s[0] == '+') {
        s1 = 1;
        s++;
    } else if (s[0] == '-') {
        s1 = -1;
        s++;
    } else {
        s1 = 1;
    }
    if (s[0] == '.') {
        if (n == 1 || !isdigit(s[1])) {
            return -1;
        }
    } else if (!isdigit(s[0])) {
        return -1;
    }
    f = 0;
    base = 10;
    if (s[0] == '0') {
        s++;
        if (n == 1) {
            *p_end = s;
            *p_f = 0;
            return 0;
        }
        n--;
        switch (s[0]) {
        case 'b':
        case 'B':
            base = 2;
            if (n == 1 || (s[1] != '0' && s[1] != '1')) {
                *p_end = s;
                *p_f = 0;
                return 0;
            }
            s++;
            n--;
            while (n > 0 && (s[0] == '0' || s[0] == '1')) {
                f = f * 2 + (s[0] - '0');
                s++;
                n--;
            }
            break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
            base = 8;
            while (n > 0 && (s[0] >= '0' && s[0] <= '7')) {
                f = f * 8 + (s[0] - '0');
                s++;
                n--;
            }
            break;

        case 'x':
        case 'X':
            base = 16;
            if (n == 1 || !isxdigit(s[1])) {
                *p_end = s;
                *p_f = 0;
                return 0;
            }
            s++;
            n--;
            while (n > 0 && isxdigit(s[0])) {
                f = f * 16 + (isdigit(s[0]) ? s[0] - '0' :
                        tolower(s[0]) - 'a' + 10);
                s++;
                n--;
            }
            break;
        }
    } else {
        while (n > 0 && isdigit(s[0])) {
            f = f * 10 + (s[0] - '0');
            s++;
            n--;
        }
    }
    if (n > 0 && s[0] == '.') {
        div = 1;
        d = 0;
        s++;
        n--;
        if (base == 16) {
            while (n > 0 && isxdigit(s[0])) {
                div *= 16;
                d = d * 16 + (isdigit(s[0]) ? s[1] - '0' :
                        tolower(s[0]) - 'a' + 10);
                s++;
                n--;
            }
        } else {
            while (n > 0 && s[0] >= '0' && s[0] <= base + '0' - 1) {
                div *= base;
                d = d * base + s[0] - '0';
                s++;
                n--;
            }
        }
        f += d / div;
    }
    if (n > 1 && (s[0] == 'e' || s[0] == 'E')) {
        if (isdigit(s[1]) || (n > 2 && (s[1] == '+' || s[1] == '-') &&
                              isdigit(s[2]))) {
            s++;
            n--;
            if (s[0] == '+') {
                s2 = 1;
                s++;
                n--;
            } else if (s[0] == '-') {
                s2 = -1;
                s++;
                n--;
            } else {
                s2 = 1;
            }
            e = 0;
            while (n > 0 && isdigit(s[0])) {
                e = e * 10 + (s[0] - '0');
                s++;
                n--;
            }
            f *= powl(base, s2 * e);
        }
    }
    *p_f = s1 * f;
    *p_end = s;
    return 0;
}

char *parse_string(char *s, char **p_end, size_t *p_n, char term)
{
    char            *p;
    size_t          a, n;
    int             hexa;
    int             c;

    p = NULL;
    n = 0;
    a = 0;
    while (s[0] != term && s[0] != '\0') {
        if (s[0] == '\\') {
            s++;
            if (s[0] == 'x' ||
                    s[0] == 'u' ||
                    s[0] == 'U') {
                hexa = s[0] == 'x' ? 2 :
                        s[0] == 'u' ? 4 : 8;
                s++;
                if (n + hexa > a) {
                    a *= 2;
                    a += hexa;
                    p = xrealloc(p, a);
                }
                for (c = 0; hexa > 0 && s[0] != '\0'; hexa--, s++) {
                    c <<= 4;
                    c |= isalpha(s[0]) ? tolower(s[0]) - 'a' + 10 :
                        s[0] - '0';
                    if (hexa % 2 == 1) {
                        p[n++] = c;
                        c = 0;
                    }
                }
                continue;
            }
            switch (s[0]) {
            case '\0':
                s--;
                c = '\\';
                break;
            case '0': c = '\0'; break;
            case 'a': c = '\a'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 'v': c = '\v'; break;
            case 't': c = '\t'; break;
            default:
                c = s[0];
            }
        } else {
            c = s[0];
        }
        if (n == a) {
            a *= 2;
            a++;
            p = xrealloc(p, a);
        }
        p[n++] = c;
        s++;
    }
    *p_end = s;
    *p_n = n;
    return xrealloc(p, n);
}

static size_t begins_with(const char *s)
{
    size_t          n;

    for (n = 0; *s != '\0'; s++) {
        if (*s != Parser.p[n]) {
            return 0;
        }
        n++;
    }
    return n;
}

void copy_group(struct group *dest, const struct group *src)
{
    dest->type = src->type;
    dest->v = src->v;
    dest->left = NULL;
    dest->right = NULL;
    switch (src->type) {
    case GROUP_NUMBER:
        dest->v.f = src->v.f;
        break;

    case GROUP_VARIABLE:
        dest->v.w = src->v.w;
        break;

    case GROUP_STRING:
        dest->v.s.p = xmemdup(src->v.s.p, src->v.s.n);
        dest->v.s.n = src->v.s.n;
        break;

    default:
        if (src->left != NULL) {
            dest->left  = xmalloc(sizeof(*dest->left));
            copy_group(dest->left, src->left);
        }
        if (src->right != NULL) {
            dest->right = xmalloc(sizeof(*dest->right));
            copy_group(dest->right, src->right);
        }
    }
}

void clear_group(struct group *group)
{
    if (group->type == GROUP_STRING) {
        free(group->v.s.p);
    }
    if (group->left != NULL) {
        free_group(group->left);
    }
    if (group->right != NULL) {
        free_group(group->right);
    }
}

void free_group(struct group *group)
{
    clear_group(group);
    free(group);
}

static void walk_up_precedences(int prec)
{
    while (Parser.num_stack > 1) {
        if (Precedences[Parser.stack[Parser.num_stack - 2]->type] < prec) {
            break;
        }
        Parser.num_stack--;
    }
}

static void enter_group(int group_type)
{
    struct group    *group;

    Parser.stack = xreallocarray(Parser.stack, Parser.num_stack + 1,
                                 sizeof(*Parser.stack));
    group = xcalloc(1, sizeof(*group));
    group->type = group_type;
    Parser.stack[Parser.num_stack - 1]->right = group;
    Parser.stack[Parser.num_stack++] = group;
}

static void walk_down(void)
{
    Parser.stack = xreallocarray(Parser.stack, Parser.num_stack + 1,
                                 sizeof(*Parser.stack));
    Parser.stack[Parser.num_stack] = Parser.stack[Parser.num_stack - 1]->left;
    Parser.num_stack++;
}

static void exchange_group(int group_type)
{
    struct group    *group;
    struct group    *parent;

    group = xcalloc(1, sizeof(*group));
    group->type = group_type;
    group->left = Parser.stack[Parser.num_stack - 1];
    if (Parser.num_stack > 1) {
        parent = Parser.stack[Parser.num_stack - 2];
        if (parent->right != NULL) {
            parent->right = group;
        } else {
            parent->left = group;
        }
    }
    Parser.stack[Parser.num_stack - 1] = group;
}

static char *search_entry(const char *word, size_t len, size_t *pIndex)
{
    size_t          l, m, r;
    int             cmp;

    l = 0;
    r = Parser.num_words;
    while (l < r) {
        m = (l + r) / 2;

        cmp = strncmp(Parser.words[m], word, len);
        if (cmp == 0) {
            cmp = (unsigned char) Parser.words[m][len];
        }
        if (cmp == 0) {
            if (pIndex != NULL) {
                *pIndex = m;
            }
            return Parser.words[m];
        }
        if (cmp < 0) {
            l = m + 1;
        } else {
            r = m;
        }
    }
    if (pIndex != NULL) {
        *pIndex = r;
    }
    return NULL;
}

char *save_word(const char *word, size_t len)
{
    char *w;
    char **words;
    size_t index;

    w = search_entry(word, len, &index);
    if (w != NULL) {
        return w;
    }

    words = xreallocarray(Parser.words, Parser.num_words + 1,
                          sizeof(*Parser.words));
    Parser.words = words;

    w = xstrndup(word, len);
    memmove(&Parser.words[index + 1], &Parser.words[index],
            sizeof(*Parser.words) * (Parser.num_words - index));
    Parser.words[index] = w;
    Parser.num_words++;
    return w;
}

struct group *parse(const char *s)
{
    /* opr expr */
    static const struct prefix_operator {
        const char *s;
        int t;
    } prefixes[] = {
        { "+", GROUP_POSITIVE },
        { "-", GROUP_NEGATE },
        { "negate", GROUP_NEGATE },
        { "not", GROUP_NOT },
        { "√", GROUP_SQRT },
        { "∛", GROUP_CBRT },
    };

    /* expr opr expr */
    static const struct infix_operator {
        const char *s;
        int t;
    } infixes[] = {
        { ";", GROUP_SEMICOLON },

        { ",", GROUP_COMMA },

        { "if", GROUP_IF },
        { "do", GROUP_DO },

        { "and", GROUP_AND },
        { "or", GROUP_OR },
        { "xor", GROUP_XOR },

        { "equals", GROUP_EQUAL },
        { "greater equals", GROUP_GREATER_EQUAL },
        { "greater than", GROUP_GREATER },
        { "less equals", GROUP_LESS_EQUAL },
        { "less than", GROUP_LESS },

        { "<=", GROUP_LESS_EQUAL },
        { "<", GROUP_LESS },
        { "=", GROUP_EQUAL },
        { ">=", GROUP_GREATER_EQUAL },
        { ">", GROUP_GREATER },
        { "≠", GROUP_NOT_EQUAL },
        { "≤", GROUP_LESS_EQUAL },
        { "≥", GROUP_GREATER_EQUAL },

        { "+", GROUP_PLUS },
        { "-", GROUP_MINUS },
        { "*", GROUP_MULTIPLY },
        { "·", GROUP_MULTIPLY },
        { "/", GROUP_DIVIDE },
        { "mod", GROUP_MOD },

        { "^", GROUP_RAISE },
        { "_", GROUP_LOWER },
    };

    /* expr opr */
    static const struct suffix_operator {
        const char *s;
        int t;
    } suffixes[] = {
        { "!", GROUP_EXCLAM },
        { "%", GROUP_PERCENT },
        { "²", GROUP_RAISE2 },
        { "³", GROUP_RAISE3 },
        { "else", GROUP_ELSE },
    };

    /* make sure these have the same addresses */
    static const char *const bar = "|";
    static const char *const doubleBar = "||";
    static const char *const doubleBar2 = "‖";

    /* opr expr opr */
    static const struct match_operator {
        const char *l, *r;
        int t;
    } matches[] = {
        { "(", ")", GROUP_ROUND },
        { "«", "»", GROUP_DOUBLE_CORNER },
        { "<<", ">>", GROUP_DOUBLE_CORNER },
        { "<", ">", GROUP_CORNER },
        { "[", "]", GROUP_SQUARE },
        { "{", "}", GROUP_CURLY },
        { doubleBar2, doubleBar2, GROUP_DOUBLE_BAR },
        { doubleBar, doubleBar, GROUP_DOUBLE_BAR },
        { bar, bar, GROUP_BAR },
        { "⌈", "⌉", GROUP_CEIL },
        { "⌊", "⌋", GROUP_FLOOR },
    };

    size_t          i, n;
    char            *end;
    size_t          ns;

    Parser.s = (char*) s;
    Parser.p = Parser.s;

    if (Parser.num_stack > 0) {
        clear_group(Parser.stack[0]);
        memset(Parser.stack[0], 0, sizeof(*Parser.stack[0]));
    } else {
        Parser.stack = xmalloc(sizeof(*Parser.stack));
        Parser.stack[0] = xcalloc(1, sizeof(*Parser.stack[0]));
    }
    Parser.num_stack = 1;

beg:
    skip_space();

    for (i = 0; i < ARRAY_SIZE(prefixes); i++) {
        n = begins_with(prefixes[i].s);
        if (n > 0) {
            Parser.p += n;
            exchange_group(prefixes[i].t);
            walk_down();
            goto beg;
        }
    }

    for (i = 0; i < ARRAY_SIZE(matches); i++) {
        n = begins_with(matches[i].l);
        if (n > 0) {
            Parser.p += n;
            exchange_group(matches[i].t);
            walk_down();
            goto beg;
        }
    }

    if (isalpha(Parser.p[0])) {
        read_word();
        Parser.stack[Parser.num_stack - 1]->type = GROUP_VARIABLE;
        Parser.stack[Parser.num_stack - 1]->v.w = save_word(Parser.w, Parser.n);
    } else if ((isdigit(Parser.p[0]) || Parser.p[0] == '.') &&
               parse_number(Parser.p, SIZE_MAX, &end,
                            &Parser.stack[Parser.num_stack - 1]->v.f) == 0) {
        Parser.p = end;
        Parser.stack[Parser.num_stack - 1]->type = GROUP_NUMBER;
    } else if (Parser.p[0] == '\"') {
        Parser.p++;
        Parser.str.p = parse_string(Parser.p, &Parser.p, &Parser.str.n, '\"');
        if (Parser.p[0] == '\"') {
            Parser.p++;
        }
        Parser.stack[Parser.num_stack - 1]->type = GROUP_STRING;
        Parser.stack[Parser.num_stack - 1]->v.s = Parser.str;
    } else if (Parser.p[0] == '\\' && isdigit(Parser.p[1])) {
        Parser.stack[Parser.num_stack - 1]->type = GROUP_VARIABLE;
        Parser.stack[Parser.num_stack - 1]->v.w = save_word(Parser.p, 2);
        Parser.p += 2;
    } else if (Parser.p[0] != '\0') {
        Parser.stack[Parser.num_stack - 1]->type = GROUP_VARIABLE;
        Parser.w = Parser.p;
        if (!(Parser.p[0] & 0x80)) {
            Parser.p++;
        } else {
            while (Parser.p++, (Parser.p[0] & 0xc0) == 0x80) {
                (void) 0;
            }
        }
        Parser.n = Parser.p - Parser.w;
        Parser.stack[Parser.num_stack - 1]->v.w = save_word(Parser.w, Parser.n);
    } else {
        set_error("missing operand");
        return NULL;
    }

infix:
    skip_space();
    if (Parser.p[0] == '\0') {
        return Parser.stack[0];
    }

    /* this must come before infixes because otherwise
     * a closing >> is intepreted as > (greater)
     */
    for (i = 0; i < ARRAY_SIZE(matches); i++) {
        n = begins_with(matches[i].r);
        if (n > 0) {
            /* find matching left bracket */
            ns = Parser.num_stack;
            if (Precedences[Parser.stack[ns - 1]->type] == 0) {
                ns--;
            }
            while (ns > 0) {
                if (Precedences[Parser.stack[ns - 1]->type] == 0) {
                   break;
                }
                ns--;
            }
            if (ns == 0) {
                set_error("not open: '%s' needs matching '%s'",
                        matches[i].r, matches[i].l);
                return NULL;
            }
            if (Parser.stack[ns - 1]->type != matches[i].t) {
                if (matches[i].l == matches[i].r) {
                    goto beg;
                }
                set_error("collision: a previous pair needs to be closed first");
                return NULL;
            }

            Parser.p += n;
            Parser.num_stack = ns;
            goto infix;
        }
    }

    for (i = 0; i < ARRAY_SIZE(infixes); i++) {
        n = begins_with(infixes[i].s);
        if (n > 0) {
            Parser.p += n;
            walk_up_precedences(Precedences[infixes[i].t]);
            exchange_group(infixes[i].t);
            enter_group(GROUP_NULL);
            goto beg;
        }
    }

    for (i = 0; i < ARRAY_SIZE(suffixes); i++) {
        n = begins_with(suffixes[i].s);
        if (n > 0) {
            Parser.p += n;
            walk_up_precedences(Precedences[suffixes[i].t]);
            exchange_group(suffixes[i].t);
            goto infix;
        }
    }

    /* implicit operator */
    /* +1 because otherwise `f f 4` would be translated to f(f)*4 assuming f is
     * a function needing one argument
     */
    walk_up_precedences(Precedences[GROUP_IMPLICIT] + 1);
    exchange_group(GROUP_IMPLICIT);
    enter_group(0);
    goto beg;
}

static bool get_variable_index(const char *name, size_t *p_index)
{
    size_t          l, m, r;
    int             cmp;

    l = 0;
    r = Parser.num_vars;
    while (l < r) {
        m = (l + r) / 2;
        cmp = strcmp(Parser.vars[m].name, name);
        if (cmp == 0) {
            *p_index = m;
            return true;
        }
        if (cmp < 0) {
            l = m + 1;
        } else {
            r = m;
        }
    }
    *p_index = l;
    return false;
}

size_t get_variable(const char *name)
{
    size_t          i;

    if (!get_variable_index(name, &i)) {
        return SIZE_MAX;
    }
    return i;
}

size_t add_variable(char *name, const struct group *val,
        char *const *args, size_t num_args)
{
    struct var_set  *var;
    size_t          i, j;
    struct var_item *item;

    if (get_variable_index(name, &i)) {
        var = &Parser.vars[i];
        var->items = xreallocarray(var->items, var->num_items + 1,
                                   sizeof(*var->items));
        for (j = 0; j < var->num_items; j++) {
            if (var->items[j].num_args >= num_args) {
                break;
            }
        }
        memmove(&var->items[j + 1],
                &var->items[j],
                sizeof(*var->items) * (var->num_items - j));
        var->num_items++;
        item = &var->items[j];
    } else {
        Parser.vars = xreallocarray(Parser.vars, Parser.num_vars + 1,
                                    sizeof(*Parser.vars));
        memmove(&Parser.vars[i + 1],
                &Parser.vars[i],
                sizeof(*Parser.vars) * (Parser.num_vars - i));
        Parser.num_vars++;
        var = &Parser.vars[i];
        var->name = name;
        var->items = xmalloc(sizeof(*var->items));
        var->num_items = 1;
        item = &var->items[0];
    }
    item->args = xreallocarray(NULL, num_args, sizeof(*item->args));
    memcpy(item->args, args, sizeof(*args) * num_args);
    item->num_args = num_args;
    copy_group(&item->group, val);
    return i;
}

size_t get_local_variable(char *name)
{
    size_t              i;
    struct local_var    *local;

    for (i = Parser.num_locals; i > 0; ) {
        local = &Parser.locals[--i];
        if (local->name == name) {
            return i;
        }
    }
    return SIZE_MAX;
}

void push_local_variable(char *name, struct value *value)
{
    Parser.locals = xreallocarray(Parser.locals, Parser.num_locals + 1,
                                  sizeof(*Parser.locals));
    Parser.locals[Parser.num_locals].name = name;
    Parser.locals[Parser.num_locals].value = *value;
    Parser.num_locals++;
}
