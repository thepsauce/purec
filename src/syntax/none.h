size_t none_state_start(struct state_ctx *ctx)
{
    ctx->hi = HI_NORMAL;
    return ctx->n;
}

state_proc_t none_lang_states[] = {
    [STATE_START] = none_state_start,
};
