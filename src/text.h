#ifndef TEXT_H
#define TEXT_H

#include "util.h"
#include "purec.h"

#include <stdlib.h>
#include <stdio.h>

struct line {
    /// data of the line in utf8 format
    char *s;
    /// number of bytes on this line
    col_t n;
};

/**
 * Initialize a line by setting its data to given string.
 *
 * @param l     Line to initialize.
 * @param str   String to set the line to, can be `NULL`.
 * @param nstr  Length of given string.
 */
#define init_line(l, str, nstr) do { \
    struct line *const _l = (l); \
    const char *const _s = (str); \
    const int _n = (nstr); \
    _l->n = _n; \
    _l->s = xmalloc(_n); \
    if (_s != NULL) { \
        memcpy(_l->s, _s, _n); \
    } \
} while (0)

#define init_zero_line(l) do { \
    struct line *const _l = (l); \
    _l->n = 0; \
    _l->s = NULL; \
} while (0)

struct text {
    /// the lines of text
    struct line *lines;
    /// the number of lines
    line_t num_lines;
    /// the number of allocated lines
    line_t a_lines;
};

void init_text(struct text *text, size_t num_lines);
void clear_text(struct text *text);
void make_text(struct text *text, struct line *lines, line_t num_lines);
void str_to_text(const char *str, size_t len, struct text *text);
char *text_to_str(struct text *text, size_t *p_len);
struct file_rule;
size_t read_text(FILE *fp, struct file_rule *rule, struct text *text,
                 line_t max_lines);
size_t write_text(FILE *fp, const struct file_rule *rule,
                  const struct text *text,
                  line_t from, line_t to);
bool clip_range(const struct text *text,
                const struct pos *from, const struct pos *to,
                struct pos *d_from, struct pos *d_to);
bool clip_block(const struct text *text,
                const struct pos *from, const struct pos *to,
                struct pos *d_from, struct pos *d_to);
struct line *insert_blank(struct text *text, line_t line, line_t num_lines);
void insert_text(struct text *text,
                 const struct pos *pos,
                 const struct text *src);
void insert_text_block(struct text *text,
                       const struct pos *pos,
                       const struct text *src);
void get_text(const struct text *text,
              const struct pos *from,
              const struct pos *to,
              struct text *dest);
void get_text_block(const struct text *text,
               const struct pos *from,
               const struct pos *to,
               struct text *dest);
void delete_text(struct text *text,
                 const struct pos *from,
                 const struct pos *to);
void delete_text_block(struct text *text,
                 const struct pos *from,
                 const struct pos *to);
void repeat_text(struct text *text, const struct text *src, size_t count);
void repeat_text_block(struct text *text, const struct text *src, size_t count);

#endif
