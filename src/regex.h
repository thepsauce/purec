#ifndef REGEX_H
#define REGEX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "util.h"
#include "text.h"

struct char_set {
    uint16_t set[16];
};

/**
 * Sets given char to on in the char set.
 *
 * @param set   The char set to use.
 * @param ch    The char to set.
 */
void set_char(struct char_set *set, unsigned char ch);

/**
 * Toggles given char from on to off or off to on in the char set.
 *
 * @param set   The char set to use.
 * @param ch    The char to toggle.
 */
void toggle_char(struct char_set *set, unsigned char ch);

/**
 * Checks if given char is on in the char set.
 *
 * @param set   The char set to use.
 * @param ch    The char to check for.
 */
bool is_char_toggled(struct char_set *set, unsigned char ch);

/**
 * Toggles all chars in the char set.
 *
 * @param set   The set to use.
 */
void invert_chars(struct char_set *set);

enum rxgroup_type {
    RXGROUP_NULL,
    RXGROUP_ROUND,
    RXGROUP_OR,
    RXGROUP_CON,
    RXGROUP_RANGE,
    RXGROUP_LIT,
    RXGROUP_WORD_START,
    RXGROUP_WORD_END,
    RXGROUP_START,
    RXGROUP_END,
};

struct regex_group {
    enum rxgroup_type type;
    struct regex_group *left;
    struct regex_group *right;
    struct char_set chars;
    size_t min, max;
};

struct regex_parser {
    struct regex_group **stack;
    size_t num_stack;
};

/**
 * Parses given regex to the internal regex group structure.
 */
struct regex_group *parse_regex(const char *s);

/**
 * Frees a regex group allocated by the parsing process.
 *
 * @param group The group to check for.
 */
void free_regex_group(struct regex_group *group);

struct regex_match {
    /// the start of the match
    size_t start;
    /// the end of the match
    size_t end;
};

struct regex_matcher {
    struct line *lines;
    line_t num_lines;
    struct pos pos;
    struct regex_match sub[9];
    size_t num;
};

/**
 * Matches given string against a regex group.
 *
 * @param group     The group to use for matching.
 * @param matcher   The place to store sub matches and the input text.
 *
 * @return The length of the match.
 */
int match_regex(struct regex_group *group, struct regex_matcher *matcher);

#endif
