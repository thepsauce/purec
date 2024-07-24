#include "buf.h"
#include "xalloc.h"

#include <stdio.h>
#include <string.h>

static struct line *insert_line(struct buf *buf, size_t line_i, size_t num_lines)
{
    if (buf->num_lines + num_lines > buf->a_lines) {
        buf->a_lines *= 2;
        buf->a_lines += num_lines;
        buf->lines = xreallocarray(buf->lines, buf->a_lines, sizeof(*buf->lines));
    }
    memmove(&buf->lines[line_i + num_lines], &buf->lines[line_i],
            sizeof(*buf->lines) * (buf->num_lines - line_i));
    buf->num_lines += num_lines;
    return &buf->lines[line_i];
}

static int reload_file(struct buf *buf)
{
    FILE *fp;
    char *s_line = NULL;
    size_t a_line = 0;
    ssize_t n_line;
    struct line *line;

    fp = fopen(buf->path, "r");
    if (fp == NULL) {
        printf("failed opening: %s\n", buf->path);
        return -1;
    }
    buf->num_lines = 0;
    while (n_line = getline(&s_line, &a_line, fp), n_line > 0) {
        if (s_line[n_line - 1] == '\n') {
            n_line--;
        }
        line = insert_line(buf, buf->num_lines, 1);
        line->s = xmalloc(n_line);
        memcpy(line->s, s_line, n_line);
        line->n = n_line;
    }
    free(s_line);
    return 0;
}

struct buf *create_buffer(const char *path)
{
    struct buf *buf;

    buf = xcalloc(1, sizeof(*buf));
    if (path != NULL) {
        buf->path = xstrdup(path);
        if (stat(path, &buf->st) == 0) {
            (void) reload_file(buf);
            return buf;
        }
    }
    buf->lines = xcalloc(1, sizeof(*buf->lines));
    buf->num_lines = 1;
    buf->a_lines = 1;
    return buf;
}

void delete_buffer(struct buf *buf)
{
    free(buf->path);
    for (size_t i = 0; i < buf->num_lines; i++) {
        free(buf->lines[i].s);
    }
    free(buf->lines);
    for (size_t i = 0; i < buf->num_events; i++) {
        free(buf->events[i].text);
    }
    free(buf->events);
    free(buf);
}
