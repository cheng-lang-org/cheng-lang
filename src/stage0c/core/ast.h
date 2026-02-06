#ifndef CHENG_STAGE0C_CORE_AST_H
#define CHENG_STAGE0C_CORE_AST_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    NK_ERROR,
    NK_EMPTY,
    NK_MODULE,
    NK_STMT_LIST,
    NK_IMPORT_STMT,
    NK_IMPORT_AS,
    NK_IMPORT_GROUP,
    NK_INCLUDE_STMT,
    NK_FN_DECL,
    NK_ITERATOR_DECL,
    NK_MACRO_DECL,
    NK_TEMPLATE_DECL,
    NK_FORMAL_PARAMS,
    NK_IDENT_DEFS,
    NK_TYPE_DECL,
    NK_OBJECT_DECL,
    NK_ENUM_DECL,
    NK_ENUM_FIELD_DECL,
    NK_CONCEPT_DECL,
    NK_TRAIT_DECL,
    NK_REF_TY,
    NK_PTR_TY,
    NK_VAR_TY,
    NK_TUPLE_TY,
    NK_SET_TY,
    NK_FN_TY,
    NK_REC_LIST,
    NK_REC_CASE,
    NK_GENERIC_PARAMS,
    NK_LET,
    NK_VAR,
    NK_CONST,
    NK_PRAGMA,
    NK_ANNOTATION,
    NK_RETURN,
    NK_YIELD,
    NK_BREAK,
    NK_CONTINUE,
    NK_DEFER,
    NK_IF,
    NK_WHEN,
    NK_WHILE,
    NK_FOR,
    NK_CASE,
    NK_BLOCK,
    NK_OF_BRANCH,
    NK_ELSE,
    NK_ASGN,
    NK_FAST_ASGN,
    NK_CALL,
    NK_INFIX,
    NK_PREFIX,
    NK_POSTFIX,
    NK_CAST,
    NK_DOT_EXPR,
    NK_BRACKET_EXPR,
    NK_CURLY_EXPR,
    NK_PAR,
    NK_TUPLE_LIT,
    NK_SEQ_LIT,
    NK_TABLE_LIT,
    NK_CURLY,
    NK_BRACKET,
    NK_RANGE,
    NK_PATTERN,
    NK_CALL_ARG,
    NK_GUARD,
    NK_LAMBDA,
    NK_DO,
    NK_IF_EXPR,
    NK_WHEN_EXPR,
    NK_CASE_EXPR,
    NK_COMPREHENSION,
    NK_HIDDEN_DEREF,
    NK_DEREF_EXPR,
    NK_IDENT,
    NK_SYMBOL,
    NK_INT_LIT,
    NK_FLOAT_LIT,
    NK_STR_LIT,
    NK_CHAR_LIT,
    NK_BOOL_LIT,
    NK_NIL_LIT
} ChengNodeKind;

typedef struct ChengNode {
    ChengNodeKind kind;
    int line;
    int col;
    int type_id;
    char *ident;
    char *str_val;
    int64_t int_val;
    double float_val;
    uint8_t call_style;
    struct ChengNode **kids;
    size_t len;
    size_t cap;
} ChengNode;

#define CHENG_CALL_STYLE_DEFAULT 0
#define CHENG_CALL_STYLE_SPACE 1
#define CHENG_CALL_STYLE_TYPE_DIAG 2
#define CHENG_CALL_STYLE_PAREN 4

ChengNode *cheng_node_new(ChengNodeKind kind, int line, int col);
void cheng_node_set_ident(ChengNode *node, const char *text);
void cheng_node_set_str(ChengNode *node, const char *text);
int cheng_node_add(ChengNode *parent, ChengNode *child);
void cheng_node_free(ChengNode *node);

#endif
