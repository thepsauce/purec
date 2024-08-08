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
            format_message("no file name");
            return -1;
        }

        if ((cd->has_range || cd->has_number) && !cd->force) {
            format_message("use ! to write partial buffer");
            return -1;
        }

        file = buf->path;
        if (!cd->force && stat(file, &st) == 0) {
            if (st.st_mtime != buf->st.st_mtime) {
                format_message("file changed, use  :w!  to overwrite");
                return -1;
            }
        }
    } else if (buf->path == NULL) {
        buf->path = xstrdup(file);
        file = buf->path;
    }

    fp = fopen(file, "w");
    if (fp == NULL) {
        format_message("could not open '%s': %s", file, strerror(errno));
        return -1;
    }
    num_bytes = write_file(buf, cd->from, cd->to, fp);
    fclose(fp);

    if (file == buf->path) {
        stat(buf->path, &buf->st);
        buf->save_event_i = buf->event_i;
    }

    if (num_bytes == 0) {
        format_message("nothing to write");
    } else {
        format_message("%s %zuL, %zuB written", file,
                MIN(buf->num_lines, cd->to) - MIN(buf->num_lines, cd->from) + 1,
                num_bytes);
    }
    return 0;
}

int cmd_cquit(struct cmd_data *cd)
{
    ExitCode = cd->has_number ? (int) cd->from : 1;
    IsRunning = false;
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
    IsRunning = false;
    return 0;
}

int cmd_quit(struct cmd_data *cd)
{
    if (!cd->force && SelFrame->buf->save_event_i != SelFrame->buf->event_i) {
        format_message("buffer has changed, use  :q!  to quit");
        return 1;
    }
    destroy_frame(SelFrame);
    return 0;
}

int cmd_quit_all(struct cmd_data *cd)
{
    if (cd->force) {
        IsRunning = false;
        return 0;
    }
    for (struct frame *frame = FirstFrame; frame != NULL; frame = frame->next) {
        if (!cd->force && SelFrame->buf->save_event_i != SelFrame->buf->event_i) {
            format_message("buffer has changed, use  :qa!  to quit");
            return 1;
        }
    }
    IsRunning = false;
    return 0;
}

int cmd_read(struct cmd_data *cd)
{
    const char *file;
    FILE *fp;

    if (cd->arg[0] == '\0') {
        file = SelFrame->buf->path;
        if (file == NULL) {
            format_message("no file name");
            return -1;
        }
    } else {
        file = cd->arg;
    }
    fp = fopen(cd->arg, "r");
    if (fp == NULL) {
        format_message("failed opening '%s': %s\n", file, strerror(errno));
        return -1;
    }
    read_file(SelFrame->buf, &SelFrame->cur, fp);
    fclose(fp);
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
        if (save_buffer(cd, frame->buf)) {
            return 1;
        }
    }
    return 0;
}