#ifndef CHENG_HRT_RUNTIME_H
#define CHENG_HRT_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void cheng_mem_retain(void *p);
void cheng_mem_release(void *p);
void cheng_mem_retain_atomic(void *p);
void cheng_mem_release_atomic(void *p);
int32_t cheng_mem_refcount(void *p);
int32_t cheng_mem_refcount_atomic(void *p);
int64_t cheng_mm_retain_count(void);
int64_t cheng_mm_release_count(void);
void cheng_mm_diag_reset(void);

int32_t c_strlen(const char *s);
const char *__cheng_concat_str(const char *a, const char *b);

typedef struct {
    int32_t len;
    int32_t cap;
    void *buffer;
} cheng_seq;

int32_t __cheng_vec_contains_int32(cheng_seq seq, int32_t val);
int32_t __cheng_vec_contains_int64(cheng_seq seq, int64_t val);
int32_t __cheng_vec_contains_ptr_void(cheng_seq seq, void *val);
int32_t __cheng_vec_contains_str(cheng_seq seq, const char *val);

void echo(const char *s);
void panic(const char *s);

typedef struct {
    int32_t head;
    int32_t tail;
    int32_t cap;
    int32_t mask;
} cheng_hrt_ring_meta;

typedef struct {
    void *data;
    int32_t len;
    int32_t flags;
    int32_t reserved;
} cheng_hrt_net_slot;

cheng_hrt_net_slot *cheng_hrt_net_slot_ptr(void *slots, int32_t idx);

typedef struct {
    cheng_hrt_ring_meta *rx_meta;
    cheng_hrt_net_slot *rx_slots;
    cheng_hrt_ring_meta *tx_meta;
    cheng_hrt_net_slot *tx_slots;
} cheng_hrt_netdev_map_t;

void *cheng_hrt_netdev_open(const char *ifname, int32_t queue, int32_t rx_cap, int32_t tx_cap);
int32_t cheng_hrt_netdev_map(void *handle, cheng_hrt_netdev_map_t *out);
int32_t cheng_hrt_netdev_poll_rx(void *handle);
int32_t cheng_hrt_netdev_kick_tx(void *handle);
int32_t cheng_hrt_netdev_stub_rx_push(void *handle, int32_t len, int32_t flags);
void cheng_hrt_netdev_close(void *handle);

typedef struct {
    int32_t op;
    int32_t file_id;
    int64_t offset;
    int32_t len;
    void *data;
    int32_t flags;
} cheng_hrt_file_req;

cheng_hrt_file_req *cheng_hrt_file_req_ptr(void *slots, int32_t idx);

typedef struct {
    int32_t op;
    int32_t file_id;
    int64_t offset;
    int32_t len;
    void *data;
    int32_t status;
} cheng_hrt_file_cpl;

cheng_hrt_file_cpl *cheng_hrt_file_cpl_ptr(void *slots, int32_t idx);

typedef struct {
    cheng_hrt_ring_meta *submit_meta;
    cheng_hrt_file_req *submit_slots;
    cheng_hrt_ring_meta *complete_meta;
    cheng_hrt_file_cpl *complete_slots;
} cheng_hrt_filedev_map_t;

void *cheng_hrt_filedev_open(const char *path, int32_t submit_cap, int32_t complete_cap);
int32_t cheng_hrt_filedev_map(void *handle, cheng_hrt_filedev_map_t *out);
int32_t cheng_hrt_filedev_poll(void *handle);
void cheng_hrt_filedev_close(void *handle);

typedef int32_t (*cheng_hrt_task_fn)(void);

typedef struct {
    const char *name;
    int64_t period_ns;
    int64_t deadline_ns;
    int32_t priority;
    cheng_hrt_task_fn entry;
} cheng_hrt_task_desc;

typedef struct {
    int32_t task_count;
    const cheng_hrt_task_desc *tasks;
} cheng_hrt_task_table;

typedef struct {
    int32_t state;
} cheng_hrt_mutex;

typedef struct {
    int32_t count;
    int32_t max;
} cheng_hrt_semaphore;

typedef struct {
    int32_t state;
} cheng_hrt_event;

typedef struct {
    int32_t (*register_task)(const cheng_hrt_task_desc *task);
    int32_t (*start)(void);
    void (*yield)(void);
    int32_t (*sleep_ns)(int64_t ns);
    int64_t (*now_ns)(void);
    int32_t (*mutex_init)(cheng_hrt_mutex *m);
    int32_t (*mutex_lock)(cheng_hrt_mutex *m, int64_t timeout_ns);
    int32_t (*mutex_unlock)(cheng_hrt_mutex *m);
    int32_t (*semaphore_init)(cheng_hrt_semaphore *s, int32_t initial, int32_t max);
    int32_t (*semaphore_take)(cheng_hrt_semaphore *s, int64_t timeout_ns);
    int32_t (*semaphore_give)(cheng_hrt_semaphore *s);
    int32_t (*event_init)(cheng_hrt_event *e);
    int32_t (*event_wait)(cheng_hrt_event *e, int64_t timeout_ns);
    int32_t (*event_signal)(cheng_hrt_event *e);
} cheng_hrt_rtos_ops;

int32_t cheng_hrt_rtos_bind(const cheng_hrt_rtos_ops *ops);
int32_t cheng_hrt_rtos_register_task(const cheng_hrt_task_desc *task);
int32_t cheng_hrt_rtos_start(void);
void cheng_hrt_rtos_yield(void);
int32_t cheng_hrt_rtos_sleep_ns(int64_t ns);
int64_t cheng_hrt_rtos_now_ns(void);

int32_t cheng_hrt_mutex_init(cheng_hrt_mutex *m);
int32_t cheng_hrt_mutex_try_lock(cheng_hrt_mutex *m);
int32_t cheng_hrt_mutex_lock(cheng_hrt_mutex *m, int64_t timeout_ns);
int32_t cheng_hrt_mutex_unlock(cheng_hrt_mutex *m);
int32_t cheng_hrt_semaphore_init(cheng_hrt_semaphore *s, int32_t initial, int32_t max);
int32_t cheng_hrt_semaphore_take(cheng_hrt_semaphore *s, int64_t timeout_ns);
int32_t cheng_hrt_semaphore_give(cheng_hrt_semaphore *s);
int32_t cheng_hrt_event_init(cheng_hrt_event *e);
int32_t cheng_hrt_event_wait(cheng_hrt_event *e, int64_t timeout_ns);
int32_t cheng_hrt_event_signal(cheng_hrt_event *e);

#ifdef __cplusplus
}
#endif

#endif
