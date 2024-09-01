size_t diff_state_start(struct state_ctx *ctx)
{
    unsigned n;

    if (ctx->i > 0) {
        ctx->hi = HI_NORMAL;
        return ctx->n - ctx->i;
    }

    switch (ctx->s[0]) {
    case '#':
        ctx->hi = HI_COMMENT;
        break;

    case '+':
        ctx->hi = HI_ADDED;
        break;

    case '*':
    case '-':
        ctx->hi = HI_REMOVED;
        break;

    case '>':
        ctx->hi = HI_CHANGED;
        break;

    case '<':
        ctx->hi = HI_CHANGED;
        break;

    /* including all upper case letters here make it so notes are highlighted:
     * Only in ...
     * Binary files ... and ... differ
     * and more
     */
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
    case 'd': /* diff */
    case '\\':
    case '=':
        ctx->hi = HI_TYPE;
        break;

    case 'i': /* index */
        ctx->hi = HI_TYPE_MOD;
        break;

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        ctx->hi = HI_OPERATOR;
        break;

    case '@': /* @@ .,. .,. @@ */
        if (ctx->n >= 4 && ctx->s[1] == '@') {
            ctx->hi = HI_OPERATOR;
            for (n = 2; n < MIN(ctx->n - 1, 80); n++) {
                if (ctx->s[n] == '@' && ctx->s[n + 1] == '@') {
                    n += 2;
                    break;
                }
            }
        } else {
            ctx->hi = HI_ERROR;
            return ctx->n;
        }
        return n;

    case ' ':
        ctx->hi = HI_NORMAL;
        break;

    default:
        ctx->hi = HI_NORMAL;
        break;
    }
    return ctx->n;
}

state_proc_t diff_lang_states[] = {
    [STATE_START] = diff_state_start,
};
