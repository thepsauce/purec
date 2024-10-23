#ifndef PARSE_H
#define PARSE_H

#include <stdbool.h>
#include <stdio.h>

#include <math.h>

/* * * Group * * */

/* The life cycle of a group is:
 * 0. The group is created (xmalloc())
 * 1. The parser parses a string and generates groups (parse()).
 * 2.1 The core evaluates this group (compute_value()).
 * 2.2 Or the group is stored in a variable (compute_value() as well)
 * 3.1 The group is destroyed after no longer needed (free_group()).
 * 3.2 Or the address of the group is reused (clear_group()).
 *
 * Below are all types..
 */

#define GROUP_NULL          0

/* opr expr */
#define GROUP_POSITIVE      1
#define GROUP_NEGATE        2
#define GROUP_NOT           3
#define GROUP_SQRT          4
#define GROUP_CBRT          5

/* expr opr expr */
#define GROUP_PLUS          6
#define GROUP_MINUS         7
#define GROUP_MULTIPLY      8
#define GROUP_DIVIDE        9
#define GROUP_MOD           10
#define GROUP_RAISE         11
#define GROUP_LOWER         12

#define GROUP_AND           13
#define GROUP_OR            14
#define GROUP_XOR           15

#define GROUP_IF            16

#define GROUP_LESS          17
#define GROUP_LESS_EQUAL    18
#define GROUP_GREATER       19
#define GROUP_GREATER_EQUAL 20
#define GROUP_EQUAL         21
#define GROUP_NOT_EQUAL     22

#define GROUP_COMMA         23
#define GROUP_SEMICOLON     24

#define GROUP_DO            25

/* expr opr */
#define GROUP_EXCLAM        26
#define GROUP_PERCENT       27
#define GROUP_RAISE2        28
#define GROUP_RAISE3        29
#define GROUP_ELSE          30

/* opr expr opr */
#define GROUP_ROUND         31
#define GROUP_DOUBLE_CORNER 32
#define GROUP_CORNER        33
#define GROUP_SQUARE        34
#define GROUP_CURLY         35
#define GROUP_DOUBLE_BAR    36
#define GROUP_BAR           37
#define GROUP_CEIL          38
#define GROUP_FLOOR         39

/* expr expr */
#define GROUP_IMPLICIT      40

#define GROUP_VARIABLE      41
#define GROUP_NUMBER        42
#define GROUP_STRING        43

#define GROUP_MAX           44

struct val_string {
    /// pointer to the string data
    char            *p;
    /// length of the string
    size_t          n;
};

struct group {
    /// type of this group
    int             type;
    /// children of the group
    struct group    *children;
    /// number of children
    size_t          num_children;
    /// parent of the group
    struct group    *parent;
    union {
        /// variable name for GROUP_VARIABLE
        char                *w;
        /// number value for GROUP_NUMBER
        long double         f;
        /// string value for GROUP_STRING
        struct val_string   s;
    } v;
};

/**
 * Creates a deep copy of src and stores it into dest.
 *
 * @param dest  The destination of the new group.
 * @param src   The source to copy from.
 */
void copy_group(struct group *dest, const struct group *src);

/**
 * Make the group a parent group with n children 0the first child will be the
 * group itself and the parent will have type type.
 *
 * @param group The group to surround.
 * @param type  The type to set the parent to.
 * @param n     The number of children the parent will have.
 *
 * @return The allocated surrounding group.
 */
struct group *surround_group(struct group *group, int type, size_t n);

/**
 * Cleared the memory used by a group, this does not free the group pointer.
 *
 * @param group The group to clear.
 */
void clear_group(struct group *group);

/**
 * Same as clear_group() but it also uses free(group).
 *
 * @param group The group to clear and free.
 */
void free_group(struct group *group);

/* * * Value * * */

/*
 * Values are computed groups that store data depending on which
 * type (enum value_type) they are, see below for the available types.
 */
#define VALUE_NULL      0
#define VALUE_BOOL      1
#define VALUE_NUMBER    2
#define VALUE_STRING    3
#define VALUE_MAX       4

struct value;

struct var_item {
    /// the group this variable is set to
    struct group    group;
    /// the args the group needs
    char            **args;
    /// the number of arguments
    size_t          num_args;
};

struct var_set {
    /// name of the variable
    char            *name;
    /// items the variable has, index into `Parser.items`
    struct var_item *items;
    /// number of items
    size_t          num_items;
};

struct value {
    /// type of the value
    int             type;
    /// literal value
    union {
        /// floating value (VALUE_NUMBER)
        long double         f;
        /// booling value (VALUE_BOOL)
        bool                b;
        /// string value (VALUE_STRING)
        struct val_string   s;
    } v;
};

struct local_var {
    /// the name of the local variable
    char            *name;
    /// value of the local variable
    struct value    value;
};

/**
 * Computes the value of the group.
 *
 * @param group The group to compute.
 * @param value The resulting value.
 *
 * @return -1 if an error occured, 0 otherwise.
 */
int compute_value(/* const */ struct group *group, struct value *value);

/**
 * Copies src into dest, allocates new memory so they are independent.
 *
 * @param dest  The destination of the value.
 * @param src   The source to copy from.
 */
void copy_value(struct value *dest, const struct value *src);

/**
 * Frees resources associated with this value.
 *
 * @param value The value whose resource to free.
 */
void clear_value(struct value *value);

/* * * Parser * * */

extern struct parser {
    /// start (null terminated)
    char *s;
    /// current pointer
    char *p;

    /// read_word (uses n for length)
    char *w;
    /// length of word
    int n;
    /// read_string uses this
    struct val_string str;

    /// root group
    struct group root;
    /// deepest group
    struct group *cur;

    /// words cached
    char **words;
    /// number of words cached
    size_t num_words;

    /// variable sets
    struct var_set *vars;
    /// number of variable sets
    size_t num_vars;
    /// local variables
    struct local_var *locals;
    /// number of local variables
    size_t num_locals;
} Parser;

/**
 * Parser a number in the parser format.
 *
 * @param s     The string containing the number at the front.
 * @param n     The length of `s`, use `SIZE_MAX` for null terminated strings.
 * @param p_end The end position of the number.
 * @param p_f   The result.
 *
 * @return 0 if the number was read or -1 if there is no number.
 */
int parse_number(char *s, size_t n, char **p_end, long double *p_f);

/**
 * Parse given string.
 *
 * This returns NULL when the syntax of the string is invalid otherwise the
 * resulting group when all went normal and the parser is done.
 *
 * @param s The string to parse.
 *
 * @return The parsed group.
 */
struct group *parse(const char *s);

/**
 * Get the first variable index in the list.
 *
 * @param name  Name of the variable.
 *
 * @return Index to the variable within `Parser.vars`.
 */
size_t get_variable(const char *name);

/*
 * Simply adds the named value to the variable list.
 *
 * Note: This does not check if the variable exists already. The name must
 * be contained in the dictionary.
 *
 * @param name  Name of the variable.
 * @param val   Value of the variable.
 * @param dep   The dependencies.
 * @param ndep  The number of dependencies.
 *
 * @return The index of the new variable.
 */
size_t add_variable(char *name, const struct group *val,
        char *const *args, size_t num_args);

/**
 * Gets a local variable.
 *
 * @param name  The name of the local variable, must be in the dictionary.
 *
 * @return SIZE_T if the variable does not exist, otherwise the index to the
 *                local variable within `Parser.locals`.
 */
size_t get_local_variable(char *name);

/**
 * Puts a local variable at the end of the local variable list.
 *
 * @param name  The name of the local variable, must be in the dictionary.
 * @param value The value of the local variable.
 */
void push_local_variable(char *name, struct value *value);

#endif
