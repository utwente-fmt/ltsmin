#ifndef LTSMIN_SYNTAX_H
#define LTSMIN_SYNTAX_H

#include <stdbool.h>

#include <hre/user.h>
#include <hre-io/stream.h>
#include <hre/stringindex.h>


/**
 * \brief Enumeration type for mu-calculus expressions.
 */
typedef enum {
    MUCALC_FORMULA,
    MUCALC_MU,
    MUCALC_NU,
    MUCALC_MUST,
    MUCALC_MAY,
    MUCALC_AND,
    MUCALC_OR,
    MUCALC_NOT,
    MUCALC_TRUE,
    MUCALC_FALSE,
    MUCALC_PROPOSITION,
    MUCALC_VAR
} mucalc_type_enum_t;


/**
 * \brief Enumeration type for value types.
 */
typedef enum {
    MUCALC_VALUE_STRING,
    MUCALC_VALUE_NUMBER
} mucalc_value_type_enum_t;


typedef struct mucalc_parse_env_s *mucalc_parse_env_t;

/**
 * \brief Creates mu-calculus parser environment.
 */
mucalc_parse_env_t mucalc_parse_env_create();


/**
 * \brief Clears parser environment from memory. Does not destroy the expressions inside.
 */
void mucalc_parse_env_destroy(mucalc_parse_env_t env);


extern void MucalcParse(void*,int,int,mucalc_parse_env_t);


extern void *MucalcParseAlloc(void *(*mallocProc)(size_t));


extern void MucalcParseFree(void *p,void (*freeProc)(void*));


void mucalc_parse(void *yyp, int yymajor, int yyminor, mucalc_parse_env_t env);


void *mucalc_parse_alloc(void *(*mallocProc)(size_t));


void mucalc_parse_free(void *p, void (*freeProc)(void*));


/**
 * \brief Mu-calculus expression type. Contains the type of the expression, an integer
 * representation of a value (e.g., index of a variable name or action expression),
 * and two subexpression arguments.
 */
typedef struct mucalc_expr_s *mucalc_expr_t;
struct mucalc_expr_s {
    int                 idx;    // the index in the array of expressions
    mucalc_type_enum_t  type;   // the expression type
    int                 value;  // the (index of the) value associated with the expression
    mucalc_expr_t       arg1;
    mucalc_expr_t       arg2;
};


/**
 * \brief Creates a mu-calculus expression object of type <tt>type</tt> with value <tt>value</tt> and expressions
 * <tt>arg1</tt> and <tt>arg2</tt> as arguments (possibly NULL).
 */
mucalc_expr_t mucalc_expr_create(mucalc_parse_env_t env, mucalc_type_enum_t type, int value, mucalc_expr_t arg1, mucalc_expr_t arg2);

void mucalc_expr_destroy(mucalc_expr_t e);


/**
 * \brief Proposition type. Encodes an atomic proposition of the form <tt>p="v"</tt>, where <tt>p</tt> is the name
 * of a part of the state vector and <tt>"v"</tt> is the string representation of a value.
 * The integer <tt>value</tt> is used to store the index of the tuple of part and value.
 * The proposition object is later enriched by storing the index <tt>state_idx</tt> and type number <tt>state_typeno</tt>
 * of the part <tt>p</tt> and index <tt>value_idx</tt> of the value <tt>"v"</tt>.
 */
typedef struct mucalc_proposition {
    int id;                 // the index of the left hand side identifier of the equation.
    int value;              // the index of the right hand side value of the equation.
    int state_idx;          // the index in the state vector that represents id.
    int state_typeno;       // the type of the state vector part at position state_idx.
    int value_idx;          // the index of the value in the chunk table, or the integer representation.
                            // of the value in case of a direct value type.
} mucalc_proposition_t;


/**
 * \brief Adds a proposition with id <tt>id</tt> and value <tt>value</tt> to the parse environment.
 */
int mucalc_add_proposition(mucalc_parse_env_t env, int id, int value);


/**
 * \brief Action expression type.
 */
typedef struct mucalc_action_expression {
    int value;      // the index of the (string) action expression.
    bool negated;   // indicates whether the action expression is negated or not.
} mucalc_action_expression_t;


/**
 * \brief Adds an action expression to the parse environment.
 */
int mucalc_add_action_expression(mucalc_parse_env_t env, int value, bool negated);


/**
 * \brief Action expression type.
 */
typedef struct mucalc_value {
    mucalc_value_type_enum_t type;  // the type of the value (string or number).
    int value;                      // the index of the value (string) or the value itself (number).
} mucalc_value_t;


/**
 * \brief Adds a value to the parse environment.
 */
int mucalc_add_value(mucalc_parse_env_t env, mucalc_value_type_enum_t type, int value);


/**
 * \brief Returns the name of a mu-calculus expression type.
 */
const char* mucalc_type_print(mucalc_type_enum_t type);

/**
 * \brief Fetches the string version of action expression <tt>action_expr</tt>.
 */
const char* mucalc_fetch_action_value(mucalc_parse_env_t env, mucalc_action_expression_t action_expr);

/**
 * \brief Gets the action expression for the modal operator at index <tt>value</tt>.
 */
mucalc_action_expression_t mucalc_get_action_expression(mucalc_parse_env_t env, int value);


/**
 * \brief Fetches the string version of index <tt>value</tt> for type <tt>type</tt>.
 */
const char* mucalc_fetch_value(mucalc_parse_env_t env, mucalc_type_enum_t type, int value);


/**
 * \brief Prints a string representation of an expression object to infoLong.
 */
void mucalc_expr_print(mucalc_parse_env_t env, mucalc_expr_t e);

/**
 * \brief Prints the expression to b and returns the number of bytes in b
 * exclusing the \0 byte. This function writs at most size bytes, including the \0 byte.
 * Parameter b may be null; then it acts as strcpy.
 */
size_t mucalc_print_formula(char* b, size_t size, mucalc_parse_env_t env, mucalc_expr_t expr);

/**
 * \brief Checks if variables in the formula are in the scope of a corresponding
 * fixpoint operator.
 */
bool mucalc_check_formula(mucalc_parse_env_t env, mucalc_expr_t expr);

static const char   MUCALC_ESCAPE_CHAR = '\\';
static const size_t MUCALC_ESCAPE_N = 6;
static const char   MUCALC_ESCAPE_LITERALS[] = {'\\', '\"', '\'', '\n', '\r', '\t'};
static const char   MUCALC_ESCAPE_SYMBOLS[]  = {'\\', '\"', '\'', 'n', 'r', 't'};

/**
 * \brief Escapes a string. Allocates a string using malloc.
 * Replaces '\', '"', ''', '\n', '\t', '\r' by their escaped string representations.
 */
char* mucalc_escape_string(const char* input);

/**
 * \brief Unescapes a string. The reverse of escape_string. Allocates a string using malloc.
 */
char* mucalc_unescape_string(const char* input);

#endif
