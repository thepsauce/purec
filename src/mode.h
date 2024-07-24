#ifndef MODE_H
#define MODE_H

#include <stdint.h>
#include <stdlib.h>

#define NORMAL_MODE 0
#define INSERT_MODE 1

struct pos {
    size_t line;
    size_t col;
};

extern int Mode;

extern struct normal_mode {
    struct pos *jumps;
    size_t num_jumps;
    size_t jump_i;
    size_t counter;
} Normal;

void set_mode(int mode);

#endif
