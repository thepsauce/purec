#define MAKE_DEFAULT            2
#define MAKE_STATE_COMMENT      3
#define MAKE_STATE_ASSIGNMENT   4
#define MAKE_VARIABLE_BEGIN     5
#define MAKE_VARIABLE           6

const char *make_statements[] = {
    "abspath",
    "addprefix",
    "addsuffix",
    "and",
    "basename",
    "call",
    "dir",
    "error",
    "eval",
    "file",
    "filter",
    "filter-out",
    "findstring",
    "firstword",
    "flavor",
    "foreach",
    "guile",
    "if",
    "info",
    "join",
    "lastword",
    "notdir",
    "or",
    "origin",
    "patsubst",
    "realpath",
    "shell",
    "sort",
    "strip",
    "subst",
    "suffix",
    "value",
    "warning",
    "wildcard",
    "word",
    "wordlist",
    "words"
};

const char *make_preproc[] = {
    "cmdswitches",
    "else",
    "else if",
    "else ifdef",
    "else ifndef",
    "endif",
    "error",
    "if",
    "ifdef",
    "ifndef",
    "include",
    "message",
    "undef"
};

const char *make_else_preproc[] = {
    "if", "ifdef", "ifndef"
};

size_t make_indentor(struct buf *buf, size_t line_i)
{
    (void) buf;
    (void) line_i;
    return 0;
}

static size_t check_variable(struct state_ctx *ctx)
{
    struct pos  pos;

    ctx->hi = HI_IDENTIFIER;
    if (ctx->s[ctx->pos.col] == '$' && ctx->pos.col + 1 < ctx->n) {
        switch (ctx->s[ctx->pos.col + 1]) {
        case '(':
            ctx->state = MAKE_VARIABLE_BEGIN | (((ctx->state >> 8) + 1) << 8);
            pos.col = ctx->pos.col + 1;
            pos.line = ctx->pos.line;
            add_paren(ctx->buf, &pos, FOPEN_PAREN | '(');
            return 2;

        case '<':
        case '>':
        case '@':
            return 2;
        }
    }
    return 0;
}

size_t make_state_default(struct state_ctx *ctx)
{
    size_t          n;

    n = check_variable(ctx);
    if (n != 0) {
        return n;
    }
    ctx->hi = HI_NORMAL;
    return 1;
}

size_t make_state_start(struct state_ctx *ctx)
{
    size_t          i, j;

    i = ctx->pos.col;
    while (i < ctx->n && isblank(ctx->s[i])) {
        i++;
    }

    if (i != ctx->pos.col) {
        return i - ctx->pos.col;
    }

    switch (ctx->s[ctx->pos.col]) {
    case '#':
        ctx->hi = HI_COMMENT;
        return ctx->n - ctx->pos.col;

    case '-':
    case '!':
        ctx->hi = HI_NORMAL;
        i++;
        while (i < ctx->n && isalpha(ctx->s[i])) {
            i++;
        }
        if (i - 1 > ctx->pos.col) {
            if (i - ctx->pos.col == 5 && memcmp(&ctx->s[ctx->pos.col + 1],
                                                "else", 4) == 0) {
                ctx->hi = HI_PREPROC;
                while (i < ctx->n && isblank(ctx->s[i])) {
                    i++;
                }
                j = i;
                while (j < ctx->n && isalpha(ctx->s[j])) {
                    j++;
                }
                if (bin_search(make_else_preproc,
                               ARRAY_SIZE(make_else_preproc),
                               &ctx->s[i],
                               j - i) != NULL) {
                    return j - ctx->pos.col;
                }
            } else if (bin_search(make_preproc,
                    ARRAY_SIZE(make_preproc),
                    &ctx->s[ctx->pos.col + 1],
                    i - 1 - ctx->pos.col) != NULL) {
                ctx->hi = HI_PREPROC;
            }
        }
        return i - ctx->pos.col;

    default:
        return make_state_default(ctx);
    }
}

size_t make_state_variable(struct state_ctx *ctx)
{
    size_t n;

    n = check_variable(ctx);
    if (n != 0) {
        return n;
    }
    if (ctx->s[ctx->pos.col] == ')') {
        add_paren(ctx->buf, &ctx->pos, '(');
        ctx->state = (ctx->state >> 8) - 1;
        if (ctx->state == 0) {
            ctx->state = MAKE_DEFAULT;
        } else {
            ctx->state = MAKE_VARIABLE_BEGIN | (ctx->state << 8);
        }
    }
    return 1;
}

size_t make_state_variable_begin(struct state_ctx *ctx)
{
    size_t          i;

    ctx->hi = HI_IDENTIFIER;
    i = ctx->pos.col;
    while (i < ctx->n && isblank(ctx->s[i])) {
        i++;
    }
    if (i != ctx->pos.col) {
        return i - ctx->pos.col;
    }
    if (isalpha(ctx->s[i])) {
        do {
            i++;
        } while (i < ctx->n && (isalpha(ctx->s[i]) || ctx->s[i] == '-'));
        if (bin_search(make_statements, ARRAY_SIZE(make_statements),
                   &ctx->s[ctx->pos.col], i - ctx->pos.col) != NULL) {
            ctx->hi = HI_STATEMENT;
        }
    }
    ctx->state = MAKE_VARIABLE | (ctx->state & ~0xff);
    return i - ctx->pos.col;
}

state_proc_t make_lang_states[] = {
    [STATE_START] = make_state_start,
    [MAKE_DEFAULT] = make_state_default,
    [MAKE_VARIABLE_BEGIN] = make_state_variable_begin,
    [MAKE_VARIABLE] = make_state_variable,
};
