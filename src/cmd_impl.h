int cmd_buffer(struct cmd_data *cd)
{
    struct buf *buf;

    buf = get_buffer(cd->from);
    if (buf == NULL || buf == SelFrame->buf) {
        return -1;
    }
    set_frame_buffer(SelFrame, buf);
    return 0;
}

int cmd_bnext(struct cmd_data *cd)
{
    struct buf *buf;

    if (cd->from == 0) {
        cd->from = 1;
    }
    cd->from %= get_buffer_count();
    if (cd->from == 0) {
        return 0;
    }
    for (buf = SelFrame->buf; cd->from > 0; cd->from--) {
        buf = buf->next;
        if (buf == NULL) {
            buf = FirstBuffer;
        }
    }
    set_frame_buffer(SelFrame, buf);
    return 0;
}

int cmd_bprev(struct cmd_data *cd)
{
    struct buf *buf;

    if (cd->from == 0) {
        cd->from = 1;
    }
    cd->from %= get_buffer_count();
    if (cd->from == 0) {
        return 0;
    }
    for (buf = FirstBuffer; cd->from > 0; cd->from--) {
        while (buf->next != NULL && buf->next != SelFrame->buf) {
            buf = buf->next;
        }
    }
    set_frame_buffer(SelFrame, buf);
    return 0;
}

int cmd_colorscheme(struct cmd_data *cd)
{
    if (set_theme(cd->arg) == -1) {
        set_error("color scheme '%s' does not exist", cd->arg);
        return -1;
    }
    return 0;
}

int cmd_colorschemeindex(struct cmd_data *cd)
{
    int index;
    int num;

    if (cd->arg[0] != '\0') {
        index = -1;
        num = get_number_of_themes();
        for (int t = 0; t < num; t++) {
            if (strcasecmp(Themes[t].name, cd->arg) == 0) {
                index = t;
            }
        }
        if (index < 0) {
            return -1;
        }
    } else {
        index = Theme;
    }
    set_message("Theme index: %d", index);
    return 0;
}

/**
 * Saves a buffer using command meta data.
 *
 * @param cd    Command data.
 * @param buf   The buffer to save.
 *
 * @return 0 on success, -1 otherwise.
 */
static int save_buffer(struct cmd_data *cd, struct buf *buf)
{
    FILE *fp;
    const char *file;
    struct stat st;
    size_t num_bytes;

    file = cd->arg;

    if (file[0] == '\0') {
        if (buf->path == NULL) {
            set_error("no file name");
            return -1;
        }

        if ((cd->has_range || cd->has_number) && !cd->force) {
            set_error("use ! to write partial buffer");
            return -1;
        }

        file = buf->path;
        if (!cd->force && stat(file, &st) == 0) {
            if (st.st_mtime != buf->st.st_mtime) {
                set_error("file changed, use  :w!  to overwrite");
                return -1;
            }
        }
    } else if (buf->path == NULL) {
        buf->path = xstrdup(file);
        file = buf->path;
        if (buf->lang == NO_LANG) {
            buf->lang = detect_language(buf);
        }
    }

    if (!cd->has_range && !cd->has_number) {
        cd->from = 0;
        cd->to = SIZE_MAX;
    }

    fp = fopen(file, "w");
    if (fp == NULL) {
        set_error("could not open '%s': %s", file, strerror(errno));
        return -1;
    }
    num_bytes = write_file(buf, cd->from, cd->to, fp);
    fclose(fp);

    if (file == buf->path) {
        stat(buf->path, &buf->st);
        buf->save_event_i = buf->event_i;
    }

    if (num_bytes == 0) {
        set_message("nothing to write");
    } else {
        set_message("%s %zuL, %zuB written", get_pretty_path(file),
                MIN(buf->num_lines - 1, cd->to) -
                    MIN(buf->num_lines - 1, cd->from) + 1,
                num_bytes);
    }
    return 0;
}

int cmd_cquit(struct cmd_data *cd)
{
    Core.exit_code = cd->has_number ? (int) cd->from : 1;
    Core.is_stopped = true;
    return 0;
}

int cmd_edit(struct cmd_data *cd)
{
    char *entry;
    struct buf *buf = NULL;

    if (cd->arg[0] == '\0') {
        entry = choose_file(NULL);
        if (entry != NULL) {
            buf = create_buffer(entry);
        }
    } else {
        buf = create_buffer(cd->arg);
    }
    if (buf == NULL) {
        return -1;
    }
    set_frame_buffer(SelFrame, buf);
    return 0;
}

int cmd_exit(struct cmd_data *cd)
{
    if (save_buffer(cd, SelFrame->buf) != 0) {
        return -1;
    }
    destroy_frame(SelFrame);
    return 0;
}

int cmd_exit_all(struct cmd_data *cd)
{
    for (struct frame *frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (save_buffer(cd, frame->buf) && !cd->force) {
            return -1;
        }
    }
    Core.is_stopped = true;
    return 0;
}

int cmd_highlight(struct cmd_data *cd)
{
    int             i;
    short           fg, bg;
    short           r, g, b;

    for (i = 1; i < HI_MAX; i++) {
        if (strcasecmp(HiNames[i], cd->arg) == 0) {
            set_highlight(Core.msg_win, HI_CMD);
            Core.msg_state = MSG_OTHER;
            werase(Core.msg_win);
            waddstr(Core.msg_win, HiNames[i]);

            waddch(Core.msg_win, ' ');
            set_highlight(Core.msg_win, i);
            waddstr(Core.msg_win, "xxx");

            color_content(i, &r, &g, &b);

            set_highlight(Core.msg_win, HI_CMD);

            waddch(Core.msg_win, ' ');
            pair_content(i, &fg, &bg);
            color_content(fg, &r, &g, &b);
            r = r * 51 / 200;
            g = g * 51 / 200;
            b = b * 51 / 200;
            wprintw(Core.msg_win, "#%02x%02x%02x", r, g, b);

            waddch(Core.msg_win, ' ');
            color_content(bg, &r, &g, &b);
            r = r * 51 / 200;
            g = g * 51 / 200;
            b = b * 51 / 200;
            wprintw(Core.msg_win, "#%02x%02x%02x", r, g, b);
            return 0;
        }
    }
    return -1;
}

int cmd_nohighlight(struct cmd_data *cd)
{
    (void) cd;
    SelFrame->buf->num_matches = 0;
    return 0;
}

int cmd_quit(struct cmd_data *cd)
{
    if (!cd->force && SelFrame->buf->save_event_i != SelFrame->buf->event_i) {
        set_error("buffer has changed, use  :q!  to quit");
        return -1;
    }
    destroy_frame(SelFrame);
    return 0;
}

int cmd_quit_all(struct cmd_data *cd)
{
    if (cd->force) {
        Core.is_stopped = true;
        return 0;
    }
    for (struct frame *frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (!cd->force && frame->buf->save_event_i != frame->buf->event_i) {
            set_error("buffer has changed, use  :qa!  to quit");
            return -1;
        }
    }
    Core.is_stopped = true;
    return 0;
}

int cmd_read(struct cmd_data *cd)
{
    const char *file;
    FILE *fp;

    if (cd->arg[0] == '\0') {
        file = SelFrame->buf->path;
        if (file == NULL) {
            set_error("no file name");
            return -1;
        }
    } else {
        file = cd->arg;
    }
    fp = fopen(cd->arg, "r");
    if (fp == NULL) {
        set_error("failed opening '%s': %s", file, strerror(errno));
        return -1;
    }
    read_file(SelFrame->buf, &SelFrame->cur, fp);
    fclose(fp);
    return 0;
}

static struct raw_line *gen_lines(const char *s, size_t *p_num_lines)
{
    struct raw_line *lines;
    size_t          num_lines;
    char            ch;
    char            *p;
    size_t          a, n;

    lines = NULL;
    num_lines = 0;

    p = NULL;
    a = 0;
    n = 0;
    for (; s[0] != '\0'; s++) {
        if (s[0] == '\\') {
            s++;
            switch (s[0]) {
            case 'a': ch = '\a'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            case 'v': ch = '\v'; break;
            default:
                ch = s[0];
            }
        } else {
            ch = s[0];
        }
        if (ch == '\n') {
            lines = xreallocarray(lines, sizeof(*lines), num_lines + 1);
            lines[num_lines].s = xmemdup(p, n);
            lines[num_lines].n = n;
            num_lines++;
            n = 0;
            continue;
        }
        if (a == n) {
            a *= 2;
            a++;
            p = xrealloc(p, a);
        }
        p[n++] = ch;
    }
    lines = xreallocarray(lines, sizeof(*lines), num_lines + 1);
    lines[num_lines].s = xmemdup(p, n);
    lines[num_lines].n = n;
    num_lines++;

    free(p);

    *p_num_lines = num_lines;
    return lines;
}

int cmd_substitute(struct cmd_data *cd)
{
    char                sep;
    char                *e1, *e2;
    struct buf          *buf;
    struct raw_line     *lines;
    size_t              num_lines;
    struct undo_event   *ev;

    sep = cd->arg[0];
    if (sep == '\0') {
        return 0;
    }

    do {
        e1 = strchr(&cd->arg[1], sep);
        if (e1 == NULL) {
            return 0;
        }
    } while (e1[-1] == '\\');

    do {
        e2 = strchr(&e1[1], sep);
        if (e2 == NULL) {
            e2 = e1 + strlen(e1);
            break;
        }
    } while (e2[-1] == '\\');

    *e1 = '\0';
    *e2 = '\0';

    buf = SelFrame->buf;
    search_string(buf, &cd->arg[1]);
    lines = gen_lines(&e1[1], &num_lines);
    /* do it in reverse */
    for (struct match *m = &buf->matches[buf->num_matches];
            m != buf->matches; ) {
        m--;
        /* skip matches that are out of range */
        if (m->from.line > cd->to ||
                m->to.line < cd->from) {
            continue;
        }
        ev = replace_lines(buf, &m->from, &m->to, lines, num_lines);
        ev->cur = SelFrame->cur;
    }

    for (size_t i = 0; i < num_lines; i++) {
        free(lines[i].s);
    }
    free(lines);
    return 0;
}

int cmd_syntax(struct cmd_data *cd)
{
    for (int i = 0; i < NUM_LANGS; i++) {
        if (strcasecmp(Langs[i].name, cd->arg) == 0) {
            set_language(SelFrame->buf, i);
            return 0;
        }
    }
    if (cd->arg[0] != '\0') {
        set_error("language '%s' does not exist", cd->arg);
        return -1;
    }
    set_language(SelFrame->buf, detect_language(SelFrame->buf));
    return 0;
}

int cmd_write(struct cmd_data *cd)
{
    if (cd->from > 1) {
        cd->from--;
    }
    if (cd->to > 1) {
        cd->to--;
    }
    return save_buffer(cd, SelFrame->buf);
}

int cmd_write_all(struct cmd_data *cd)
{
    for (struct frame *frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (save_buffer(cd, frame->buf) != 0) {
            return -1;
        }
    }
    return 0;
}
