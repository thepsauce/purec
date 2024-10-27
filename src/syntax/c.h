#define C_STATE_COMMENT                 2
#define C_STATE_MULTI_COMMENT           (FSTATE_FORCE_MULTI|3)
#define C_STATE_STRING                  4
#define C_STATE_INCLUDE                 5
#define C_STATE_INCLUDE_STRING          6
#define C_STATE_INCLUDE_CORNER          7
#define C_STATE_PREPROC                 8
#define C_STATE_PREPROC_TRAIL           9

#define C_PAREN_STRING_MASK     0x100
#define C_PAREN_COMMENT_MASK    0x200
#define C_PAREN_MASK            0x300

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

const char *c_statements[] = {
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

col_t c_indentor(struct buf *buf, line_t line_i)
{
    struct pos      p;
    size_t          index, m_i;
    col_t           c;
    struct paren    *par;
    struct line     *line;
    col_t           i;

    if (line_i == 0) {
        return 0;
    }
    p.col = 0;
    p.line = line_i;
    index = get_next_paren_index(buf, &p);

    while (index > 0 && !(buf->parens[index - 1].type & FOPEN_PAREN)) {
        m_i = get_matching_paren(buf, index - 1);
        if (m_i == SIZE_MAX) {
            index--;
        } else {
            index = m_i;
        }
    }

    if (index == 0) {
        return 0;
    }

    par = &buf->parens[index - 1];
    line = &buf->text.lines[par->pos.line];
    if (par->pos.col + 1 != line->n) {
        return par->pos.col + 1;
    }

    switch ((par->type & 0xff)) {
    case '{':
        if (index <= 1 || par[-1].type != '(') {
            break;
        }
        index = get_matching_paren(buf, index - 2);
        if (index == SIZE_MAX) {
            break;
        }
        par = &buf->parens[index];
        break;
    }

    line = &buf->text.lines[line_i];
    for (i = 1; i < line->n; i++) {
        if (buf->attribs[line_i][i] == HI_OPERATOR) {
            if (line->s[i - 1] != ' ' && line->s[i] == ':') {
                return get_line_indent(buf, par->pos.line);
            }
        }
    }
    c = get_line_indent(buf, line_i);
    if (c == line->n || line->s[c] != '}') {
        c = 1;
    } else {
        c = 0;
    }
    return Core.tab_size * c + get_line_indent(buf, par->pos.line);
}

static col_t c_get_identf(struct state_ctx *ctx)
{
    char ch;
    col_t n;

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
        } else if (bin_search(c_statements, ARRAY_SIZE(c_statements),
                    &ctx->s[ctx->pos.col], n) != NULL) {
            ctx->hi = HI_STATEMENT;
        } else if (ctx->pos.col + n < ctx->n && ctx->s[ctx->pos.col + n] == '(') {
            ctx->hi = HI_FUNCTION;
        }
    } else {
        n = 0;
    }
    return n;
}

static col_t c_get_number(struct state_ctx *ctx)
{
    col_t n = 0;

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

static col_t c_read_escapist(struct state_ctx *ctx, col_t i)
{
    col_t hex_l;

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

static col_t c_get_char(struct state_ctx *ctx)
{
    col_t n;
    col_t e;

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

static inline void check_paren(struct state_ctx *ctx, col_t i, int flags)
{
    struct pos pos;

    switch (ctx->s[i]) {
    case '(':
    case '{':
    case '[':
        pos.col = i;
        pos.line = ctx->pos.line;
        add_paren(ctx->buf, &pos, FOPEN_PAREN | flags | ctx->s[i]);
        break;;

    case ')':
    case '}':
    case ']':
        pos.col = i;
        pos.line = ctx->pos.line;
        add_paren(ctx->buf, &pos, flags | (ctx->s[i] == ')' ? '(' :
                ctx->s[i] == '}' ? '{' : '['));
        break;
    }
}

col_t c_state_string(struct state_ctx *ctx)
{
    col_t n;

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

        check_paren(ctx, ctx->pos.col + n, C_PAREN_STRING_MASK);
    }
    return n;
}

col_t c_state_include(struct state_ctx *ctx)
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

col_t c_state_include_corner(struct state_ctx *ctx)
{
    col_t n;

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

col_t c_state_include_string(struct state_ctx *ctx)
{
    col_t n;

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

col_t c_state_preproc_trail(struct state_ctx *ctx)
{
    ctx->hi = HI_ERROR;
    return ctx->n - ctx->pos.col;
}

col_t c_state_common(struct state_ctx *ctx)
{
    col_t len;

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
                add_paren(ctx->buf, &ctx->pos, FOPEN_PAREN | '*');
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

    default:
        check_paren(ctx, ctx->pos.col, 0);
    }
    ctx->hi = HI_NORMAL;
    return 1;
}

col_t c_state_preproc(struct state_ctx *ctx)
{
    if (ctx->pos.col + 1 == ctx->n && ctx->s[ctx->pos.col] == '\\') {
        ctx->hi = HI_PREPROC;
        ctx->state |= FSTATE_MULTI;
        return 1;
    }
    ctx->hi = HI_PREPROC;
    return c_state_common(ctx);
}

col_t c_state_start(struct state_ctx *ctx)
{
    col_t i;
    col_t w_i;

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

col_t c_state_comment(struct state_ctx *ctx)
{
    if (ctx->s[ctx->n - 1] == '\\') {
        ctx->state |= FSTATE_MULTI;
    }
    ctx->hi = HI_COMMENT;
    return ctx->n - ctx->pos.col;
}

col_t c_state_multi_comment(struct state_ctx *ctx)
{
    col_t i;

    ctx->hi = HI_COMMENT;
    switch (ctx->s[ctx->pos.col]) {
    case 'F':
        if (ctx->pos.col + 5 < ctx->n &&
                memcmp(&ctx->s[ctx->pos.col + 1], "IXME:", 5) == 0) {
            ctx->hi = HI_TODO;
            return 6;
        }
        break;

    case 'T':
        if (ctx->pos.col + 4 < ctx->n &&
                memcmp(&ctx->s[ctx->pos.col + 1], "ODO:", 4) == 0) {
            ctx->hi = HI_TODO;
            return 5;
        }
        break;

    case 'X':
        if (ctx->pos.col + 3 < ctx->n &&
                memcmp(&ctx->s[ctx->pos.col + 1], "XX:", 3) == 0) {
            ctx->hi = HI_TODO;
            return 4;
        }
        break;

    case '/':
        if (ctx->pos.col == 0 || ctx->s[ctx->pos.col - 1] != '*') {
            break;
        }
        /* pop state */
        ctx->state ^= FSTATE_MULTI | FSTATE_FORCE_MULTI;
        ctx->state >>= 8;
        add_paren(ctx->buf, &ctx->pos, '*');
        break;

    case '@':
        for (i = ctx->pos.col + 1; i < ctx->n; i++) {
            if (!isalpha(ctx->s[i])) {
                break;
            }
        }
        ctx->hi = HI_JAVADOC;
        return i - ctx->pos.col;

    default:
        check_paren(ctx, ctx->pos.col, C_PAREN_COMMENT_MASK);
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
