#ifndef COLOR_H
#define COLOR_H

/*     *           *    *
 * *     * *    *
 *     C*o l o*r *     **
 *  * *    *    *  *
 *       *       *     */

#include <ncurses.h>

/**
 * A highlight value like `HI_NORMAL` is itself the id of the color pair within
 * ncurses and also an index into a color theme attribute array.
 */

#define HI_DEFAULT      0
#define HI_NORMAL       1
#define HI_COMMENT      2
#define HI_JAVADOC      3
#define HI_TYPE         4
#define HI_TYPE_MOD     5
#define HI_IDENTIFIER   6
#define HI_STATEMENT    7
#define HI_NUMBER       8
#define HI_STRING       9
#define HI_CHAR         10
#define HI_PREPROC      11
#define HI_OPERATOR     12
#define HI_ERROR        13
#define HI_LINE_NO      14
#define HI_STATUS       15
#define HI_VERT_SPLIT   16
#define HI_FUZZY        17
#define HI_CMD          18
#define HI_FUNCTION     19
#define HI_ADDED        20
#define HI_REMOVED      21
#define HI_CHANGED      22
#define HI_PAREN_MATCH  23
#define HI_SEARCH       24
#define HI_VISUAL       25
#define HI_TODO         26
#define HI_MAX          27

extern const char *HiNames[HI_MAX];

extern const struct theme {
    const char *name;
    const char *term_colors[256];
    int attribs[HI_MAX][3];
    int colors_needed;
} Themes[];

extern const struct named_color {
    const char *name;
    unsigned int color;
} ColorMap[];

/**
 * Gets the number of elements in `Themes`.
 *
 * @return The number of elements.
 */
int get_number_of_themes(void);

#define get_attrib_of(hi) (Themes[Core.theme].attribs[hi][2])

/**
 * Initializes the colors.
 */
void init_colors(void);

/**
 * Sets the current theme to given theme name.
 *
 * @param name  The name of the theme.
 *
 * @return 0 if the theme was set, -1 if it does not exist.
 */
int set_theme(const char *name);

/**
 * Sets the attributes of given window to the highlight.
 *
 * @param win   The window whose attributes to set.
 * @param hi    The highlighting to use.
 */
void set_highlight(WINDOW *win, int hi);

#endif
