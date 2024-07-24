#include "mode.h"

#include <stdio.h>

struct normal_mode Normal;
int Mode;

void set_mode(int mode)
{
    Mode = mode;
    switch (mode) {
    case NORMAL_MODE:
        printf("\x1b[\x30 q");
        break;
    case INSERT_MODE:
        printf("\x1b[\x35 q");
        break;
    default:
        printf("set_mode - invalid mode: %d", mode);
        abort();
    }
    fflush(stdout);
}
