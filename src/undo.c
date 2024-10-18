#include "buf.h"
#include "frame.h"
#include "xalloc.h"

#include <string.h>

struct undo Undo;

bool should_join(const struct undo_event *ev1, const struct undo_event *ev2)
{
    if (((ev1->flags | ev2->flags) & (IS_REPLACE | IS_BLOCK))) {
        return false;
    }
    if ((ev1->flags & IS_INSERTION)) {
        if ((ev2->flags & IS_INSERTION)) {
            return is_in_range(&ev2->pos, &ev1->pos, &ev1->end);
        }
        return is_in_range(&ev2->pos, &ev1->pos, &ev1->end) &&
            is_in_range(&ev2->end, &ev1->pos, &ev1->end);
    }
    if ((ev2->flags & IS_INSERTION)) {
        return is_point_equal(&ev1->pos, &ev2->pos);
    }
    return is_point_equal(&ev1->pos, &ev2->pos) ||
        is_point_equal(&ev1->pos, &ev2->end);
}

/**
 * Compares an already loaded segment with an existing segment.
 *
 * @param a A loaded undo segment.
 * @param b An unloaded undo segment.
 *
 * @return The comparison result.
 */
static int compare_segments(struct undo_seg *a, struct undo_seg *b)
{
    int cmp;

    if (a->data_len < b->data_len) {
        return -1;
    }

    if (a->data_len > b->data_len) {
        return 1;
    }

    load_undo_data(b);
    cmp = memcmp(a->data, b->data, b->data_len);
    unload_undo_data(b);

    return cmp;
}

struct undo_seg *save_lines(struct raw_line *lines, line_t num_lines)
{
    struct undo_seg new_seg;
    line_t          i;
    col_t           j;
    char            *new_s;
    size_t          l, m, r;
    int             cmp;
    time_t          cur_time;
    struct tm       *tm;
    struct undo_seg *p_seg;
    char            *name;

    new_seg.data = lines[0].s;
    new_seg.data_len = lines[0].n;

    for (i = 1; i < num_lines; i++) {
        new_seg.data_len += 1 + lines[i].n;
    }

    new_seg.data = xrealloc(new_seg.data, new_seg.data_len);
    lines[0].s = new_seg.data;
    for (i = 1, j = lines[0].n; i < num_lines; i++) {
        new_seg.data[j++] = '\n';

        new_s = memcpy(&new_seg.data[j], lines[i].s, lines[i].n);
        j += lines[i].n;

        free(lines[i].s);

        lines[i].s = new_s;
    }

    new_seg.lines = lines;
    new_seg.num_lines = num_lines;
    new_seg.load_count = 0;

    l = 0;
    r = Undo.num_segments;
    while (l < r) {
        m = (l + r) / 2;

        cmp = compare_segments(&new_seg, Undo.segments[m]);

        if (cmp == 0) {
            /* do not need these anymore, already stored this information */
            free(new_seg.data);
            free(new_seg.lines);
            return Undo.segments[m];
        }

        if (cmp > 0) {
            l = m + 1;
        } else {
            r = m;
        }
    }

    if (Undo.num_segments + 1 > Undo.a_segments) {
        Undo.a_segments *= 2;
        Undo.a_segments++;
        Undo.segments = xreallocarray(Undo.segments, Undo.a_segments,
                sizeof(*Undo.segments));
    }

    if (new_seg.data_len > HUGE_UNDO_THRESHOLD) {
        if (Undo.fp == NULL) {
            cur_time = time(NULL);
            tm = localtime(&cur_time);

            name = xasprintf("/tmp/undo_data_%04d-%02d-%02d_%02d-%02d-%02d",
                    Core.cache_dir,
                    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                    tm->tm_hour, tm->tm_min, tm->tm_sec);
            Undo.fp = fopen(name, "w+");
            free(name);
        }
        fseek(Undo.fp, 0, SEEK_END);
        fgetpos(Undo.fp, &new_seg.file_pos);
        fwrite(new_seg.data, 1, new_seg.data_len, Undo.fp);
        for (i = 0; i < new_seg.num_lines; i++) {
            new_seg.lines[i].s -= (size_t) new_seg.data;
        }
        free(new_seg.data);
        new_seg.data = NULL;
    }

    p_seg = xmemdup(&new_seg, sizeof(new_seg));

    memmove(&Undo.segments[l + 1], &Undo.segments[l],
            sizeof(*Undo.segments) * (Undo.num_segments - l));
    Undo.segments[l] = p_seg;
    Undo.num_segments++;

    return p_seg;
}

void load_undo_data(struct undo_seg *seg)
{
    line_t          i;

    seg->load_count++;
    if (seg->data == NULL) {
        seg->data = xmalloc(seg->data_len);

        for (i = 0; i < seg->num_lines; i++) {
            seg->lines[i].s += (size_t) seg->data;
        }

        fsetpos(Undo.fp, &seg->file_pos);
        fread(seg->data, 1, seg->data_len, Undo.fp);
    }
}

void unload_undo_data(struct undo_seg *seg)
{
    line_t          i;

    seg->load_count--;
    if (seg->data_len > HUGE_UNDO_THRESHOLD && seg->load_count == 0) {
        for (i = 0; i < seg->num_lines; i++) {
            seg->lines[i].s -= (size_t) seg->data;
        }
        free(seg->data);
        seg->data = NULL;
    }
}

struct undo_event *add_event(struct buf *buf, int flags, const struct pos *pos,
        struct raw_line *lines, line_t num_lines)
{
    struct undo_event *ev;
    col_t           max_n;
    line_t          i;

    if (buf->event_i + 1 >= buf->a_events) {
        buf->a_events *= 2;
        buf->a_events++;
        buf->events = xreallocarray(buf->events, buf->a_events,
                sizeof(*buf->events));
    }

    ev = &buf->events[buf->event_i++];
    buf->num_events = buf->event_i;

    /* all events are flagged as `IS_TRANSIENT`,
     * when receiving input has finished, this flag is removed
     * for the last event in the event list of a buffer
     */
    ev->flags = flags | IS_TRANSIENT;
    ev->pos = *pos;
    if ((flags & IS_BLOCK)) {
        ev->end.line = ev->pos.line + num_lines - 1;
        max_n = 0;
        for (i = 0; i < num_lines; i++) {
            max_n = MAX(max_n, lines[i].n);
        }
        ev->end.col = ev->pos.col + max_n - 1;
    } else {
        ev->end.line = ev->pos.line + num_lines - 1;
        ev->end.col = lines[num_lines - 1].n;
        if (ev->end.line == ev->pos.line) {
            ev->end.col += ev->pos.col;
        }
    }
    ev->time = time(NULL);
    ev->seg = save_lines(lines, num_lines);
    ev->cur = SelFrame->cur;
    return ev;
}

static void do_event(struct buf *buf, const struct undo_event *ev, int flags)
{
    struct undo_seg *seg;
    struct line     *line;
    line_t          i;
    col_t           j;
    bool            r;

    if ((flags & IS_DELETION)) {
        if ((flags & IS_BLOCK)) {
            _delete_block(buf, &ev->pos, &ev->end);
        } else {
            _delete_range(buf, &ev->pos, &ev->end);
        }
        return;
    }

    seg = ev->seg;
    load_undo_data(seg);
    if ((flags & IS_REPLACE)) {
        line = &buf->lines[ev->pos.line];
        for (j = 0; j < seg->lines[0].n; j++) {
            line->s[ev->pos.col + j] ^= seg->lines[0].s[j];
        }
        for (i = 1; i < seg->num_lines; i++) {
            line = &buf->lines[ev->pos.line + i];
            for (j = 0; j < seg->lines[i].n; j++) {
                line->s[j] ^= seg->lines[i].s[j];
            }
        }

        r = false;
        for (i = 0; i < seg->num_lines; i++) {
            r = rehighlight_line(buf, ev->pos.line + i);
        }
        while (r) {
            r = rehighlight_line(buf, ev->pos.line + i);
            i++;
        }
    } else {
        if ((flags & IS_BLOCK)) {
            _insert_block(buf, &ev->pos, seg->lines, seg->num_lines);
        } else {
            _insert_lines(buf, &ev->pos, seg->lines, seg->num_lines);
        }
    }
    unload_undo_data(seg);
}

struct undo_event *undo_event_no_trans(struct buf *buf)
{
    struct undo_event *ev;
    int flags;

    if (buf->event_i == 0) {
        return NULL;
    }

    ev = &buf->events[--buf->event_i];
    /* reverse the insertion/deletion flags to undo */
    flags = ev->flags;
    if ((flags & (IS_INSERTION | IS_DELETION))) {
        flags ^= (IS_INSERTION | IS_DELETION);
    }
    do_event(buf, ev, flags);
    return ev;
}

struct undo_event *undo_event(struct buf *buf)
{
    struct undo_event *ev;
    int flags;

    if (buf->event_i == 0) {
        return NULL;
    }

    do {
        ev = &buf->events[--buf->event_i];
        /* reverse the insertion/deletion flags to undo */
        flags = ev->flags;
        if ((flags & (IS_INSERTION | IS_DELETION))) {
            flags ^= (IS_INSERTION | IS_DELETION);
        }
        do_event(buf, ev, flags);
    } while (buf->event_i > 0 &&
            (buf->events[buf->event_i - 1].flags & IS_TRANSIENT));
    return ev;
}

struct undo_event *redo_event(struct buf *buf)
{
    struct undo_event *ev;

    if (buf->event_i == buf->num_events) {
        return NULL;
    }

    do {
        ev = &buf->events[buf->event_i++];
        do_event(buf, ev, ev->flags);
    } while ((ev->flags & IS_TRANSIENT) && buf->event_i != buf->num_events);
    return ev;
}
