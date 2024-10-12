#define MAKE_STATE_SHELL 2

size_t make_indentor(struct buf *buf, size_t line_i)
{
    (void) buf;
    (void) line_i;
    return 0;
}

size_t make_state_start(struct state_ctx *ctx)
{
    ctx->hi = HI_NORMAL;
    return 1;
}

state_proc_t make_lang_states[] = {
    [STATE_START & 0xff] = make_state_start,
};
