#ifndef LANG_H
#define LANG_H

#include "buf.h"

#define NO_LANG     0
#define C_LANG      1
#define DIFF_LANG   2
#define NUM_LANGS   3

struct state_ctx {
    /// the buffer that is being highlighted
    struct buf *buf;
    /// the current highlight group
    unsigned hi;
    /// the current state
    unsigned state;
    /// the current line
    char *s;
    /// the index on the current line
    size_t i;
    /// the length of the current line
    size_t n;
};

/**
 * A state procedures returns how many characters were consumed.
 *
 * It may modify `ctx->hi` to set the highlight for the consumed characters.
 */
typedef size_t (*state_proc_t)(struct state_ctx *ctx);

extern struct lang {
    /// name of the language
    const char *name;
    /// state machine of the language
    state_proc_t *fsm;
    /// extensions a file for this language as null terminated list
    const char *file_exts;
} Langs[NUM_LANGS];

#endif
