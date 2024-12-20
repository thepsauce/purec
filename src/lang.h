#ifndef LANG_H
#define LANG_H

#include "buf.h"

#define NO_LANG     0
#define C_LANG      1
#define DIFF_LANG   2
#define COMMIT_LANG 3
#define MAKE_LANG   4
#define NUM_LANGS   5

#define STATE_NULL      0
#define STATE_START     1

/// if the state always wants to continue to the next line
#define FSTATE_FORCE_MULTI 0x80000000
/// if the state wants to continue to the next line only if a condition is met
#define FSTATE_MULTI 0x40000000

struct state_ctx {
    /// the buffer that is being highlighted
    struct buf *buf;
    /// the current position in the buffer
    struct pos pos;
    /// the current highlight group
    unsigned hi;
    /// the current state
    unsigned state;
    /// the current line
    char *s;
    /// the length of the current line
    col_t n;
};

/**
 * A state procedures returns how many characters were consumed.
 *
 * It may modify `ctx->hi` to set the highlight for the consumed characters.
 */
typedef col_t (*state_proc_t)(struct state_ctx *ctx);

/**
 * Called before text is typed into a buffer.
 */
typedef void (*char_hook_t)(struct buf *buf, struct pos *pos, int c);

/**
 * An indentor for a language computes the indentation given line would need.
 */
typedef col_t (*indentor_t)(struct buf *buf, line_t line_i);

/**
 * Defined in "render_frame.c".
 */
extern struct lang {
    /// name of the language
    const char *name;
    /// state machine of the language
    state_proc_t *fsm;
    //// the character type hook
    char_hook_t char_hook;
    /// indentation computer
    indentor_t indentor;
    /// extensions a file for this language as null terminated list
    const char *file_exts;
} Langs[NUM_LANGS];

#endif
