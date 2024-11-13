#include "regex.h"
#include "xalloc.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

static int Precedences[] = {
    [RXGROUP_ROUND]         = 0,
    [RXGROUP_OR]            = 1,
    [RXGROUP_CON]           = 2,

    [RXGROUP_RANGE]         = 10,

    [RXGROUP_LIT]           = INT_MAX,
    [RXGROUP_WORD_START]    = INT_MAX,
    [RXGROUP_WORD_END]      = INT_MAX,
    [RXGROUP_START]         = INT_MAX,
    [RXGROUP_END]           = INT_MAX,
};

void set_char(struct char_set *set, unsigned char ch)
{
    set->set[ch >> 4] |= 1 << (ch & 0xf);
}

void toggle_char(struct char_set *set, unsigned char ch)
{
    set->set[ch >> 4] ^= 1 << (ch & 0xf);
}

bool is_char_toggled(struct char_set *set, unsigned char ch)
{
    return (set->set[ch >> 4] & (1 << (ch & 0xf)));
}

void invert_chars(struct char_set *set)
{
    unsigned        i;

    for (i = 0; i < ARRAY_SIZE(set->set); i++) {
        set->set[i] ^= 0xffff;
    }
}

static void walk_up_precedences(struct regex_parser *rp, int prec)
{
    while (rp->num_stack > 1) {
        if (Precedences[rp->stack[rp->num_stack - 2]->type] < prec) {
            break;
        }
        rp->num_stack--;
    }
}

static void enter_group(struct regex_parser *rp, int group_type)
{
    struct regex_group  *group;

    rp->stack = xreallocarray(rp->stack, rp->num_stack + 1,
                                 sizeof(*rp->stack));
    group = xcalloc(1, sizeof(*group));
    group->type = group_type;
    rp->stack[rp->num_stack - 1]->right = group;
    rp->stack[rp->num_stack++] = group;
}

static void walk_down(struct regex_parser *rp)
{
    rp->stack = xreallocarray(rp->stack, rp->num_stack + 1,
                              sizeof(*rp->stack));
    rp->stack[rp->num_stack] = rp->stack[rp->num_stack - 1]->left;
    rp->num_stack++;
}

static void exchange_group(struct regex_parser *rp, int group_type)
{
    struct regex_group  *group;
    struct regex_group  *parent;

    group = xcalloc(1, sizeof(*group));
    group->type = group_type;
    group->left = rp->stack[rp->num_stack - 1];
    if (rp->num_stack > 1) {
        parent = rp->stack[rp->num_stack - 2];
        if (parent->right != NULL) {
            parent->right = group;
        } else {
            parent->left = group;
        }
    }
    rp->stack[rp->num_stack - 1] = group;
}

static char get_escaped(char ch)
{
    switch (ch) {
    case '0': return '\0';
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'n': return '\n';
    case 'r': return '\r';
    case 'v': return '\v';
    case 't': return '\t';
    }
    return ch;
}

static const char *get_hex_sequence(const char *s, int *amount, char *chs)
{
    int             a;
    int             c, n;

    if (s[0] == '\0') {
        chs[0] = 'x';
        *amount = 1;
        return s;
    }

    a = *amount;
    n = 0;
    for (c = 0; a > 0 && s[0] != '\0'; a--, s++) {
        c <<= 4;
        c |= isalpha(s[0]) ? tolower(s[0]) - 'a' + 10 :
            s[0] - '0';
        if (a % 2 == 1) {
            chs[n++] = c;
            c = 0;
        }
    }
    *amount = n;
    return s;
}

static void make_special_chars(struct char_set *s, char id)
{
    bool            do_invert;
    int             c;

    do_invert = isupper(id);
    switch (id) {
    case 'D':
    case 'd':
        for (c = '0'; c <= '9'; c++) {
            set_char(s, c);
        }
        break;

    case 'K':
    case 'k':
        for (c = '0'; c <= '9'; c++) {
            set_char(s, c);
        }
        for (c = 'a'; c <= 'z'; c++) {
            set_char(s, c);
        }
        for (c = 'A'; c <= 'Z'; c++) {
            set_char(s, c);
        }
        set_char(s, '_');
        break;

    case 'S':
    case 's':
        set_char(s, ' ');
        set_char(s, '\f');
        set_char(s, '\t');
        set_char(s, '\v');
        break;

    case 'W':
    case 'w':
        for (c = 'a'; c <= 'z'; c++) {
            set_char(s, c);
        }
        for (c = 'A'; c <= 'Z'; c++) {
            set_char(s, c);
        }
        break;
    }

    if (do_invert) {
        invert_chars(s);
    }
}

const char *parse_special_group(struct char_set *set, const char *s)
{
    const char      *e;
    bool            do_invert;
    unsigned char   prev_c, c;
    bool            has_range;

    /* find end */
    for (e = s; e[0] != ']'; e++) {
        if (e[0] == '\\') {
            e++;
        }
        if (e[0] == '\0') {
            /* if the end does not exist, interpret it as literal '[' */
            set_char(set, '[');
            return s + 1;
        }
    }

    s++;

    if (s[0] == '^') {
        if (&s[1] == e) {
            set_char(set, '^');
            return e + 1;
        }
        do_invert = true;
        s++;
    } else {
        do_invert = false;
    }

    has_range = false;
    for (; s != e; s++) {
        if (s[0] == '\\') {
            s++;
            if (s[0] == 'x') {
                if (s[1] == ']') {
                    c = 'x';
                } else if (isalpha(s[1]) || isdigit(s[1])) {
                    s++;
                    c = isalpha(s[0]) ? tolower(s[0]) - 'a' + 10 :
                            s[0] - '0';
                    if (isalpha(s[1]) || isdigit(s[1])) {
                        s++;
                        c <<= 4;
                        c |= isalpha(s[0]) ? tolower(s[0]) - 'a' + 10 :
                                s[0] - '0';
                    }
                }
            } else {
                c = get_escaped(s[0]);
            }
        } else if (s[0] == '-') {
            if (&s[1] == e) {
                c = '-';
            } else {
                has_range = true;
                continue;
            }
        } else {
            c = s[0];
        }
        if (has_range) {
            for (; prev_c != c; prev_c++) {
                set_char(set, prev_c);
            }
            has_range = false;
        }
        set_char(set, c);
        prev_c = c;
    }
    if (do_invert) {
        invert_chars(set);
    }
    return e + 1;
}

struct regex_group *parse_regex(const char *s)
{
    struct regex_parser rp;
    char                chs[4];
    int                 n, i;
    size_t              ns;
    struct regex_group  *gr;

    rp.stack = xmalloc(sizeof(*rp.stack));
    rp.stack[0] = xcalloc(1, sizeof(*rp.stack[0]));
    rp.num_stack = 1;

beg:
    if (s[0] == '(') {
        exchange_group(&rp, RXGROUP_ROUND);
        walk_down(&rp);
        s++;
        goto beg;
    }

    if (s[0] == '\\') {
        s++;
        rp.stack[rp.num_stack - 1]->type = RXGROUP_LIT;
        if (s[0] == '\0') {
            set_char(&rp.stack[rp.num_stack - 1]->chars, '\\');
        } else if (strchr("+*|()", s[0]) != NULL) {
            set_char(&rp.stack[rp.num_stack - 1]->chars, s[0]);
            s++;
        } else if (strchr("xuU", s[0]) != NULL) {
            n = s[0] == 'x' ? 2 : s[0] == 'u' ? 4 : 8;
            s++;
            s = get_hex_sequence(s, &n, chs);
            set_char(&rp.stack[rp.num_stack - 1]->chars, chs[0]);
            for (i = 1; i < n; i++) {
                exchange_group(&rp, RXGROUP_CON);
                enter_group(&rp, RXGROUP_LIT);
                set_char(&rp.stack[rp.num_stack - 1]->chars, chs[i]);
            }
        } else if (strchr("DdKkSsWw", s[0]) != NULL) {
            make_special_chars(&rp.stack[rp.num_stack - 1]->chars, s[0]);
            s++;
        } else {
            set_char(&rp.stack[rp.num_stack - 1]->chars, get_escaped(s[0]));
            s++;
        }
    } else if (s[0] == '[') {
        rp.stack[rp.num_stack - 1]->type = RXGROUP_LIT;
        s = parse_special_group(&rp.stack[rp.num_stack - 1]->chars, s);
    } else if (s[0] == '.') {
        rp.stack[rp.num_stack - 1]->type = RXGROUP_LIT;
        memset(&rp.stack[rp.num_stack - 1]->chars, 0xff,
               sizeof(rp.stack[rp.num_stack - 1]->chars));
        toggle_char(&rp.stack[rp.num_stack - 1]->chars, '\n');
        s++;
    } else if (s[0] == '<') {
        rp.stack[rp.num_stack - 1]->type = RXGROUP_WORD_START;
        s++;
    } else if (s[0] == '>') {
        rp.stack[rp.num_stack - 1]->type = RXGROUP_WORD_END;
        s++;
    } else if (s[0] == '^') {
        rp.stack[rp.num_stack - 1]->type = RXGROUP_START;
        s++;
    } else if (s[0] == '$') {
        rp.stack[rp.num_stack - 1]->type = RXGROUP_END;
        s++;
    } else if (s[0] != '\0' && strchr("+*|(", s[0]) == NULL) {
        rp.stack[rp.num_stack - 1]->type = RXGROUP_LIT;
        set_char(&rp.stack[rp.num_stack - 1]->chars, s[0]);
        s++;
    }

infix:
    if (s[0] == '\0') {
        gr = rp.stack[0];
        free(rp.stack);
        return gr;
    }

    if (s[0] == ')') {
        /* find matching left bracket */
        ns = rp.num_stack;
        if (Precedences[rp.stack[ns - 1]->type] == 0) {
            ns--;
        }
        while (ns > 0) {
            if (Precedences[rp.stack[ns - 1]->type] == 0) {
               break;
            }
            ns--;
        }
        s++;
        if (ns == 0 || rp.stack[ns - 1]->type != RXGROUP_ROUND) {
            rp.stack[rp.num_stack - 1]->type = RXGROUP_LIT;
            set_char(&rp.stack[rp.num_stack - 1]->chars, s[0]);
        } else {
            rp.num_stack = ns;
        }
        goto infix;
    }

    if (s[0] == '|') {
        walk_up_precedences(&rp, Precedences[RXGROUP_OR]);
        exchange_group(&rp, RXGROUP_OR);
        enter_group(&rp, RXGROUP_NULL);
        s++;
    } else if (strchr("+*?", s[0]) != NULL) {
        walk_up_precedences(&rp, Precedences[RXGROUP_RANGE]);
        exchange_group(&rp, RXGROUP_RANGE);
        rp.stack[rp.num_stack - 1]->min = s[0] == '+' ? 1 : 0;
        rp.stack[rp.num_stack - 1]->max = s[0] == '?' ? 1 : SIZE_MAX;
        s++;
        goto infix;
    } else {
        walk_up_precedences(&rp, Precedences[RXGROUP_CON] + 1);
        exchange_group(&rp, RXGROUP_CON);
        enter_group(&rp, RXGROUP_NULL);
    }
    goto beg;
}

void free_regex_group(struct regex_group *group)
{
    if (group == NULL) {
        return;
    }
    free_regex_group(group->left);
    free_regex_group(group->right);
    free(group);
}

struct matcher_stack {
    struct matcher_stack_item {
        struct regex_group *group;
        struct pos pos;
        size_t times;
    } *items;
    size_t len;
};

static inline void push_group(struct matcher_stack *stack,
                              struct regex_group *group,
                              struct pos *pos)
{
    stack->items = xrealloc(stack->items,
                            sizeof(*stack->items) * (stack->len + 1));
    stack->items[stack->len].group = group;
    stack->items[stack->len].pos = *pos;
    stack->items[stack->len].times = 0;
    stack->len++;
}

static inline struct regex_group *pop_group(struct matcher_stack *stack,
                                            struct pos *pos)
{
    struct matcher_stack_item   *item;

    item = &stack->items[--stack->len];
    if (pos != NULL) {
        *pos = item->pos;
    }
    return item->group;
}

static inline int pop_group_success(struct matcher_stack *stack,
                                    struct regex_group **p_group,
                                    struct pos *pos)
{
    struct regex_group  *group;
    size_t              times;

    while (stack->len > 0) {
        group = pop_group(stack, NULL);
        switch (group->type) {
        case RXGROUP_OR:
        case RXGROUP_ROUND:
            break;

        case RXGROUP_CON:
            *p_group = group->right;
            return 0;

        case RXGROUP_RANGE:
            times = stack->items[stack->len].times + 1;
            if (times < group->max) {
                push_group(stack, group, pos);
                stack->items[stack->len - 1].times = times;
                *p_group = group->left;
                return 0;
            }
            break;

        default:
            *p_group = group;
            return 0;
        }
    }
    *p_group = NULL;
    return 0;
}

static inline int pop_group_fail(struct matcher_stack *stack,
                                 struct regex_group **p_group,
                                 struct pos *pos)
{
    struct regex_group  *group;

    while (stack->len > 0) {
        group = pop_group(stack, NULL);
        switch (group->type) {
        case RXGROUP_OR:
            *pos = stack->items[stack->len].pos;
            *p_group = group->right;
            return 0;

        case RXGROUP_RANGE:
            if (stack->items[stack->len].times < group->min) {
                break;
            }
            *pos = stack->items[stack->len].pos;
            return pop_group_success(stack, p_group, pos);

        default:
            /* nothing */
            break;
        }
    }
    return -1;
}

int match_regex(struct regex_group *group, struct regex_matcher *matcher)
{
    struct matcher_stack    stack;
    struct line             *line;

    memset(&stack, 0, sizeof(stack));
    while (group != NULL) {
        switch (group->type) {
        case RXGROUP_NULL:
            goto fail;

        case RXGROUP_ROUND:
        case RXGROUP_CON:
        case RXGROUP_OR:
        case RXGROUP_RANGE:
            push_group(&stack, group, &matcher->pos);
            group = group->left;
            continue;

        case RXGROUP_WORD_START:
            line = &matcher->lines[matcher->pos.line];
            if (matcher->pos.col == line->n ||
                    !isidentf(line->s[matcher->pos.col]) ||
                    (matcher->pos.col > 0 &&
                     isidentf(line->s[matcher->pos.col - 1]))) {
                goto fail;
            }
            break;

        case RXGROUP_WORD_END:
            line = &matcher->lines[matcher->pos.line];
            if (matcher->pos.col == line->n || matcher->pos.col == 0 ||
                    isidentf(line->s[matcher->pos.col]) ||
                    !isidentf(line->s[matcher->pos.col - 1])) {
                goto fail;
            }
            break;

        case RXGROUP_START:
            if (matcher->pos.col > 0) {
                goto fail;
            }
            break;

        case RXGROUP_END:
            line = &matcher->lines[matcher->pos.line];
            if (matcher->pos.col < line->n) {
                goto fail;
            }
            break;

        case RXGROUP_LIT:
            line = &matcher->lines[matcher->pos.line];
            if (matcher->pos.col == line->n) {
                if (matcher->pos.line + 1 < matcher->num_lines &&
                        is_char_toggled(&group->chars, '\n')) {
                    matcher->pos.col = 0;
                    matcher->pos.line++;
                    break;
                }
            } else if (is_char_toggled(&group->chars,
                                       line->s[matcher->pos.col])) {
                matcher->pos.col++;
                break;
            }
            goto fail;
        }

        (void) pop_group_success(&stack, &group, &matcher->pos);
        continue;

    fail:
        if (pop_group_fail(&stack, &group, &matcher->pos) == -1) {
            goto mismatch;
        }
    }

    free(stack.items);
    return 0;

mismatch:
    free(stack.items);
    return 1;
}
