#ifndef SYSTEM_HELPERS_H
#define SYSTEM_HELPERS_H

#include <stdbool.h>
#include <stdint.h>
typedef struct ChengStrBridge {
    const char* ptr;
    int32_t len;
    int32_t store_id;
    int32_t flags;
} ChengStrBridge;

typedef struct ChengBytesBridge {
    void* data;
    int32_t len;
} ChengBytesBridge;

typedef struct ChengErrorInfoBridgeCompat {
    int32_t code;
    ChengStrBridge msg;
} ChengErrorInfoBridgeCompat;

typedef struct ChengSeqHeader {
  int32_t len;
  int32_t cap;
  void* buffer;
} ChengSeqHeader;

typedef struct ChengErrorInfoCompat {
  int32_t code;
  char* msg;
} ChengErrorInfoCompat;

enum {
    CHENG_STR_BRIDGE_FLAG_NONE = 0,
    CHENG_STR_BRIDGE_FLAG_OWNED = 1,
};

void* cheng_malloc(int32_t size);
void cheng_free(void* p);
void* cheng_realloc(void* p, int32_t size);
void* cheng_memcpy(void* dest, void* src, int64_t n);
void* cheng_memset(void* dest, int32_t val, int64_t n);
void* cheng_memcpy_ffi(void* dest, void* src, int64_t n);
void* cheng_memset_ffi(void* dest, int32_t val, int64_t n);
int32_t cheng_system_entropy_fill(void* dst, int32_t len);
void* alloc(int32_t size);
void copyMem(void* dest, void* src, int32_t size);
void setMem(void* dest, int32_t val, int32_t size);
void cheng_zero_mem_compat(void* dest, int32_t size);
int32_t cheng_memcmp(void* a, void* b, int64_t n);
int32_t cheng_strlen(char* s);
int32_t cheng_strcmp(const char* a, const char* b);
char* cheng_str_param_to_cstring_compat(void* raw);
char* cheng_str_to_cstring_temp_bridge(ChengStrBridge s);
int32_t cheng_v3_udp_bind_host_port_bridge(ChengStrBridge host,
                                           int32_t port,
                                           int32_t isV6,
                                           int32_t* outFd,
                                           int32_t* outPort,
                                           int32_t* outFamily,
                                           int32_t* outUseLenField);
int32_t cheng_v3_udp_bind_fd_bridge(ChengStrBridge host, int32_t port, int32_t isV6);
int32_t cheng_v3_udp_bound_port_bridge(int32_t fd, int32_t isV6);
int32_t cheng_v3_udp_platform_use_len_field_bridge(void);
int32_t __cheng_str_eq(const char* a, const char* b);
bool cheng_str_is_empty(const char* s);
bool cheng_str_nonempty(const char* s);
bool cheng_str_has_prefix_bool(const char* s, const char* prefix);
bool cheng_str_contains_char_bool(const char* s, int32_t value);
bool cheng_str_contains_str_bool(const char* s, const char* sub);
char* cheng_str_drop_prefix(const char* s, const char* prefix);
char* cheng_str_slice_bytes(const char* text, int32_t start, int32_t count);
void cheng_str_arc_retain(const char* s);
void cheng_str_arc_release(const char* s);
char* cheng_str_from_bytes_compat(ChengSeqHeader data, int32_t len);
char* cheng_char_to_str_compat(int32_t value);
char* cheng_slice_string_compat(const char* s, int32_t start, int32_t stop, bool exclusive);
bool cheng_str_contains_compat(const char* text, const char* sub);
char* cheng_seq_string_repr_compat(ChengSeqHeader xs);
char* cheng_seq_bool_repr_compat(ChengSeqHeader xs);
char* cheng_seq_i32_repr_compat(ChengSeqHeader xs);
char* cheng_seq_i64_repr_compat(ChengSeqHeader xs);
ChengSeqHeader cheng_seq_return_string_compat(ChengSeqHeader out);
void* ptr_add(void* p, int32_t offset);
void* rawmemAsVoid(void* p);
uint64_t cheng_ptr_to_u64(void* p);
int32_t cheng_ptr_size(void);
int32_t elemSize_ptr(void);
void __cheng_setCmdLine(int32_t argc, const char** argv);
int32_t __cheng_rt_paramCount(void);
const char* __cheng_rt_paramStr(int32_t i);
char* __cheng_rt_paramStrCopy(int32_t i);
ChengStrBridge __cheng_rt_paramStrCopyBridge(int32_t i);
void __cheng_rt_paramStrCopyBridgeInto(int32_t i, ChengStrBridge* out);
int32_t cmdCountFromRuntime(void);
int32_t __cmdCountFromRuntime(void);
bool cheng_cmd_ready_compat(void);
char* cheng_program_name_compat(void);
char* cheng_param_str_compat(int32_t i);
int32_t paramCount(void);
const char* paramStr(int32_t i);
void store_int32(void* p, int32_t val);
int32_t load_int32(void* p);
void store_bool(void* p, int8_t val);
int8_t load_bool(void* p);
void store_ptr(void* p, void* val);
void* load_ptr(void* p);
ChengStrBridge load(const ChengStrBridge* p);
void cheng_str_bridge_load_into(void* out_raw, const ChengStrBridge* p);
void store(ChengStrBridge* p, ChengStrBridge val);
void* cheng_ptr_seq_get_value(ChengSeqHeader seq, int32_t idx);
void* cheng_ptr_seq_get_compat(ChengSeqHeader seq, int32_t idx);
void cheng_ptr_seq_set_compat(ChengSeqHeader* seq_ptr, int32_t idx, void* value);
void cheng_error_info_init_compat(ChengErrorInfoCompat* out, int32_t code, char* msg);
int32_t cheng_error_info_code_compat(ChengErrorInfoCompat e);
char* cheng_error_info_msg_compat(ChengErrorInfoCompat e);
void cheng_error_info_bridge_ok(void* out_raw);
void cheng_error_info_bridge_new(void* out_raw, ChengStrBridge msg);
void cheng_error_info_bridge_copy(void* out_raw, int32_t code, ChengStrBridge msg);
void cheng_error_info_bridge_copy_from(void* out_raw, ChengErrorInfoBridgeCompat src);
int32_t cheng_error_info_bridge_code(ChengErrorInfoBridgeCompat src);
void cheng_error_info_bridge_msg_into(void* out_raw, ChengErrorInfoBridgeCompat src);
ChengStrBridge cheng_error_info_bridge_msg(ChengErrorInfoBridgeCompat src);
int32_t cheng_seq_header_len_get(void* seq_ptr);
void cheng_seq_header_len_set(void* seq_ptr, int32_t value);
int32_t cheng_seq_header_cap_get(void* seq_ptr);
void cheng_seq_header_cap_set(void* seq_ptr, int32_t value);
void* cheng_seq_header_buffer_get(void* seq_ptr);
void cheng_seq_header_buffer_set(void* seq_ptr, void* value);
int32_t cheng_seq_next_cap(int32_t cur_cap, int32_t need_len);
void cheng_seq_grow_to_raw(void** p_buffer, int32_t* p_cap, int32_t min_cap, int32_t elem_size);
void cheng_seq_grow_inst(void* seq_ptr, int32_t min_cap, int32_t elem_size);
void cheng_seq_zero_tail_raw(void* buffer, int32_t seq_cap, int32_t len, int32_t target, int32_t elem_size);
int32_t cheng_seq_string_init_cap(int32_t seq_len, int32_t seq_cap);
int32_t cheng_seq_string_elem_bytes_compat(void);
void* cheng_seq_string_alloc_compat(int32_t buf_cap);
void cheng_seq_string_init_compat(void* seq_ptr, int32_t seq_len, int32_t seq_cap);
ChengSeqHeader cheng_new_seq_string_compat(int32_t seq_len, int32_t seq_cap);
void cheng_seq_string_register_compat(void* seq_ptr);
void cheng_seq_string_buffer_register_compat(void* buffer);
void cheng_seq_free(void* seq_ptr);
void cheng_seq_arc_retain(ChengSeqHeader seq);
void cheng_seq_arc_release(ChengSeqHeader seq);
char* cheng_seq_string_get_compat(ChengSeqHeader seq, int32_t at);
void cheng_seq_string_add_compat(void* seq_ptr, const char* value);
int32_t cheng_rawbytes_get_at(void* base, int32_t idx);
void cheng_rawbytes_set_at(void* base, int32_t idx, int32_t value);
ChengBytesBridge cheng_v3_test_pki_hex_decode_bridge(ChengStrBridge text);
void cheng_v3_test_pki_hex_decode_into_bridge(ChengStrBridge text, ChengBytesBridge* out);
void* cheng_v3_test_pki_hex_decode_ptr_bridge(ChengStrBridge text);
int32_t cheng_v3_test_pki_hex_decode_len_bridge(ChengStrBridge text);
void cheng_rawmem_write_i8(void* dst, int32_t idx, int8_t value);
void cheng_rawmem_write_char(void* dst, int32_t idx, int32_t value);
int32_t cheng_force_segv(void);
void cheng_func_ptr_shadow_remember(void* p);
void* cheng_func_ptr_shadow_recover(uint64_t p);
void* cheng_machine_inst_new(int32_t op, int32_t rd, int32_t rn, int32_t rm,
                             int32_t ra, int64_t imm, const char* label, int32_t cond);
void* cheng_machine_inst_new_n(int32_t op, int32_t rd, int32_t rn, int32_t rm,
                               int32_t ra, int64_t imm, const char* label, int32_t label_len,
                               int32_t cond);
void* cheng_machine_inst_clone(void* inst);
int32_t cheng_machine_inst_valid(void* inst);
int32_t cheng_machine_inst_op(void* inst);
int32_t cheng_machine_inst_rd(void* inst);
int32_t cheng_machine_inst_rn(void* inst);
int32_t cheng_machine_inst_rm(void* inst);
int32_t cheng_machine_inst_ra(void* inst);
int64_t cheng_machine_inst_imm(void* inst);
char* cheng_machine_inst_label(void* inst);
int32_t cheng_machine_inst_cond(void* inst);
int32_t driver_c_chr_i32(int32_t value);
int32_t driver_c_ord_char(int32_t value);
int32_t driver_c_chr_i32_compat(int32_t value);
int32_t driver_c_ord_char_compat(int32_t value);
char* driver_c_bool_to_str(bool value);
bool driver_c_bool_identity(bool value);

int32_t xor_0(int32_t a, int32_t b);
int32_t shl_0(int32_t a, int32_t b);
int32_t shr_0(int32_t a, int32_t b);
int32_t mod_0(int32_t a, int32_t b);
int32_t bitand_0(int32_t a, int32_t b);
int32_t bitor_0(int32_t a, int32_t b);
int32_t bitnot_0(int32_t a);
int32_t mul_0(int32_t a, int32_t b);
int32_t div_0(int32_t a, int32_t b);
bool not_0(bool a);
int32_t cheng_puts(const char* text);
void cheng_exit(int32_t code);
void cheng_bounds_check(int32_t len, int32_t idx);
void* cheng_seq_get(void* buffer, int32_t len, int32_t idx, int32_t elem_size);
void* cheng_seq_set(void* buffer, int32_t len, int32_t idx, int32_t elem_size);
void* cheng_seq_set_grow(void* seq_ptr, int32_t idx, int32_t elem_size);
void* cheng_slice_get(void* ptr, int32_t len, int32_t idx, int32_t elem_size);
void* cheng_slice_set(void* ptr, int32_t len, int32_t idx, int32_t elem_size);

struct ChengAwaitI32;
struct ChengAwaitVoid;
struct ChengChanI32;

void cheng_spawn(void* fn_ptr, void* ctx);
int32_t cheng_sched_pending(void);
int32_t cheng_sched_run_once(void);
void cheng_sched_run(void);
int32_t cheng_thread_spawn(void* fn_ptr, void* ctx);
int32_t cheng_thread_spawn_i32(void* fn_ptr, int32_t ctx);
int32_t cheng_thread_parallelism(void);
void cheng_thread_yield(void);
int32_t cheng_thread_local_i32_get(int32_t slot);
void cheng_thread_local_i32_set(int32_t slot, int32_t value);

struct ChengAwaitI32* cheng_async_pending_i32(void);
struct ChengAwaitI32* cheng_async_ready_i32(int32_t value);
void cheng_async_set_i32(struct ChengAwaitI32* st, int32_t value);
int32_t cheng_await_i32(struct ChengAwaitI32* st);

struct ChengAwaitVoid* cheng_async_pending_void(void);
struct ChengAwaitVoid* cheng_async_ready_void(void);
void cheng_async_set_void(struct ChengAwaitVoid* st);
void cheng_await_void(struct ChengAwaitVoid* st);

struct ChengChanI32* cheng_chan_i32_new(int32_t cap);
int32_t cheng_chan_i32_send(struct ChengChanI32* ch, int32_t value);
int32_t cheng_chan_i32_recv(struct ChengChanI32* ch, int32_t* out);

void* get_stdin(void);
void* get_stdout(void);
void* get_stderr(void);
int32_t cheng_fseek(void* stream, int64_t offset, int32_t whence);
int64_t cheng_ftell(void* stream);
#define addr_0(x) (&(x))
void* sys_memset(void* dest, int32_t val, int32_t n);
double cheng_bits_to_f32(int32_t bits);
int32_t cheng_f32_to_bits(double value);
int64_t cheng_f64_to_bits(double value);
int64_t cheng_parse_f64_bits(const char* s);
double cheng_bits_to_f64(int64_t bits);
bool cheng_f64_bits_is_nan(int64_t bits);
bool cheng_f64_bits_is_zero(int64_t bits);
uint64_t cheng_f64_bits_order(int64_t bits);
int64_t cheng_f64_add_bits(int64_t a_bits, int64_t b_bits);
int64_t cheng_f64_sub_bits(int64_t a_bits, int64_t b_bits);
int64_t cheng_f64_mul_bits(int64_t a_bits, int64_t b_bits);
int64_t cheng_f64_div_bits(int64_t a_bits, int64_t b_bits);
int64_t cheng_f64_neg_bits(int64_t a_bits);
int64_t cheng_f64_lt_bits(int64_t a_bits, int64_t b_bits);
int64_t cheng_f64_le_bits(int64_t a_bits, int64_t b_bits);
int64_t cheng_f64_gt_bits(int64_t a_bits, int64_t b_bits);
int64_t cheng_f64_ge_bits(int64_t a_bits, int64_t b_bits);
int64_t cheng_i64_to_f64_bits(int64_t x);
int64_t cheng_u64_to_f64_bits(uint64_t x);
int64_t cheng_f64_bits_to_i64(int64_t bits);
uint64_t cheng_f64_bits_to_u64(int64_t bits);
int64_t cheng_f32_bits_to_f64_bits(int32_t bits);
int32_t cheng_f64_bits_to_f32_bits(int64_t bits);
int64_t cheng_f32_bits_to_i64(int32_t bits);
uint64_t cheng_f32_bits_to_u64(int32_t bits);
int32_t cheng_jpeg_decode(const uint8_t* data, int32_t len, int32_t* out_w, int32_t* out_h, uint8_t** out_rgba);
void cheng_jpeg_free(void* p);
int32_t cheng_abi_sum_seq_i32(uint64_t seqLike0, uint64_t seqLike1, uint64_t seqLike2);
int32_t cheng_abi_sum_ptr_len_i32(const int32_t* ptr, int32_t len);
void cheng_abi_borrow_mut_pair_i32(int64_t pairPtr, int32_t da, int32_t db);
uint64_t cheng_ffi_handle_register_ptr(void* ptr);
void* cheng_ffi_handle_resolve_ptr(uint64_t handle);
int32_t cheng_ffi_handle_invalidate(uint64_t handle);
uint64_t cheng_ffi_handle_new_i32(int32_t value);
int32_t cheng_ffi_handle_get_i32(uint64_t handle, int32_t* out_value);
int32_t cheng_ffi_handle_add_i32(uint64_t handle, int32_t delta, int32_t* out_value);
int32_t cheng_ffi_handle_release_i32(uint64_t handle);
void* cheng_ffi_raw_new_i32(int32_t value);
int32_t cheng_ffi_raw_get_i32(void* p);
int32_t cheng_ffi_raw_add_i32(void* p, int32_t delta);
int32_t cheng_ffi_raw_release_i32(void* p);
int32_t cheng_crash_trace_enabled(void);
void cheng_crash_trace_set_phase(const char* phase);
void cheng_crash_trace_mark(const char* tag, int32_t a, int32_t b, int32_t c, int32_t d);
void cheng_dump_backtrace_if_enabled(void);

void* cheng_mem_scope_push(void);
void cheng_mem_scope_pop(void);
void cheng_mem_scope_escape(void* p);
void cheng_mem_scope_escape_global(void* p);
void cheng_mem_retain(void* p);
void cheng_mem_release(void* p);
int32_t cheng_mem_refcount(void* p);
void cheng_mem_retain_atomic(void* p);
void cheng_mem_release_atomic(void* p);
int32_t cheng_mem_refcount_atomic(void* p);
int32_t cheng_atomic_cas_i32(int32_t* p, int32_t expect, int32_t desired);
void cheng_atomic_store_i32(int32_t* p, int32_t val);
int32_t cheng_atomic_load_i32(int32_t* p);

ChengStrBridge driver_c_str_from_utf8_copy_bridge(const char* raw, int32_t n);
ChengStrBridge driver_c_str_clone_bridge(const char* s);
ChengStrBridge driver_c_str_slice_bridge(ChengStrBridge s, int32_t start, int32_t count);
ChengStrBridge driver_c_str_concat_bridge(ChengStrBridge a, ChengStrBridge b);
ChengStrBridge driver_c_char_to_str_bridge(int32_t value);
int32_t driver_c_str_eq_bridge(ChengStrBridge a, ChengStrBridge b);
int32_t driver_c_str_eq_raw_bridge(ChengStrBridge actual, const char* expected_ptr, int32_t expected_len);
int32_t driver_c_str_has_prefix_bridge(ChengStrBridge s, ChengStrBridge prefix);
int32_t driver_c_str_has_suffix_bridge(ChengStrBridge s, ChengStrBridge suffix);
int32_t driver_c_str_contains_char_bridge(ChengStrBridge s, int32_t value);
int32_t driver_c_str_contains_str_bridge(ChengStrBridge s, ChengStrBridge sub);
ChengStrBridge cheng_v3_str_strip_bridge(ChengStrBridge s);
int32_t cheng_v3_os_is_absolute_bridge(ChengStrBridge path);
ChengStrBridge cheng_v3_os_join_path_bridge(ChengStrBridge left, ChengStrBridge right);
int32_t cheng_v3_os_file_exists_bridge(ChengStrBridge path);
int32_t cheng_v3_os_dir_exists_bridge(ChengStrBridge path);
int64_t cheng_v3_os_file_size_bridge(ChengStrBridge path);
ChengStrBridge cheng_v3_read_file_bridge(ChengStrBridge path);
int32_t cheng_v3_tcp_loopback_payload_bridge(ChengStrBridge protocol_text,
                                             ChengStrBridge payload_text,
                                             ChengStrBridge* response_text,
                                             ChengStrBridge* err_text);
int32_t cheng_v3_tcp_loopback_request_response_bridge(ChengStrBridge protocol_text,
                                                      ChengStrBridge request_text,
                                                      ChengStrBridge response_text,
                                                      ChengStrBridge* client_response_text,
                                                      ChengStrBridge* err_text);
int32_t cheng_v3_tcp_serve_payload_once_bridge(ChengStrBridge host_text,
                                               int32_t port,
                                               ChengStrBridge protocol_text,
                                               ChengStrBridge payload_text,
                                               ChengStrBridge ready_path_text,
                                               ChengStrBridge* err_text);
int32_t cheng_v3_tcp_recv_payload_once_bridge(ChengStrBridge host_text,
                                              int32_t port,
                                              ChengStrBridge protocol_text,
                                              ChengStrBridge* response_text,
                                              ChengStrBridge* err_text);
ChengStrBridge cheng_v3_tcp_loopback_multistream_bridge(ChengStrBridge protocol_text);
ChengStrBridge cheng_v3_sha256_hex_bridge(ChengStrBridge text);
int64_t cheng_v3_sha256_word_bridge(ChengStrBridge text, int32_t word_index);
int32_t driver_c_cli_param1_eq_bridge(ChengStrBridge expected);
int32_t driver_c_cli_param1_eq_raw_bridge(const char* expected_ptr, int32_t expected_len);
int32_t driver_c_compiler_core_print_usage_bridge(void);
int32_t driver_c_compiler_core_print_status_bridge(void);
ChengStrBridge driver_c_exec_file_capture_or_panic_bridge(ChengStrBridge file_path,
                                                          ChengSeqHeader argv,
                                                          ChengStrBridge working_dir,
                                                          ChengStrBridge label);
void driver_c_compare_text_files_or_panic_bridge(ChengStrBridge left_path,
                                                 ChengStrBridge right_path,
                                                 ChengStrBridge label);
ChengStrBridge driver_c_read_flag_or_default_bridge(ChengStrBridge key, ChengStrBridge default_value);
int32_t driver_c_read_flag_value_bridge(ChengStrBridge key, ChengStrBridge* out_value);
int32_t driver_c_read_int32_flag_or_default_bridge(ChengStrBridge key, int32_t default_value);
ChengStrBridge driver_c_absolute_path_bridge(ChengStrBridge path);
ChengStrBridge driver_c_program_absolute_path_bridge(void);
ChengStrBridge driver_c_get_current_dir_bridge(void);
ChengStrBridge driver_c_join_path2_bridge(ChengStrBridge left, ChengStrBridge right);
void driver_c_create_dir_all_bridge(ChengStrBridge path);
void driver_c_write_text_file_bridge(ChengStrBridge path, ChengStrBridge content);
int32_t driver_c_compare_text_files_bridge(ChengStrBridge left_path, ChengStrBridge right_path);
int32_t driver_c_compare_binary_files_bridge(ChengStrBridge left_path, ChengStrBridge right_path);
ChengStrBridge driver_c_extract_line_value_bridge(ChengStrBridge text, ChengStrBridge key);
int32_t driver_c_count_external_cc_providers_bridge(ChengStrBridge plan_text);
int32_t driver_c_parse_plan_int32_or_zero_bridge(ChengStrBridge plan_text, ChengStrBridge key);
ChengStrBridge driver_c_provider_field_for_module_from_plan_text_bridge(ChengStrBridge plan_text,
                                                                        ChengStrBridge module_name,
                                                                        ChengStrBridge field_name);
ChengStrBridge driver_c_compiler_core_provider_source_kind_bridge(ChengStrBridge plan_text);
ChengStrBridge driver_c_compiler_core_provider_compile_mode_bridge(ChengStrBridge plan_text);
int32_t driver_c_run_stage_selfhost_host_bridge(void);
int32_t driver_c_run_tooling_selfhost_host_bridge(void);
int32_t driver_c_run_tooling_selfhost_check_bridge(void);
int32_t driver_c_run_program_selfhost_check_bridge(void);
int32_t driver_c_compiler_core_tooling_local_payload_bridge(const char* payload);
int32_t driver_c_compiler_core_program_local_payload_bridge(const char* payload);
int32_t driver_c_compiler_core_program_argv_bridge(int32_t argc, const char** argv);
int32_t driver_c_compiler_core_local_payload_bridge(const char* payload);
ChengStrBridge driver_c_machine_target_architecture_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_obj_format_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_symbol_prefix_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_text_section_name_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_cstring_section_name_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_symtab_section_name_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_strtab_section_name_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_reloc_section_name_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_call_relocation_kind_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_metadata_page_relocation_kind_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_metadata_pageoff_relocation_kind_bridge(ChengStrBridge target);
int32_t driver_c_machine_target_pointer_width_bits_bridge(ChengStrBridge target);
int32_t driver_c_machine_target_stack_align_bytes_bridge(ChengStrBridge target);
int32_t driver_c_machine_target_text_align_pow2_bridge(ChengStrBridge target);
int32_t driver_c_machine_target_cstring_align_pow2_bridge(ChengStrBridge target);
int32_t driver_c_machine_target_darwin_platform_id_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_darwin_platform_name_bridge(ChengStrBridge target);
int32_t driver_c_machine_target_darwin_minos_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_darwin_minos_text_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_darwin_arch_name_bridge(ChengStrBridge target);
ChengStrBridge driver_c_machine_target_darwin_sdk_name_bridge(ChengStrBridge target);
int32_t driver_c_str_get_at_bridge(ChengStrBridge s, int32_t idx);
void driver_c_str_set_at_bridge(ChengStrBridge* s, int32_t idx, int32_t value);
ChengStrBridge driver_c_getenv_copy_bridge(const char* name);
void driver_c_getenv_copy_bridge_into(const char* name, ChengStrBridge* out);
ChengStrBridge driver_c_cli_input_copy_bridge(void);
void driver_c_cli_input_copy_bridge_into(ChengStrBridge* out);
ChengStrBridge driver_c_cli_output_copy_bridge(void);
void driver_c_cli_output_copy_bridge_into(ChengStrBridge* out);
ChengStrBridge driver_c_cli_target_copy_bridge(void);
void driver_c_cli_target_copy_bridge_into(ChengStrBridge* out);
ChengStrBridge driver_c_cli_linker_copy_bridge(void);
void driver_c_cli_linker_copy_bridge_into(ChengStrBridge* out);
ChengStrBridge driver_c_read_file_all_bridge(const char* path);
void driver_c_read_file_all_bridge_into(const char* path, ChengStrBridge* out);
void* driver_c_build_module_stage1_direct(const char* input_path, const char* target);

// Cheng std/os + std/times + std/monotimes minimal support (cross-platform)
int32_t cheng_file_exists(const char* path);
int32_t cheng_dir_exists(const char* path);
int64_t cheng_file_mtime(const char* path);
int64_t cheng_file_size(const char* path);
int32_t cheng_mkdir1(const char* path);
char* cheng_getcwd(void);
double cheng_epoch_time(void);
int64_t cheng_epoch_time_seconds(void);
int64_t cheng_monotime_ns(void);
char* cheng_list_dir(const char* path);
char* cheng_read_file(const char* path);
int32_t cheng_write_file(const char* path, const char* content);
char* cheng_exec_cmd_ex(const char* command, const char* workingDir, int32_t mergeStderr, int64_t* exitCode);
char* cheng_exec_file_capture(const char* filePath, void* argvSeqPtr, void* envOverridesSeqPtr,
                              const char* workingDir, int32_t mergeStderr, int32_t timeoutSec,
                              int64_t* exitCode);
int64_t cheng_exec_file_status(const char* filePath, void* argvSeqPtr, void* envOverridesSeqPtr,
                               const char* workingDir, int32_t timeoutSec);
char* chengQ_execQ_cmdQ_ex_0(char* command, char* workingDir, int32_t mergeStderr, int64_t* exitCode);
int32_t cheng_pty_is_supported(void);
int32_t cheng_pty_spawn(const char* command, const char* workingDir, int32_t* outMasterFd, int64_t* outPid);
int32_t cheng_pipe_spawn(const char* command, const char* workingDir, int32_t* outReadFd, int32_t* outWriteFd, int64_t* outPid);
char* cheng_pty_read(int32_t fd, int32_t maxBytes, int32_t* outEof);
char* cheng_fd_read(int32_t fd, int32_t maxBytes, int32_t* outEof);
char* cheng_fd_read_wait(int32_t fd, int32_t maxBytes, int32_t timeoutMs, int32_t* outEof);
int32_t cheng_fd_write(int32_t fd, const char* data, int32_t len);
int32_t cheng_pty_write(int32_t fd, const char* data, int32_t len);
int32_t cheng_pty_close(int32_t fd);
int32_t cheng_pty_wait(int64_t pid, int32_t* outExitCode);
int32_t cheng_tcp_listener(int32_t port, int32_t* outPort);
int32_t cheng_errno(void);
const char* cheng_strerror(int32_t err);
int32_t cheng_recvfrom_fd_ex(int32_t fd, void* buf, int32_t len, int32_t flags, void* addr, void* addrlen, int32_t* outErr);
int32_t cheng_v3_udp_recvfrom_addr_bridge(int32_t fd, void* buf, int32_t len, int32_t flags, void* addr, int32_t addrCap, int32_t* outAddrLen, int32_t* outErr);

#endif
