#define C_STATE_COMMENT                 2
#define C_STATE_MULTI_COMMENT           (FSTATE_FORCE_MULTI|3)
#define C_STATE_STRING                  4
#define C_STATE_INCLUDE                 5
#define C_STATE_INCLUDE_STRING          6
#define C_STATE_INCLUDE_CORNER          7
#define C_STATE_PREPROC                 8
#define C_STATE_PREPROC_TRAIL           9

const char *c_types[] = {
    "FILE",
    "bool",
    "char",
    "double",
    "float",
    "int",
    "long",
    "short",
    "signed",
    "typedef",
    "typeof",
    "unsigned",
    "void",
};

const char *c_type_mods[] =  {
    "auto",
    "const",
    "enum",
    "extern",
    "inline",
    "register",
    "static",
    "struct",
    "union",
    "volatile",
};

const char *c_identfs[] = {
    "break",
    "case",
    "continue",
    "default",
    "do",
    "else",
    "for",
    "goto",
    "if",
    "return",
    "sizeof",
    "switch",
    "while",
};

const char *c_preproc[] = {
    "define",
    "elif",
    "else",
    "endif",
    "error",
    "ident",
    "if",
    "ifdef",
    "ifndef",
    "include",
    "include_next",
    "line",
    "pragma",
    "sccs",
    "undef",
    "warn",
    "warning",
};

static size_t c_get_identf(struct state_ctx *ctx)
{
    char ch;
    size_t n;

    ch = ctx->s[ctx->pos.col];
    if (isalpha(ch) || ch == '_') {
        for (n = 1; ctx->pos.col + n < ctx->n; n++) {
            ch = ctx->s[ctx->pos.col + n];
            if (!isalnum(ch) && ch != '_') {
                break;
            }
        }

        if (n > 3 && ctx->s[ctx->pos.col + n - 2] == '_' &&
                ctx->s[ctx->pos.col + n - 1] == 't') {
            ctx->hi = HI_TYPE;
        } else if (bin_search(c_types, ARRAY_SIZE(c_types),
                    &ctx->s[ctx->pos.col], n) != NULL) {
            ctx->hi = HI_TYPE;
        } else if (bin_search(c_type_mods, ARRAY_SIZE(c_type_mods),
                    &ctx->s[ctx->pos.col], n) != NULL) {
            ctx->hi = HI_TYPE_MOD;
        } else if (bin_search(c_identfs, ARRAY_SIZE(c_identfs),
                    &ctx->s[ctx->pos.col], n) != NULL) {
            ctx->hi = HI_IDENTIFIER;
        } else if (ctx->pos.col + n < ctx->n && ctx->s[ctx->pos.col + n] == '(') {
            ctx->hi = HI_FUNCTION;
        }
    } else {
        n = 0;
    }
    return n;
}

static size_t c_get_number(struct state_ctx *ctx)
{
    size_t n = 0;

    if (isdigit(ctx->s[ctx->pos.col]) || (ctx->pos.col + 1 != ctx->n &&
                ctx->s[ctx->pos.col] == '.' &&
            isdigit(ctx->s[ctx->pos.col + 1]))) {
        for (n = 1; ctx->pos.col + n < ctx->n; n++) {
            if (ctx->s[ctx->pos.col + n] == '.') {
                continue;
            }
            if (!isalnum(ctx->s[ctx->pos.col + n])) {
                break;
            }
        }
        ctx->hi = HI_NUMBER;
    } else {
        n = 0;
    }
    return n;
}

static size_t c_read_escapist(struct state_ctx *ctx, size_t i)
{
    size_t hex_l;

    switch (ctx->s[ctx->pos.col + i]) {
    case 'a':
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't':
    case 'v':
    case '\\':
    case '\'':
    case '\"':
    case '0':
        return i + 1;

    case 'x':
        hex_l = 2;
        break;

    case 'u':
        hex_l = 4;
        break;

    case 'U':
        hex_l = 8;
        break;

    default:
        return 0;
    }

    for (i++; hex_l > 0 && ctx->pos.col + i < ctx->n; hex_l--, i++) {
        if (!isxdigit(ctx->s[ctx->pos.col + i])) {
            return 0;
        }
    }
    if (hex_l > 0) {
        return 0;
    }
    return i;
}

static size_t c_get_char(struct state_ctx *ctx)
{
    size_t n;
    size_t e;

    if (ctx->s[ctx->pos.col] != '\'') {
        return 0;
    }

    n = 1;
    if (ctx->pos.col + n == ctx->n) {
        return 1;
    }

    if (ctx->s[ctx->pos.col + n] == '\\') {
        n++;
        if (ctx->pos.col + n == ctx->n) {
            ctx->hi = HI_NORMAL;
            return 2;
        }
        e = c_read_escapist(ctx, n);
        if (e == 0) {
            ctx->hi = HI_NORMAL;
            return 2;
        }
        n = e;
        ctx->hi = HI_CHAR;
    } else {
        n++;
    }

    if (ctx->pos.col + n == ctx->n || ctx->s[ctx->pos.col + n] != '\'') {
        ctx->hi = HI_NORMAL;
        return n;
    }

    ctx->hi = HI_CHAR;
    return n + 1;
}

size_t c_state_string(struct state_ctx *ctx)
{
    size_t n;

    ctx->hi = HI_STRING;
    for (n = 0; ctx->pos.col + n < ctx->n; n++) {
        if (ctx->s[ctx->pos.col + n] == '\\') {
            if (n != 0) {
                /* return the part of the string before the \ */
                return n;
            }
            if (ctx->pos.col + 1 == ctx->n) {
                ctx->hi = HI_CHAR;
                ctx->state |= FSTATE_MULTI;
                return 1;
            }

            n = c_read_escapist(ctx, 1);
            if (n == 0) {
                ctx->hi = HI_NORMAL;
                return 2;
            }
            ctx->hi = HI_CHAR;
            return n;
        }

        if (ctx->s[ctx->pos.col + n] == '\"') {
            ctx->state >>= 8;
            n++;
            break;
        }
    }
    return n;
}

size_t c_state_include(struct state_ctx *ctx)
{
    switch (ctx->s[ctx->pos.col]) {
    case '\t':
    case ' ':
        ctx->hi = HI_NORMAL;
        return 1;

    case '<':
        ctx->state = C_STATE_INCLUDE_CORNER;
        ctx->hi = HI_STRING;
        return 1;

    case '\"':
        ctx->state = C_STATE_INCLUDE_STRING;
        ctx->hi = HI_STRING;
        return 1;

    default:
        ctx->hi = HI_ERROR;
        return ctx->n - ctx->pos.col;
    }
}

size_t c_state_include_corner(struct state_ctx *ctx)
{
    size_t n;

    ctx->hi = HI_STRING;
    for (n = 0; ctx->pos.col + n < ctx->n; n++) {
        if (ctx->s[ctx->pos.col + n] == '\\') {
            if (n != 0) {
                return n;
            }
            if (ctx->pos.col + 1 == ctx->n) {
                ctx->hi = HI_CHAR;
                ctx->state |= FSTATE_MULTI;
            }
            return 1;
        }

        if (ctx->s[ctx->pos.col + n] == '>') {
            ctx->state = C_STATE_PREPROC_TRAIL;
            n++;
            break;
        }
    }
    return n;
}

size_t c_state_include_string(struct state_ctx *ctx)
{
    size_t n;

    ctx->hi = HI_STRING;
    for (n = 0; ctx->pos.col + n < ctx->n; n++) {
        if (ctx->s[ctx->pos.col + n] == '\\') {
            if (n != 0) {
                return n;
            }
            if (ctx->pos.col + 1 == ctx->n) {
                ctx->hi = HI_CHAR;
                ctx->state |= FSTATE_MULTI;
            }
            return 1;
        }

        if (ctx->s[ctx->pos.col + n] == '\"') {
            ctx->state = C_STATE_PREPROC_TRAIL;
            n++;
            break;
        }
    }
    return n;
}

size_t c_state_preproc_trail(struct state_ctx *ctx)
{
    ctx->hi = HI_ERROR;
    return ctx->n - ctx->pos.col;
}

size_t c_state_common(struct state_ctx *ctx)
{
    size_t len;

    len = c_get_identf(ctx);
    if (len > 0) {
        return len;
    }

    len = c_get_number(ctx);
    if (len > 0) {
        return len;
    }

    len = c_get_char(ctx);
    if (len > 0) {
        return len;
    }

    switch (ctx->s[ctx->pos.col]) {
    case '\t':
    case ' ':
        for (len = 1; ctx->pos.col + len < ctx->n; len++) {
            if (!isblank(ctx->s[ctx->pos.col + len])) {
                break;
            }
        }
        ctx->hi = HI_NORMAL;
        return len;

    case '\"':
        /* push state */
        ctx->state <<= 8;
        ctx->state |= C_STATE_STRING;
        ctx->hi = HI_STRING;
        return 1;

    case '/':
        if (ctx->pos.col + 1 < ctx->n) {
            if (ctx->s[ctx->pos.col + 1] == '/') {
                ctx->state = C_STATE_COMMENT;
                return 0;
            }
            if (ctx->s[ctx->pos.col + 1] == '*') {
                /* push state */
                ctx->state <<= 8;
                ctx->state |= C_STATE_MULTI_COMMENT;
                return 0;
            }
        }
        /* fall through */
    case '!':
    case '%':
    case '&':
    case '*':
    case '+':
    case '-':
    case '.':
    case ':':
    case '<':
    case '=':
    case '>':
    case '?':
    case '^':
    case '|':
    case '~':
        ctx->hi = HI_OPERATOR;
        return 1;

    case '(':
    case '{':
    case '[':
        ctx->hi = HI_NORMAL;
        add_paren(ctx->buf, &ctx->pos, ctx->s[ctx->pos.col] | FOPEN_PAREN);
        return 1;

    case ')':
    case '}':
    case ']':
        ctx->hi = HI_NORMAL;
        add_paren(ctx->buf, &ctx->pos, ctx->s[ctx->pos.col] == ')' ?
                '(' : ctx->s[ctx->pos.col] == '}' ?
                '{' : '[');
        return 1;
    }
    return 1;
}

size_t c_state_preproc(struct state_ctx *ctx)
{
    if (ctx->pos.col + 1 == ctx->n && ctx->s[ctx->pos.col] == '\\') {
        ctx->hi = HI_PREPROC;
        ctx->state |= FSTATE_MULTI;
        return 1;
    }
    ctx->hi = HI_PREPROC;
    return c_state_common(ctx);
}

size_t c_state_start(struct state_ctx *ctx)
{
    size_t i;
    size_t w_i;

    if (ctx->s[ctx->pos.col] == '#') {
        /* skip all space after '#' */
        for (i = ctx->pos.col + 1; i < ctx->n; i++) {
            if (!isblank(ctx->s[i])) {
                break;
            }
        }

        /* read word after '#' */
        for (w_i = i; i < ctx->n; i++) {
            if (!isalpha(ctx->s[i])) {
                break;
            }
        }

        ctx->hi = HI_PREPROC;
        /* check if it is valid */
        if (bin_search(c_preproc, ARRAY_SIZE(c_preproc),
                    &ctx->s[w_i], i - w_i) != NULL) {
            /* special handling for include */
            if (ctx->s[w_i] == 'i' && ctx->s[w_i + 1] == 'n') {
                ctx->state = C_STATE_INCLUDE;
            } else {
                ctx->state = C_STATE_PREPROC;
            }
        } else if (w_i != i) {
            ctx->hi = HI_NORMAL;
        }
        return i - ctx->pos.col;
    }
    ctx->hi = HI_NORMAL;
    return c_state_common(ctx);
}

size_t c_state_comment(struct state_ctx *ctx)
{
    if (ctx->s[ctx->n - 1] == '\\') {
        ctx->state |= FSTATE_MULTI;
    }
    ctx->hi = HI_COMMENT;
    return ctx->n - ctx->pos.col;
}

size_t c_state_multi_comment(struct state_ctx *ctx)
{
    size_t i;

    ctx->hi = HI_COMMENT;
    if (ctx->pos.col + 1 < ctx->n && ctx->s[ctx->pos.col] == '*' &&
            ctx->s[ctx->pos.col + 1] == '/') {
        /* pop state */
        ctx->state ^= FSTATE_MULTI;
        ctx->state >>= 8;
        return 2;
    }
    if (ctx->s[ctx->pos.col] == '@') {
        for (i = ctx->pos.col + 1; i < ctx->n; i++) {
            if (!isalpha(ctx->s[i])) {
                break;
            }
        }
        ctx->hi = HI_JAVADOC;
        return i - ctx->pos.col;
    }
    return 1;
}
state_proc_t c_lang_states[] = {
    [STATE_START & 0xff] = c_state_start,
    [C_STATE_COMMENT & 0xff] = c_state_comment,
    [C_STATE_MULTI_COMMENT & 0xff] = c_state_multi_comment,
    [C_STATE_STRING & 0xff] = c_state_string,
    [C_STATE_INCLUDE & 0xff] = c_state_include,
    [C_STATE_INCLUDE_STRING & 0xff] = c_state_include_string,
    [C_STATE_INCLUDE_CORNER & 0xff] = c_state_include_corner,
    [C_STATE_PREPROC & 0xff] = c_state_preproc,
    [C_STATE_PREPROC_TRAIL & 0xff] = c_state_preproc_trail,
};
