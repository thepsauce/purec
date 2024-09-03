#include "color.h"
#include "util.h"

#include <string.h>

struct theme Themes[] = {
    {
        .name = "Term",
        .attribs = {
            [HI_NORMAL] = { 7, 0, A_NORMAL },
            [HI_COMMENT] = { 6, 0, A_NORMAL },
            [HI_JAVADOC] = { 5, 0, A_BOLD | A_ITALIC },
            [HI_TYPE] = { 2, 0, A_BOLD },
            [HI_TYPE_MOD] = { 2, 0, A_ITALIC },
            [HI_IDENTIFIER] = { 2, 0, A_BOLD },
            [HI_NUMBER] = { 5, 0, A_NORMAL },
            [HI_STRING] = { 3, 0, A_BOLD },
            [HI_CHAR] = { 3, 0, A_NORMAL },
            [HI_PREPROC] = { 5, 0, A_BOLD | A_UNDERLINE },
            [HI_OPERATOR] = { 5, 0, A_BOLD },
            [HI_ERROR] = { 1, 0, A_REVERSE },
            [HI_LINE_NO] = { 3, 0, A_NORMAL },
            [HI_STATUS] = { 5, 0, A_NORMAL },
            [HI_VERT_SPLIT] = { 7, 0, A_NORMAL },
            [HI_FUZZY] = { 7, 4, A_NORMAL },
            [HI_CMD] = { 7, 0, A_NORMAL },
            [HI_FUNCTION] = { 7, 0, A_BOLD },
            [HI_ADDED] = { 2, 0, A_BOLD },
            [HI_REMOVED] = { 1, 0, A_BOLD },
            [HI_CHANGED] = { 6, 0, A_BOLD },
            [HI_PAREN_MATCH] = { 3, 0, A_BOLD | A_UNDERLINE },
        },
        .colors_needed = 8,
    },

    {
        .name = "ThemeA",
        .term_colors = {
            "#0f1419", "#f07178", "#b8cc52", "#feb354",
            "#36a3d9", "#ffee99", "#95e6cb", "#ffffff",
            "#3e4b59", "#ff3333", "#b8cc52", "#f29718",
            "#36a3d9", "#ffee99", "#95e6cb", "#5c6773",
            "#232a36", "#feed98", "#b7cb52", "#ffb454",
        },
        .attribs = {
            [HI_NORMAL] = { 7, 0, A_NORMAL },
            [HI_COMMENT] = { 15, 0, A_BOLD },
            [HI_JAVADOC] = { 15, 0, A_BOLD | A_ITALIC },
            [HI_TYPE] = { 4, 0, A_BOLD },
            [HI_TYPE_MOD] = { 4, 0, A_ITALIC },
            [HI_IDENTIFIER] = { 3, 0, A_BOLD },
            [HI_NUMBER] = { 17, 0, A_NORMAL },
            [HI_STRING] = { 2, 0, A_BOLD },
            [HI_CHAR] = { 5, 0, A_NORMAL },
            [HI_PREPROC] = { 1, 0, A_UNDERLINE },
            [HI_OPERATOR] = { 18, 0, A_BOLD },
            [HI_ERROR] = { 9, 0, A_REVERSE },
            [HI_LINE_NO] = { 8, 16, A_NORMAL },
            [HI_STATUS] = { 4, 16, A_NORMAL },
            [HI_VERT_SPLIT] = { 8, 0, A_NORMAL },
            [HI_FUZZY] = { 4, 16, A_NORMAL },
            [HI_CMD] = { 4, 0, A_NORMAL },
            [HI_FUNCTION] = { 19, 0, A_NORMAL },
            [HI_ADDED] = { 2, 0, A_BOLD },
            [HI_REMOVED] = { 1, 0, A_BOLD },
            [HI_CHANGED] = { 6, 0, A_BOLD },
            [HI_PAREN_MATCH] = { 3, 0, A_BOLD | A_UNDERLINE },
        },
        .colors_needed = 256,
    }
};

int Theme = 0;

static void convert_hex_to_rgb(const char *hex, int *p_r, int *p_g, int *p_b)
{
    unsigned int r, g, b;

    sscanf(hex, "#%02x%02x%02x", &r, &g, &b);
    /* ncurses colors go from 0 to 1000 and 1000/255 = 200/51 */
    *p_r = r * 200 / 51;
    *p_g = g * 200 / 51;
    *p_b = b * 200 / 51;
}

/**
 * Sets all colors and attributes for the current theme.
 */
static void init_theme(void)
{
    struct theme *t;
    int r, g, b;

    t = &Themes[Theme];
    if (can_change_color()) {
        for (int i = 0; i < t->colors_needed; i++) {
            if (t->term_colors[i] == NULL) {
                continue;
            }
            convert_hex_to_rgb(t->term_colors[i], &r, &g, &b);
            init_color(i, r, g, b);
        }
    }

    for (int i = 1; i < HI_MAX; i++) {
        init_pair(i, t->attribs[i][0], t->attribs[i][1]);
    }
}

void init_colors(void)
{
    if (start_color() == ERR) {
        return;
    }
    init_theme();
}

int set_theme(const char *name)
{
    for (int t = 0; t < (int) ARRAY_SIZE(Themes); t++) {
        if (strcasecmp(Themes[t].name, name) == 0) {
            Theme = t;
            init_theme();
            return 0;
        }
    }
    return -1;
}

void set_highlight(WINDOW *win, int hi)
{
    wattr_set(win, get_attrib_of(hi), hi, NULL);
}
