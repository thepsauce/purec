#ifndef COLOR_H
#define COLOR_H

#include <ncurses.h>

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
#define HI_MAX          18

extern int HiAttribs[HI_MAX];

void init_colors(void);
void set_highlight(WINDOW *win, int hi);

#endif
