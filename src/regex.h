#ifndef REGEX_H
#define REGEX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

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
 * Checks if given char is on in the char set.
 *
 * @param set   The char set to use.
 * @param ch    The char to check for.
 */
bool is_char_toggled(struct char_set *set, unsigned char ch);

enum rxgroup_type {
    RXGROUP_NULL,
    RXGROUP_ROUND,
    RXGROUP_OR,
    RXGROUP_CON,
    RXGROUP_PLUS,
    RXGROUP_STAR,
    RXGROUP_OPT,
    RXGROUP_LIT,
};

struct regex_group {
    enum rxgroup_type type;
    struct regex_group *left;
    struct regex_group *right;
    struct char_set chars;
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

/**
 * Matches given string against a regex group.
 *
 * @param group The group to use for matching.
 * @param s     The string to match against.
 * @param i     The index to start matching from.
 * @param len   The length of the string.
 *
 * @return The length of the match.
 */
size_t match_regex(struct regex_group *group, const char *s,
                   size_t i, size_t len);

/**
 * Checks if the given string matches a regex group.
 *
 * @param group The group to use for matching.
 * @param s     The string to match against.
 *
 * @return If the group matched the entire string.
 */
bool regex_matches(struct regex_group *group, const char *s);

#endif
