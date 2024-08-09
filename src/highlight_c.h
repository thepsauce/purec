const char *c_types[] = {
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
    "FILE",
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
    "switch",
    "while",
};

static size_t c_get_identf(struct state_ctx *ctx)
{
    char ch;
    size_t n;

    ch = ctx->s[ctx->i];
    if (isalpha(ch) || ch == '_') {
        for (n = 1; ctx->i + n < ctx->n; n++) {
            ch = ctx->s[ctx->i + n];
            if (!isalnum(ch) && ch != '_') {
                break;
            }
        }

        if (n > 3 && ctx->s[ctx->i + n - 2] == '_' &&
                ctx->s[ctx->i + n - 1] == 't') {
            ctx->hi = HI_TYPE;
        } else if (bin_search(c_types, ARRAY_SIZE(c_types),
                    &ctx->s[ctx->i], n) != NULL) {
            ctx->hi = HI_TYPE;
        } else if (bin_search(c_type_mods, ARRAY_SIZE(c_type_mods),
                    &ctx->s[ctx->i], n) != NULL) {
            ctx->hi = HI_TYPE_MOD;
        } else if (bin_search(c_identfs, ARRAY_SIZE(c_identfs),
                    &ctx->s[ctx->i], n) != NULL) {
            ctx->hi = HI_IDENTIFIER;
        } else {
            ctx->hi = HI_NORMAL;
        }
    } else {
        n = 0;
    }
    return n;
}

static size_t c_get_number(struct state_ctx *ctx)
{
    size_t n = 0;

    if (isdigit(ctx->s[ctx->i]) || (ctx->i + 1 != ctx->n &&
                ctx->s[ctx->i] == '.' &&
            isdigit(ctx->s[ctx->i + 1]))) {
        for (n = 1; ctx->i + n < ctx->n; n++) {
            if (ctx->s[ctx->i + n] == '.') {
                continue;
            }
            if (!isalnum(ctx->s[ctx->i + n])) {
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

    switch (ctx->s[ctx->i + i]) {
    case 'a':
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't':
    case 'v':
    case '\\':
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

    for (i++; hex_l > 0 && ctx->i + i < ctx->n; hex_l--, i++) {
        if (!isxdigit(ctx->s[ctx->i + i])) {
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

    if (ctx->s[ctx->i] != '\'') {
        return 0;
    }

    n = 1;
    if (ctx->i + n == ctx->n) {
        return 1;
    }

    if (ctx->s[ctx->i + n] == '\\') {
        n++;
        if (ctx->i + n == ctx->n) {
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
        if (ctx->i + n == ctx->n) {
            ctx->hi = HI_NORMAL;
            return 2;
        }
    }

    if (ctx->s[ctx->i + n] != '\'') {
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
    for (n = 0; ctx->i + n < ctx->n; n++) {
        if (ctx->s[ctx->i + n] == '\\') {
            if (ctx->i + n + 1 == ctx->n) {
                ctx->hi = HI_CHAR;
                ctx->multi = true;
                return 1;
            }
            if (n == 0) {
                n = c_read_escapist(ctx, 1);
                if (n == 0) {
                    ctx->hi = HI_NORMAL;
                    return 2;
                }
                ctx->hi = HI_CHAR;
                return n;
            }
            /* return the part of the string before the \ */
            return n;
        }

        if (ctx->s[ctx->i + n] == '\"') {
            ctx->state = STATE_START;
            n++;
            break;
        }
    }
    return n;
}

size_t c_state_start(struct state_ctx *ctx)
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

    switch (ctx->s[ctx->i]) {
    case '\"':
        ctx->state = C_STATE_STRING;
        ctx->hi = HI_STRING;
        return 1;

    case '/':
        if (ctx->i + 1 == ctx->n) {
            break;
        }
        if (ctx->s[ctx->i + 1] == '/') {
            ctx->state = C_STATE_COMMENT;
            return 0;
        }
        if (ctx->s[ctx->i + 1] == '*') {
            ctx->state = C_STATE_MULTI_COMMENT;
            return 0;
        }
        break;
    }
    ctx->hi = HI_NORMAL;
    return 1;
}

size_t c_state_comment(struct state_ctx *ctx)
{
    if (ctx->s[ctx->n - 1] == '\\') {
        ctx->multi = true;
    }
    ctx->hi = HI_COMMENT;
    return ctx->n - ctx->i;
}

size_t c_state_multi_comment(struct state_ctx *ctx)
{
    size_t i;

    if (ctx->i + 1 != ctx->n && ctx->s[ctx->i] == '*' &&
            ctx->s[ctx->i + 1] == '/') {
        ctx->state = STATE_START;
        return 2;
    }
    if (ctx->s[ctx->i] == '@') {
        for (i = ctx->i + 1; i < ctx->n; i++) {
            if (!isalpha(ctx->s[i])) {
                break; }
        }
        ctx->hi = HI_JAVADOC;
        return i - ctx->i;
    }
    ctx->hi = HI_COMMENT;
    return 1;
}
