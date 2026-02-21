#ifndef SYSTEM_HELPERS_H
#define SYSTEM_HELPERS_H

#include <stdbool.h>
#include <stdint.h>
void* cheng_malloc(int32_t size);
void cheng_free(void* p);
void* cheng_realloc(void* p, int32_t size);
void* cheng_memcpy(void* dest, void* src, int64_t n);
void* cheng_memset(void* dest, int32_t val, int64_t n);
void* cheng_memcpy_ffi(void* dest, void* src, int64_t n);
void* cheng_memset_ffi(void* dest, int32_t val, int64_t n);
void* alloc(int32_t size);
void copyMem(void* dest, void* src, int32_t size);
void setMem(void* dest, int32_t val, int32_t size);
int32_t cheng_memcmp(void* a, void* b, int64_t n);
int32_t cheng_strlen(char* s);
int32_t cheng_strcmp(const char* a, const char* b);
void* ptr_add(void* p, int32_t offset);
uint64_t cheng_ptr_to_u64(void* p);
int32_t cheng_ptr_size(void);
void store_int32(void* p, int32_t val);
int32_t load_int32(void* p);
void store_bool(void* p, int8_t val);
int8_t load_bool(void* p);
void store_ptr(void* p, void* val);
void* load_ptr(void* p);

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

void* get_stdin();
void* get_stdout();
void* get_stderr();
int32_t cheng_fseek(void* stream, int64_t offset, int32_t whence);
int64_t cheng_ftell(void* stream);
#define addr_0(x) (&(x))
void* sys_memset(void* dest, int32_t val, int32_t n);
double cheng_bits_to_f32(int32_t bits);
int32_t cheng_f32_to_bits(double value);
int64_t cheng_f64_to_bits(double value);
int64_t cheng_parse_f64_bits(const char* s);
double cheng_bits_to_f64(int64_t bits);
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
void cheng_abi_borrow_mut_pair_i32(int64_t pairPtr, int32_t da, int32_t db);
uint64_t cheng_ffi_handle_register_ptr(void* ptr);
void* cheng_ffi_handle_resolve_ptr(uint64_t handle);
int32_t cheng_ffi_handle_invalidate(uint64_t handle);
uint64_t cheng_ffi_handle_new_i32(int32_t value);
int32_t cheng_ffi_handle_get_i32(uint64_t handle, int32_t* out_value);
int32_t cheng_ffi_handle_add_i32(uint64_t handle, int32_t delta, int32_t* out_value);
int32_t cheng_ffi_handle_release_i32(uint64_t handle);

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

// Cheng std/os + std/times + std/monotimes minimal support (cross-platform)
int32_t cheng_file_exists(const char* path);
int32_t cheng_dir_exists(const char* path);
int64_t cheng_file_mtime(const char* path);
int64_t cheng_file_size(const char* path);
int32_t cheng_mkdir1(const char* path);
char* cheng_getcwd(void);
double cheng_epoch_time(void);
int64_t cheng_monotime_ns(void);
char* cheng_list_dir(const char* path);
char* cheng_read_file(const char* path);
int32_t cheng_write_file(const char* path, const char* content);
char* cheng_exec_cmd_ex(const char* command, const char* workingDir, int32_t mergeStderr, int64_t* exitCode);
char* chengQ_execQ_cmdQ_ex_0(char* command, char* workingDir, int32_t mergeStderr, int64_t* exitCode);
int32_t cheng_pty_is_supported(void);
int32_t cheng_pty_spawn(const char* command, const char* workingDir, int32_t* outMasterFd, int64_t* outPid);
int32_t cheng_pipe_spawn(const char* command, const char* workingDir, int32_t* outReadFd, int32_t* outWriteFd, int64_t* outPid);
char* cheng_pty_read(int32_t fd, int32_t maxBytes, int32_t* outEof);
char* cheng_fd_read(int32_t fd, int32_t maxBytes, int32_t* outEof);
char* cheng_fd_read_wait(int32_t fd, int32_t maxBytes, int32_t timeoutMs, int32_t* outEof);
int32_t cheng_pty_write(int32_t fd, const char* data, int32_t len);
int32_t cheng_pty_close(int32_t fd);
int32_t cheng_pty_wait(int64_t pid, int32_t* outExitCode);
int32_t cheng_tcp_listener(int32_t port, int32_t* outPort);
int32_t cheng_errno(void);
const char* cheng_strerror(int32_t err);

#endif
