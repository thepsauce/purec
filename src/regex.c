#include "regex.h"
#include "util.h"
#include "xalloc.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

static int Precedences[] = {
    [RXGROUP_ROUND]         = 0,
    [RXGROUP_OR]            = 1,
    [RXGROUP_CON]           = 2,

    [RXGROUP_PLUS]          = 10,
    [RXGROUP_STAR]          = 10,
    [RXGROUP_OPT]           = 10,

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
            c = get_escaped(s[0]);
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
    enum rxgroup_type   type;
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
    } else if (s[0] != '\0' && strchr("+*|()", s[0]) == NULL) {
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
        type = s[0] == '+' ? RXGROUP_PLUS : s[0] == '*' ? RXGROUP_STAR :
                RXGROUP_OPT;
        walk_up_precedences(&rp, Precedences[type]);
        exchange_group(&rp, type);
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

size_t match_regex(struct regex_group *group, struct regex_matcher *matcher,
                   size_t i)
{
    size_t          l, l2, total_len;

    switch (group->type) {
    case RXGROUP_NULL:
        return SIZE_MAX;

    case RXGROUP_ROUND:
        if (matcher->num == ARRAY_SIZE(matcher->sub)) {
            return match_regex(group->left, matcher, i);
        }
        matcher->sub[matcher->num].start = i;
        l = match_regex(group->left, matcher, i);
        matcher->sub[matcher->num].end = i + l;
        matcher->num++;
        return l;

    case RXGROUP_OR:
        l = match_regex(group->left, matcher, i);
        if (l != SIZE_MAX) {
            return l;
        }
        return match_regex(group->right, matcher, i);

    case RXGROUP_CON:
        l = match_regex(group->left, matcher, i);
        if (l == SIZE_MAX) {
            return SIZE_MAX;
        }
        l2 = match_regex(group->right, matcher, i + l);
        if (l2 == SIZE_MAX) {
            return SIZE_MAX;
        }
        return l + l2;

    case RXGROUP_PLUS:
        l = match_regex(group->left, matcher, i);
        if (l == SIZE_MAX) {
            return SIZE_MAX;
        }
        total_len = 0;
        do {
            i += l;
            total_len += l;
            l = match_regex(group->left, matcher, i);
        } while (l != 0 && l != SIZE_MAX);
        return total_len;

    case RXGROUP_STAR:
        total_len = 0;
        while (1) {
            l = match_regex(group->left, matcher, i);
            if (l == 0 || l == SIZE_MAX) {
                break;
            }
            i += l;
            total_len += l;
        }
        return total_len;

    case RXGROUP_OPT:
        l = match_regex(group->left, matcher, i);
        return l == SIZE_MAX ? 0 : l;

    case RXGROUP_LIT:
        if (i < matcher->len && is_char_toggled(&group->chars, matcher->s[i])) {
            return 1;
        }
        return SIZE_MAX;

    case RXGROUP_WORD_START:
        if (!isidentf(matcher->s[i])) {
            return SIZE_MAX;
        }
        if (i > 0 && isidentf(matcher->s[i - 1])) {
            return SIZE_MAX;
        }
        return 0;

    case RXGROUP_WORD_END:
        if (isidentf(matcher->s[i])) {
            return SIZE_MAX;
        }
        if (i > 0 && !isidentf(matcher->s[i - 1])) {
            return SIZE_MAX;
        }
        return 0;

    case RXGROUP_START:
        if (i > 0) {
            return SIZE_MAX;
        }
        return 0;

    case RXGROUP_END:
        if (i < matcher->len) {
            return SIZE_MAX;
        }
        return 0;
    }
    return SIZE_MAX;
}

bool regex_matches(struct regex_group *group, const char *s)
{
    struct regex_matcher    matcher;
    size_t                  l;

    matcher.s = s;
    matcher.len = strlen(s);
    l = match_regex(group, &matcher, 0);
    return l != SIZE_MAX && s[l] == '\0';
}
