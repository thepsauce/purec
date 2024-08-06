#ifndef CMD_H
#define CMD_H

#include <stddef.h>

extern struct command_line {
    /// current command line content
    char *buf;
    /// number of allocated bytes for `buf`
    size_t a;
    /// length of the buffer
    size_t len;
    /// index within the buffer
    size_t index;
    /// horizontal scrolling
    size_t scroll;
    /// history of all entered linees
    char **history;
    /// number of lines in the history
    size_t num_history;
    /// current index within the history
    size_t history_index;
} CmdLine;

/**
 * Reads a command line and executes given command.
 *
 * `beg` determines what type of command line it is. These are all options:
 * "/"  Search
 * "?"  Search backwards
 * ":"  Run command
 *
 * @param beg   What the command line should start with, must be larger than 1.
 */
void read_command_line(const char *beg);

#endif
