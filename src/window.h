#define FWIN_UPDATE_CURSOR 0x01
#define FWIN_UPDATE_INDEX 0x02
#define FWIN_UPDATE_VCT 0x04

typedef struct point {
	int x, y;
} Point;

typedef enum {
	EDIT_MODE_NORMAL,
	EDIT_MODE_INSERT,
} edit_mode_t;

typedef uint64_t syntax_state_t;

typedef struct syntax {
	int (*next)(struct syntax *syn);
	void (*destroy)(struct syntax *syn);
	Buffer *buffer;
	size_t index;
	attr_t attr;
	syntax_state_t stack[32];
	unsigned nstack;
	syntax_state_t state;
	char *conceal;
} Syntax;

typedef struct window {
	int64_t flags;
	WINDOW *win;
	struct window *next;
} Window;

Window *window_new(size_t sz, int64_t flags, int x, int y, int w, int h);

extern Window *first_window;

typedef struct edit {
	Window win;
	Buffer *buffer;
	size_t index;
	size_t wantindex;
	Point cursor;
	Point wantcursor;
	int scroll;
	int vct;
	struct syntax *syntax;
} Edit;

Edit *edit_new(Buffer *buf, int x, int y, int w, int h);
void edit_update(Edit *edit);
void edit_handle(Edit *edit, int c, edit_mode_t *mode);
void edit_destroy(Edit *edit);

