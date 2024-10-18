#define COMMIT_STATE_SECOND 2
#define COMMIT_STATE_REST   3

col_t commit_state_start(struct state_ctx *ctx)
{
    switch (ctx->pos.line) {
    case 0:
        break;

    case 1:
        ctx->state = COMMIT_STATE_SECOND;
        return 0;

    default:
        ctx->state = COMMIT_STATE_REST;
        return 0;
    }

    if (ctx->pos.col >= 50) {
        ctx->hi = HI_ERROR;
        return ctx->n - ctx->pos.col;
    }
    ctx->hi = HI_NORMAL;
    if (ctx->n <= 50) {
        return ctx->n;
    }
    return 50;
}

col_t commit_state_second(struct state_ctx *ctx)
{
    if (ctx->s[0] == '#') {
        ctx->state = COMMIT_STATE_REST;
        return 0;
    }
    ctx->hi = HI_ERROR;
    return ctx->n;
}

col_t commit_state_rest(struct state_ctx *ctx)
{
    col_t           n;

    for (n = 0; n < ctx->n; n++) {
        if (!isblank(ctx->s[n])) {
            break;
        }
    }
    if (n == ctx->n || ctx->s[n] != '#') {
        ctx->hi = HI_NORMAL;
        return ctx->n;
    }

    ctx->hi = HI_COMMENT;
    return ctx->n;
}

state_proc_t commit_lang_states[] = {
    [STATE_START] = commit_state_start,
    [COMMIT_STATE_SECOND] = commit_state_second,
    [COMMIT_STATE_REST] = commit_state_rest,
};
