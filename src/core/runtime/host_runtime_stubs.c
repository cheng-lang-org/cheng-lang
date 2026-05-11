#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <poll.h>
#define W __attribute__((weak))
W void* cheng_host_fopen(const char* p, const char* m) { return fopen(p,m); }
W int cheng_host_fclose(void* fp) { return fclose((FILE*)fp); }
W int cheng_host_fflush(void* fp) { return fflush((FILE*)fp); }
W size_t cheng_host_fread(void* ptr, size_t s, size_t n, void* fp) { return fread(ptr,s,n,(FILE*)fp); }
W size_t cheng_host_fwrite(const void* ptr, size_t s, size_t n, void* fp) { return fwrite(ptr,s,n,(FILE*)fp); }
W int cheng_host_fgetc(void* fp) { return fgetc((FILE*)fp); }
W char* cheng_host_fgets(char* b, int sz, void* fp) { return fgets(b,sz,(FILE*)fp); }
W int cheng_host_fseek(void* fp, long off, int w) { return fseek((FILE*)fp,off,w); }
W long cheng_host_ftell(void* fp) { return ftell((FILE*)fp); }
W void* cheng_host_fdopen(int fd, const char* m) { return fdopen(fd,m); }
W int cheng_host_fcntl(int fd, int cmd, int arg) { return fcntl(fd,cmd,arg); }
W void* cheng_host_malloc(size_t sz) { return malloc(sz); }
W void cheng_host_free(void* p) { free(p); }
W void* cheng_host_realloc(void* p, size_t sz) { return realloc(p,sz); }
W void cheng_host_exit_runtime(int c) { exit(c); }
W void cheng_host_exit_immediate_runtime(int c) { _exit(c); }
W int cheng_host_fork_runtime(void) { return fork(); }
W int cheng_host_execvp(const char* f, char* const a[]) { return execvp(f,a); }
W int cheng_host_waitpid_runtime(int pid, int* s, int opts) { return waitpid(pid,s,opts); }
W int cheng_host_setpgid_runtime(int pid, int pgid) { return setpgid(pid,pgid); }
W int cheng_host_setsid_runtime(void) { return setsid(); }
W int cheng_host_open_runtime(const char* p, int fl, int m) { return open(p,fl,m); }
W int cheng_host_close(int fd) { return close(fd); }
W ssize_t cheng_host_read_runtime(int fd, void* b, size_t c) { return read(fd,b,c); }
W ssize_t cheng_host_write_runtime(int fd, const void* b, size_t c) { return write(fd,b,c); }
W int cheng_host_creat_runtime(const char* p, int m) { return creat(p,m); }
W int cheng_host_dup2_runtime(int old, int n) { return dup2(old,n); }
W int cheng_host_pipe_runtime(int p[2]) { return pipe(p); }
W int cheng_host_poll(struct pollfd* fds, unsigned long n, int t) { return poll(fds,n,t); }
W int cheng_host_ioctl_runtime(int fd, unsigned long req, void* arg) { return ioctl(fd,req,arg); }
W void* cheng_host_opendir(const char* p) { return opendir(p); }
W int cheng_host_closedir(void* d) { return closedir((DIR*)d); }
W int cheng_host_list_dir(const char* p, void* b, size_t sz) { return 0; }
W int cheng_host_mkdir(const char* p, int m) { return mkdir(p,m); }
W int cheng_host_stat(const char* p, void* b) { return stat(p,(struct stat*)b); }
W int cheng_host_statfs(const char* p, void* b) { return 0; }
W char* cheng_host_getenv(const char* n) { return getenv(n); }
W int cheng_host_setenv(const char* n, const char* v, int o) { return setenv(n,v,o); }
W char* cheng_host_getcwd(char* b, size_t sz) { return getcwd(b,sz); }
W int cheng_host_chdir_runtime(const char* p) { return chdir(p); }
W long cheng_host_time(long* t) { return time(t); }
W int cheng_host_clock_gettime(int clk, void* tp) { return clock_gettime(clk,(struct timespec*)tp); }
W int cheng_host_usleep(unsigned int us) { usleep(us); return 0; }
W long cheng_host_sysconf(int n) { return sysconf(n); }
W int cheng_host_getentropy(void* b, size_t len) { return getentropy(b,len); }
W char* cheng_host_strerror(int n) { return strerror(n); }
W int cheng_host_posix_openpt_runtime(int fl) { return posix_openpt(fl); }
W int cheng_host_grantpt_runtime(int fd) { return grantpt(fd); }
W int cheng_host_unlockpt_runtime(int fd) { return unlockpt(fd); }
W char* cheng_host_ptsname_runtime(int fd) { return ptsname(fd); }
W int cheng_native_system_cpu_logical_cores_value_bridge(void) { return 4; }
W int cheng_native_errno_code_bridge(void) { return errno; }
W int cheng_native_af_inet_bridge(void) { return 2; }
W int cheng_native_af_inet6_bridge(void) { return 30; }
W int cheng_native_sock_stream_bridge(void) { return 1; }
W int cheng_native_sock_dgram_bridge(void) { return 2; }
W int cheng_native_ipproto_ip_bridge(void) { return 0; }
W int cheng_native_sol_socket_bridge(void) { return 0xffff; }
W int cheng_native_so_reuseaddr_bridge(void) { return 4; }
W int cheng_native_so_broadcast_bridge(void) { return 6; }
W int cheng_native_msg_waitall_bridge(void) { return 0x100; }
W int cheng_native_sockaddr_use_len_field_bridge(void) { return 1; }
W void cheng_host_puts_runtime(const char* s) { puts(s); }
W void cheng_program_support_host_trace(void) {}
W void cheng_host_bytes_copy(void* d, const void* s, size_t n) { memcpy(d,s,n); }
W void* cheng_host_ptr_plus(void* p, size_t off) { return (char*)p + off; }
/* Runtime entry points needed by every Cheng program */
W void __cheng_setCmdLine(int argc, const char** argv) {}
/* cheng_program_argv_entry: provided by primary .o (strong), weak fallback here */
W int cheng_program_argv_entry(int argc, const char** argv) { (void)argc; (void)argv; return 0; }
/* CRT entry: calls cheng_program_argv_entry (from primary .o if available) */
int main(int argc, const char** argv) { return cheng_program_argv_entry(argc, argv); }
W void cheng_debug_profile_flush_from_argv0(void) {}
W void cheng_native_register_line_map_from_argv0(void) {}
W void cheng_panic_cstring_and_exit(const char* msg) { exit(1); }
/* Additional commonly-needed symbols */
W int cheng_cstrlen(const char* s) { return (int)strlen(s); }
W void cheng_strutils_join_bridge(void) {}
W void cheng_str_to_cstring_temp_bridge(void) {}
W void driver_c_str_from_utf8_copy_bridge(void) {}
W void driver_c_get_env_bridge(void) {}
W void* cheng_spawn(void* fn, void* arg) { return 0; }
W int cheng_thread_parallelism(void) { return 1; }
W void cheng_mm_diag_reset(void) {}
W void cheng_mem_retain(void* p) {}
W void cheng_mem_release(void* p) {}
W int paramCount(void) { return 0; }
W const char* paramStr(int i) { return ""; }
W void* load_ptr(void** p) { return p ? *p : 0; }
W void store_ptr(void** p, void* v) { if(p) *p = v; }
W void* ptr_add(void* p, int off) { return (char*)p + off; }
W void copyMem(void* d, const void* s, unsigned long n) { memcpy(d,s,n); }
W void setMem(void* d, int v, unsigned long n) { memset(d,v,n); }
W int cheng_os_is_absolute_bridge(const char* p) { return *p == '/'; }
W char* cheng_os_join_path_bridge(const char* a, const char* b) { static char buf[4096]; snprintf(buf,sizeof(buf),"%s/%s",a,b); return buf; }
W int cheng_os_file_exists_bridge(const char* p) { return access(p,F_OK)==0; }
W int cheng_os_dir_exists_bridge(const char* p) { struct stat st; return stat(p,&st)==0 && S_ISDIR(st.st_mode); }
W long cheng_os_file_size_bridge(const char* p) { struct stat st; return stat(p,&st)==0 ? st.st_size : 0; }
W int driver_c_create_dir_all_bridge(const char* p) { return mkdir(p,0755); }
W int driver_c_write_text_file_bridge(const char* p, const char* c) { FILE* f=fopen(p,"w"); if(!f)return 0; fputs(c,f); fclose(f); return 1; }
W char* cheng_read_file_bridge(const char* p) { static char buf[65536]; FILE* f=fopen(p,"r"); if(!f)return 0; size_t n=fread(buf,1,sizeof(buf)-1,f); fclose(f); buf[n]=0; return buf; }
/* Atomic bridge functions for std/atomic provider.
   These use GCC __atomic builtins which generate proper ARM64 atomic instructions
   (ldar/stlr/ldaxr/stlxr) matching the cold compiler's inline codegen. */
W int cheng_atomic_cas_i32(void* p, int expect, int desired) {
    return __sync_bool_compare_and_swap((volatile int*)p, expect, desired);
}
W void cheng_atomic_store_i32(void* p, int val) {
    __atomic_store_n((int*)p, val, __ATOMIC_SEQ_CST);
}
W int cheng_atomic_load_i32(void* p) {
    return __atomic_load_n((int*)p, __ATOMIC_SEQ_CST);
}
