#include "color.h"
#include "util.h"

int HiAttribs[] = {
    [HI_NORMAL] = A_NORMAL,
    [HI_COMMENT] = A_BOLD,
    [HI_JAVADOC] = A_BOLD | A_ITALIC,
    [HI_TYPE] = A_BOLD,
    [HI_TYPE_MOD] = A_ITALIC,
    [HI_IDENTIFIER] = A_BOLD,
    [HI_NUMBER] = A_NORMAL,
    [HI_STRING] = A_BOLD,
    [HI_CHAR] = A_NORMAL,
    [HI_PREPROC] = A_UNDERLINE,
    [HI_OPERATOR] = A_BOLD,
    [HI_ERROR] = A_REVERSE,
    [HI_LINE_NO] = A_NORMAL,
    [HI_STATUS] = A_NORMAL,
    [HI_VERT_SPLIT] = A_NORMAL,
    [HI_FUZZY] = A_NORMAL,
    [HI_CMD] = A_NORMAL,
};

static void convert_hex_to_rgb(const char *hex, int *p_r, int *p_g, int *p_b)
{
    unsigned int r, g, b;

    sscanf(hex, "#%02x%02x%02x", &r, &g, &b);
    *p_r = (int) (r * 1000 / 255);
    *p_g = (int) (g * 1000 / 255);
    *p_b = (int) (b * 1000 / 255);
}

void init_colors(void)
{
    const int bg = 0;
    int r, g, b;

    if (start_color() == ERR) {
        return;
    }

    const char *term_colors[] = {
        "#0f1419", "#f07178", "#b8cc52", "#feb354",
        "#36a3d9", "#ffee99", "#95e6cb", "#ffffff",
        "#3e4b59", "#ff3333", "#b8cc52", "#f29718",
        "#36a3d9", "#ffee99", "#95e6cb", "#5c6773",
        "#232a36", "#feed98", "#b7cb52",
    };

    for (size_t i = 0; i < ARRAY_SIZE(term_colors); i++) {
        convert_hex_to_rgb(term_colors[i], &r, &g, &b);
        init_color(i, r, g, b);
    }

    init_pair(HI_NORMAL, 7, bg);
    init_pair(HI_COMMENT, 15, bg);
    init_pair(HI_JAVADOC, 15, bg);
    init_pair(HI_TYPE, 4, bg);
    init_pair(HI_TYPE_MOD, 4, bg);
    init_pair(HI_IDENTIFIER, 3, bg);
    init_pair(HI_NUMBER, 17, bg);
    init_pair(HI_STRING, 2, bg);
    init_pair(HI_CHAR, 5, bg);
    init_pair(HI_PREPROC, 1, bg);
    init_pair(HI_OPERATOR, 18, bg);
    init_pair(HI_ERROR, 9, bg);
    init_pair(HI_LINE_NO, 8, 16);
    init_pair(HI_STATUS, 4, 16);
    init_pair(HI_VERT_SPLIT, 8, bg);
    init_pair(HI_FUZZY, 4, 16);
    init_pair(HI_CMD, 4, bg);
}

void set_highlight(WINDOW *win, int hi)
{
    wattr_set(win, HiAttribs[hi], hi, NULL);
}
