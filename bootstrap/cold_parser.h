/* cold_parser.h -- declarations for the source parser (cold_parser.c)
 *
 * This header forward-declares the shared struct types to avoid pulling in
 * the full type definitions from cold_types.h.  cheng_cold.c includes this
 * header to call parser functions.  cold_parser.c includes both cold_types.h
 * (for full type definitions) and cold_parser.h (for its own declarations).
 */
#ifndef COLD_PARSER_H
#define COLD_PARSER_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * Forward declarations of struct types (defined in cold_types.h or cheng_cold.c)
 * ================================================================ */
typedef struct Arena Arena;
typedef struct Span Span;
typedef struct BodyIR BodyIR;
typedef struct Symbols Symbols;
typedef struct Locals Locals;
typedef struct LoopCtx LoopCtx;
typedef struct Parser Parser;
typedef struct ColdImportSource ColdImportSource;
typedef struct ColdCompileStats ColdCompileStats;
typedef struct ObjectField ObjectField;
typedef struct ObjectDef ObjectDef;
typedef struct TypeDef TypeDef;
typedef struct Variant Variant;

/* ================================================================
 * Parser state machine
 * ================================================================ */
Parser parser_child(Parser *owner, Span source);
void parser_ws(Parser *parser);
void parser_line(Parser *parser);
void parser_inline_ws(Parser *parser);
Span parser_token(Parser *parser);
Span parser_peek(Parser *parser);
bool parser_take(Parser *parser, const char *text);
Span parser_take_type_span(Parser *parser);
void parser_skip_balanced(Parser *parser, const char *open, const char *close);
int32_t parser_next_indent(Parser *parser);

/* ================================================================
 * Import parsing
 * ================================================================ */
bool cold_parse_import_line(Span trimmed, Span *module_out, Span *alias_out);
bool cold_import_source_path(Span module_path, char *out, size_t out_cap);
Span cold_qualify_import_type(Arena *arena, Span alias, Span type);
Span cold_import_default_alias(Span module_path);

/* ================================================================
 * Signature/type/const collection
 * ================================================================ */
void cold_collect_imported_function_signatures(Symbols *symbols, Span source);
void cold_collect_function_signatures(Symbols *symbols, Span source);
void cold_collect_import_module_types(Symbols *symbols, Span alias, Span source);
void cold_collect_import_module_enum_blocks(Symbols *symbols, Span alias, Span source);
void cold_collect_import_module_consts(Symbols *symbols, Span alias, Span source);
void cold_collect_import_module_signatures(Symbols *symbols, Span alias, Span source);

/* ================================================================
 * Source type/const/function/expression parsing
 * ================================================================ */
void parse_type(Parser *parser);
void parse_const(Parser *parser);
BodyIR *parse_fn(Parser *parser, int32_t *symbol_index_out);

int32_t parse_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);
int32_t parse_term(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);
int32_t parse_statement(Parser *parser, BodyIR *body, Locals *locals,
                        int32_t block, LoopCtx *loop);
int32_t parse_call_after_name(Parser *parser, BodyIR *body, Locals *locals,
                              Span name, int32_t *kind);
int32_t parse_call_from_args_span(Parser *owner, BodyIR *body, Locals *locals,
                                  Span name, Span args, int32_t *kind);
int32_t parse_let_binding(Parser *parser, BodyIR *body, Locals *locals,
                          int32_t block, bool is_var);
int32_t parse_arith_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);
int32_t parse_compare_expr(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);
int32_t parse_primary(Parser *parser, BodyIR *body, Locals *locals, int32_t *kind);
int32_t parse_postfix(Parser *parser, BodyIR *body, Locals *locals,
                      int32_t slot, int32_t *kind);
int32_t parse_if(Parser *parser, BodyIR *body, Locals *locals,
                 int32_t block, int32_t stmt_indent, LoopCtx *loop);
int32_t parse_for(Parser *parser, BodyIR *body, Locals *locals,
                  int32_t block, int32_t stmt_indent, LoopCtx *loop);
int32_t parse_while(Parser *parser, BodyIR *body, Locals *locals,
                    int32_t block, int32_t stmt_indent, LoopCtx *loop);
int32_t parse_match(Parser *parser, BodyIR *body, Locals *locals,
                    int32_t block, LoopCtx *loop);
void parse_return(Parser *parser, BodyIR *body, Locals *locals, int32_t block);
void parse_assign(Parser *parser, BodyIR *body, Locals *locals, Span name);
int32_t parse_field_assign(Parser *parser, BodyIR *body, Locals *locals,
                           Span name, int32_t seq_block);
int32_t parse_statements_until(Parser *parser, BodyIR *body, Locals *locals,
                               int32_t block, int32_t end_indent,
                               const char *stop_token, LoopCtx *loop);
int32_t parse_expr_from_span(Parser *owner, BodyIR *body, Locals *locals,
                              Span expr, int32_t *kind,
                              const char *trailing_message);
int32_t parse_constructor(Parser *parser, BodyIR *body, Locals *locals,
                           Variant *variant);
int32_t cold_arg_reg_count(int32_t kind, int32_t size);
int32_t parse_param_specs(Symbols *symbols, Span params, Span *names,
                          int32_t *kinds, int32_t *sizes, Span *types,
                          int32_t cap);
int32_t parse_i32_seq_literal(Parser *parser, BodyIR *body, Locals *locals,
                              int32_t *kind);
int32_t parse_expected_payloadless_variant(Parser *parser, BodyIR *body,
                                           TypeDef *type, int32_t *kind);
int32_t parse_seq_lvalue_from_span(Parser *owner, BodyIR *body, Locals *locals,
                                   Span target, int32_t *kind_out);
int32_t body_branch_to(BodyIR *body, int32_t block, int32_t target_block);
void parse_condition_span(Parser *owner, BodyIR *body, Locals *locals,
                          Span condition, int32_t block,
                          int32_t true_block, int32_t false_block);
void loop_add_break(LoopCtx *loop, int32_t term);
void loop_add_continue(LoopCtx *loop, int32_t term);
int32_t cold_imported_param_specs(Symbols *symbols, Span alias, Span params,
                                  Span *names, int32_t *kinds,
                                  int32_t *sizes, int32_t cap);
Span cold_arena_join3(Arena *arena, Span a, const char *mid, Span b);
int32_t cold_make_str_literal_cstr_slot(BodyIR *body, const char *text);

/* ================================================================
 * Import compilation (called from backend dispatch)
 * ================================================================ */
int32_t cold_collect_direct_import_sources(Span entry_source,
                                           ColdImportSource *imports,
                                           int32_t import_cap);
int32_t cold_compile_imported_bodies_no_recurse(Symbols *symbols, Span entry_source,
                                                BodyIR **function_bodies,
                                                int32_t body_cap);
void cold_compile_reachable_import_bodies(Symbols *symbols,
                                          BodyIR **function_bodies,
                                          int32_t body_cap,
                                          int32_t entry_function,
                                          ColdImportSource *imports,
                                          int32_t import_count);
void cold_collect_all_transitive_imports(Symbols *symbols, Span entry_source);

/* ================================================================
 * Compile pipeline — available in full build only
 * ================================================================ */
#ifndef COLD_BACKEND_ONLY
bool cold_compile_source_path_to_macho(const char *out_path,
                                       const char *src_path,
                                       bool allow_demo,
                                       ColdCompileStats *stats);
bool cold_compile_source_to_object(const char *out_path,
                                   const char *src_path,
                                   const char *target);

int cold_cmd_compile_bootstrap(int argc, char **argv, const char *self_path);
int cold_cmd_bootstrap_bridge(int argc, char **argv, const char *self_path);
int cold_cmd_build_backend_driver(int argc, char **argv, const char *self_path);
#endif /* COLD_BACKEND_ONLY */

#endif /* COLD_PARSER_H */
