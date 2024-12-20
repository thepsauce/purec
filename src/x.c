#include "xalloc.h"
#include "buf.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

/// X11 data structure, hold onto all information needed
static struct x_data {
    /// X connection, if this is `NULL`, all clipboard functions safely fail
    Display *dpy_copy;
    /// this is used for the paste operation
    Display *dpy_paste;
    /// this takes the role of the selection owner
    Window win_copy;
    /// this is used for the paste operation
    Window win_paste;
    /// the thread to send clipboard data
    pthread_t thread;
    /// lock for synchronising `thread` with the main thread
    pthread_mutex_t sel_lock;
    /// segment to send to requestors
    struct undo_seg *seg;
    /// the lines currently being worked on by paste
    struct text text;
} X;

/**
 * Handles a `SelectionRequest` event by sending an event to the requestor.
 *
 * @param e The event to handle.
 */
static void handle_sel_request(XEvent *e);

/**
 * Handles a `SelectionNotify` event by receiving the data in chunks.
 *
 * INCR (incremental sending of data) is NOT supported.
 *
 * @param e The event to handle.
 */
static void handle_sel_notify(XEvent *e);

/**
 * Main loop of the X11 thread responsible for sending windows the clipboard
 * selection by resolving `SelectionRequest` events.
 *
 * @param _unused   This parameter is not used for anything.
 */
static void *x_thread(void *_unused);

void init_clipboard(void)
{
    X.dpy_copy = XOpenDisplay(NULL);
    if (X.dpy_copy != NULL) {
        X.win_copy = XCreateSimpleWindow(X.dpy_copy,
                DefaultRootWindow(X.dpy_copy),
                0, 0, 1, 1, 0, 0, 0);

        if (pthread_create(&X.thread, NULL, x_thread, NULL) != 0) {
            XCloseDisplay(X.dpy_copy);
            X.dpy_copy = NULL;
        }
        pthread_mutex_init(&X.sel_lock, NULL);
    }

    X.dpy_paste = XOpenDisplay(NULL);
    if (X.dpy_paste != NULL) {
        X.win_paste = XCreateSimpleWindow(X.dpy_paste,
                DefaultRootWindow(X.dpy_paste),
                0, 0, 1, 1, 0, 0, 0);
    }
}

static void *x_thread(void *_unused)
{
    XEvent          xev;

    while (XNextEvent(X.dpy_copy, &xev), 1) {
        if (xev.type == SelectionRequest) {
            pthread_mutex_lock(&X.sel_lock);
            handle_sel_request(&xev);
            pthread_mutex_unlock(&X.sel_lock);
        }
    }

    return _unused;
}

int copy_clipboard(struct undo_seg *seg, int primary)
{
    Atom            clipboard;

    if (X.dpy_copy == NULL) {
        return -1;
    }

    pthread_mutex_lock(&X.sel_lock);

    if (X.seg != NULL) {
        unload_undo_data(X.seg);
    }
    X.seg = seg;

    if (primary == 0) {
        clipboard = XInternAtom(X.dpy_copy, "CLIPBOARD", False);
    } else {
        clipboard = XA_PRIMARY;
    }
    XSetSelectionOwner(X.dpy_copy, clipboard, X.win_copy, CurrentTime);
    /* flush must be here so that the x_thread does not halt indefinitelyy */
    XFlush(X.dpy_copy);

    pthread_mutex_unlock(&X.sel_lock);
    return 0;
}

static void handle_sel_request(XEvent *e)
{
    XSelectionRequestEvent  *xsre;
    XSelectionEvent         xev;
    Atom                    targets, target, clipboard;

    xsre = (XSelectionRequestEvent*) e;
    xev.type = SelectionNotify;
    xev.requestor = xsre->requestor;
    xev.selection = xsre->selection;
    xev.target = xsre->target;
    xev.time = xsre->time;
    if (xsre->property == None) {
        xsre->property = xsre->target;
    }

    /* reject */
    xev.property = None;

    targets = XInternAtom(X.dpy_copy, "TARGETS", False);
    target = XInternAtom(X.dpy_copy, "UTF8_STRING", False);
    if (target == None) {
        target = XA_STRING;
    }

    if (xsre->target == targets) {
        /* respond with the supported type */
        (void) XChangeProperty(xsre->display, xsre->requestor, xsre->property,
                XA_ATOM, 32, PropModeReplace,
                (unsigned char*) &target, 1);
        xev.property = xsre->property;
    } else if (xsre->target == target || xsre->target == XA_STRING) {
        clipboard = XInternAtom(X.dpy_copy, "CLIPBOARD", false);
        if (xsre->selection != clipboard) {
            return;
        }
        (void) XChangeProperty(xsre->display, xsre->requestor,
                xsre->property, xsre->target,
                8, PropModeReplace,
                (unsigned char*) X.seg->data, X.seg->data_len);
        xev.property = xsre->property;
    }

    /* all done, send a notification to the listener */
    (void) XSendEvent(xsre->display, xsre->requestor, True, 0, (XEvent*) &xev);
}

bool paste_clipboard(struct text *text, int primary)
{
    Atom            clipboard, target;
    XEvent          xev;

    if (X.dpy_paste == NULL) {
        return false;
    }

    if (primary == 0) {
        clipboard = XInternAtom(X.dpy_paste, "CLIPBOARD", False);
    } else {
        clipboard = XA_PRIMARY;
    }
    target = XInternAtom(X.dpy_paste, "UTF8_STRING", False);
    if (target == None) {
        target = XA_STRING;
    }
    XConvertSelection(X.dpy_paste, clipboard, target, clipboard,
                X.win_paste, CurrentTime);

    if (X.text.lines != NULL) {
        save_lines(&X.text);
    }

    X.text.lines = NULL;
    X.text.num_lines = 0;

    /* get the next selection notify event */
    while (XNextEvent(X.dpy_paste, &xev), xev.type != SelectionNotify) {
        (void) 0;
    }
    handle_sel_notify(&xev);

    *text = X.text;
    return true;
}

static void handle_sel_notify(XEvent *e)
{
    unsigned long   nitems, ofs, rem;
    int             format;
    unsigned char   *data, *last, *repl, *st;
    Atom            type, property = None;
    struct line     *line;

    ofs = 0;
    property = e->xselection.property;
    if (property == None) {
        return;
    }

    do {
        if (XGetWindowProperty(X.dpy_paste, X.win_paste, property, ofs,
                    BUFSIZ / 4, False, AnyPropertyType,
                    &type, &format, &nitems, &rem, &data) != 0) {
            return;
        }

        repl = data;
        last = data + nitems * format / 8;
        while (st = repl, repl = memchr(repl, '\n', last - repl), 1) {
            if (repl == NULL) {
                repl = last;
            }
            line = insert_blank(&X.text, X.text.num_lines, 1);
            line->s = xmemdup(st, repl - st);
            line->n = repl - st;
            if (repl == last) {
                break;
            }
            repl++;
        }

        XFree(data);
        /* number of 32-bit chunks returned */
        ofs += nitems * format / 32;
    } while (rem > 0);

    X.text.a_lines = 0;
    X.text.lines = xreallocarray(X.text.lines, X.text.num_lines,
                                 sizeof(*X.text.lines));
}
