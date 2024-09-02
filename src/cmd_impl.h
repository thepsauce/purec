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
        set_message("%s %zuL, %zuB written", file,
                MIN(buf->num_lines, cd->to) - MIN(buf->num_lines, cd->from) + 1,
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
    struct file_list list;
    size_t entry;
    struct buf *buf = NULL;

    if (cd->arg[0] == '\0') {
        init_file_list(&list, ".");
        if (get_deep_files(&list) == 0) {
            entry = choose_fuzzy((const char**) list.paths, list.num);
            if (entry != SIZE_MAX) {
                buf = create_buffer(list.paths[entry]);
            }
        }
        clear_file_list(&list);
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
