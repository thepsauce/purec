#ifndef CMD_H
#define CMD_H

/**
 * Runs given command, `cmd` might get modified.
 *
 * @param cmd   The command line to run.
 *
 * @return -1 if the command was successfully run, 0 otherwise.
 */
int run_command(char *cmd);

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
