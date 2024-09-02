#ifndef COLOR_H
#define COLOR_H

/*     *           *    *
 * *     * *    *
 *     C*o l o*r *     **
 *  * *    *    *  *
 *                     */

#include <ncurses.h>

/**
 * A highlight value like `HI_NORMAL` is itself the id of the color pair within
 * ncurses and also an index into a color theme attribute array.
 */

#define HI_NORMAL       1
#define HI_COMMENT      2
#define HI_JAVADOC      3
#define HI_TYPE         4
#define HI_TYPE_MOD     5
#define HI_IDENTIFIER   6
#define HI_NUMBER       7
#define HI_STRING       8
#define HI_CHAR         9
#define HI_PREPROC      10
#define HI_OPERATOR     11
#define HI_ERROR        12
#define HI_LINE_NO      13
#define HI_STATUS       14
#define HI_VERT_SPLIT   15
#define HI_FUZZY        16
#define HI_CMD          17
#define HI_FUNCTION     18
#define HI_ADDED        19
#define HI_REMOVED      20
#define HI_CHANGED      21
#define HI_PAREN_MATCH  22
#define HI_MAX          23

extern struct theme {
    const char *name;
    const char *term_colors[256];
    int attribs[HI_MAX][3];
    int colors_needed;
} Themes[];

extern const int Theme;

#define get_attrib_of(hi) (Themes[Theme].attribs[hi][2])

void init_colors(void);
void set_highlight(WINDOW *win, int hi);

#endif
