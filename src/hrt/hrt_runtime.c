#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(CHENG_HRT_SYSIO) || defined(CHENG_HRT_SHM)
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#endif
#if defined(CHENG_HRT_SYSIO)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#if defined(CHENG_HRT_SHM)
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

#include "hrt_runtime.h"

static int hrt_is_pow2(int32_t cap) {
    if (cap <= 0) {
        return 0;
    }
    return (cap & (cap - 1)) == 0;
}

void cheng_mem_retain(void *p) { (void)p; }
void cheng_mem_release(void *p) { (void)p; }
void cheng_mem_retain_atomic(void *p) { (void)p; }
void cheng_mem_release_atomic(void *p) { (void)p; }
int32_t cheng_mem_refcount(void *p) {
    (void)p;
    return 1;
}
int32_t cheng_mem_refcount_atomic(void *p) {
    (void)p;
    return 1;
}
int64_t cheng_mm_retain_count(void) { return 0; }
int64_t cheng_mm_release_count(void) { return 0; }
void cheng_mm_diag_reset(void) {}

int32_t c_strlen(const char *s) {
    if (!s) {
        return 0;
    }
    return (int32_t)strlen(s);
}

const char *__cheng_concat_str(const char *a, const char *b) {
    if (a && a[0] != '\0') {
        return a;
    }
    return b ? b : "";
}

int32_t __cheng_vec_contains_int32(cheng_seq seq, int32_t val) {
    int32_t *data = (int32_t *)seq.buffer;
    for (int32_t i = 0; i < seq.len; i++) {
        if (data[i] == val) {
            return 1;
        }
    }
    return 0;
}

int32_t __cheng_vec_contains_int64(cheng_seq seq, int64_t val) {
    int64_t *data = (int64_t *)seq.buffer;
    for (int32_t i = 0; i < seq.len; i++) {
        if (data[i] == val) {
            return 1;
        }
    }
    return 0;
}

int32_t __cheng_vec_contains_ptr_void(cheng_seq seq, void *val) {
    void **data = (void **)seq.buffer;
    for (int32_t i = 0; i < seq.len; i++) {
        if (data[i] == val) {
            return 1;
        }
    }
    return 0;
}

int32_t __cheng_vec_contains_str(cheng_seq seq, const char *val) {
    const char **data = (const char **)seq.buffer;
    for (int32_t i = 0; i < seq.len; i++) {
        const char *cur = data[i];
        if (cur == val) {
            return 1;
        }
        if (cur && val && strcmp(cur, val) == 0) {
            return 1;
        }
    }
    return 0;
}

void echo(const char *s) {
    (void)s;
}

void panic(const char *s) {
    (void)s;
    abort();
}

cheng_hrt_net_slot *cheng_hrt_net_slot_ptr(void *slots, int32_t idx) {
    if (!slots) {
        return NULL;
    }
    return &((cheng_hrt_net_slot *)slots)[idx];
}

cheng_hrt_file_req *cheng_hrt_file_req_ptr(void *slots, int32_t idx) {
    if (!slots) {
        return NULL;
    }
    return &((cheng_hrt_file_req *)slots)[idx];
}

cheng_hrt_file_cpl *cheng_hrt_file_cpl_ptr(void *slots, int32_t idx) {
    if (!slots) {
        return NULL;
    }
    return &((cheng_hrt_file_cpl *)slots)[idx];
}

#if defined(CHENG_HRT_SYSIO)
#define HRT_NETDEV_RX_BUF_SIZE 2048

typedef struct {
    int fd;
    cheng_hrt_ring_meta rx_meta;
    cheng_hrt_ring_meta tx_meta;
    cheng_hrt_net_slot *rx_slots;
    cheng_hrt_net_slot *tx_slots;
    uint8_t *rx_buffers;
    int32_t rx_cap;
    int32_t tx_cap;
} cheng_hrt_netdev_sys;

typedef struct {
    int fd;
    cheng_hrt_ring_meta submit_meta;
    cheng_hrt_ring_meta complete_meta;
    cheng_hrt_file_req *submit_slots;
    cheng_hrt_file_cpl *complete_slots;
    int32_t submit_cap;
    int32_t complete_cap;
} cheng_hrt_filedev_sys;

static int hrt_netdev_set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return -1;
    }
    return 0;
}

static int hrt_netdev_parse_udp_port(const char *ifname, int *out_port) {
    if (!ifname || !out_port) {
        return 0;
    }
    const char *prefix = NULL;
    if (strncmp(ifname, "udp://", 6) == 0) {
        prefix = ifname + 6;
    } else if (strncmp(ifname, "udp:", 4) == 0) {
        prefix = ifname + 4;
    } else {
        return 0;
    }
    const char *port_str = prefix;
    const char *colon = strrchr(prefix, ':');
    if (colon) {
        port_str = colon + 1;
    }
    int port = 0;
    if (port_str && port_str[0] != '\0') {
        port = atoi(port_str);
    }
    if (port < 0) {
        port = 0;
    }
    *out_port = port;
    return 1;
}

void *cheng_hrt_netdev_open(const char *ifname, int32_t queue, int32_t rx_cap, int32_t tx_cap) {
    (void)queue;
    if (!hrt_is_pow2(rx_cap) || !hrt_is_pow2(tx_cap)) {
        return NULL;
    }
    int port = 0;
    if (!hrt_netdev_parse_udp_port(ifname, &port)) {
        return NULL;
    }
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return NULL;
    }
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    if (hrt_netdev_set_nonblock(fd) != 0) {
        close(fd);
        return NULL;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return NULL;
    }
    if (port == 0) {
        socklen_t len = (socklen_t)sizeof(addr);
        if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
            close(fd);
            return NULL;
        }
        port = (int)ntohs(addr.sin_port);
        addr.sin_port = htons((uint16_t)port);
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return NULL;
    }
    cheng_hrt_netdev_sys *dev = (cheng_hrt_netdev_sys *)calloc(1, sizeof(*dev));
    if (!dev) {
        close(fd);
        return NULL;
    }
    dev->fd = fd;
    dev->rx_cap = rx_cap;
    dev->tx_cap = tx_cap;
    dev->rx_slots = (cheng_hrt_net_slot *)calloc((size_t)rx_cap, sizeof(cheng_hrt_net_slot));
    dev->tx_slots = (cheng_hrt_net_slot *)calloc((size_t)tx_cap, sizeof(cheng_hrt_net_slot));
    dev->rx_buffers = (uint8_t *)calloc((size_t)rx_cap, HRT_NETDEV_RX_BUF_SIZE);
    if (!dev->rx_slots || !dev->tx_slots || !dev->rx_buffers) {
        free(dev->rx_slots);
        free(dev->tx_slots);
        free(dev->rx_buffers);
        close(fd);
        free(dev);
        return NULL;
    }
    dev->rx_meta.head = 0;
    dev->rx_meta.tail = 0;
    dev->rx_meta.cap = rx_cap;
    dev->rx_meta.mask = rx_cap - 1;
    dev->tx_meta.head = 0;
    dev->tx_meta.tail = 0;
    dev->tx_meta.cap = tx_cap;
    dev->tx_meta.mask = tx_cap - 1;
    for (int32_t i = 0; i < rx_cap; i++) {
        dev->rx_slots[i].data = dev->rx_buffers + ((size_t)i * HRT_NETDEV_RX_BUF_SIZE);
        dev->rx_slots[i].len = 0;
        dev->rx_slots[i].flags = 0;
        dev->rx_slots[i].reserved = 0;
    }
    return dev;
}

int32_t cheng_hrt_netdev_map(void *handle, cheng_hrt_netdev_map_t *out) {
    if (!handle || !out) {
        return 0;
    }
    cheng_hrt_netdev_sys *dev = (cheng_hrt_netdev_sys *)handle;
    out->rx_meta = &dev->rx_meta;
    out->rx_slots = dev->rx_slots;
    out->tx_meta = &dev->tx_meta;
    out->tx_slots = dev->tx_slots;
    return 1;
}

int32_t cheng_hrt_netdev_poll_rx(void *handle) {
    if (!handle) {
        return 0;
    }
    cheng_hrt_netdev_sys *dev = (cheng_hrt_netdev_sys *)handle;
    int32_t processed = 0;
    while (1) {
        int32_t next = (dev->rx_meta.tail + 1) & dev->rx_meta.mask;
        if (next == dev->rx_meta.head) {
            break;
        }
        cheng_hrt_net_slot *slot = &dev->rx_slots[dev->rx_meta.tail & dev->rx_meta.mask];
        ssize_t rc = recv(dev->fd, slot->data, HRT_NETDEV_RX_BUF_SIZE, 0);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            break;
        }
        slot->len = (int32_t)rc;
        slot->flags = 0;
        slot->reserved = 0;
        dev->rx_meta.tail = next;
        processed++;
    }
    return processed;
}

int32_t cheng_hrt_netdev_kick_tx(void *handle) {
    if (!handle) {
        return 0;
    }
    cheng_hrt_netdev_sys *dev = (cheng_hrt_netdev_sys *)handle;
    int32_t processed = 0;
    while (dev->tx_meta.head != dev->tx_meta.tail) {
        cheng_hrt_net_slot *slot = &dev->tx_slots[dev->tx_meta.head & dev->tx_meta.mask];
        if (slot->data && slot->len > 0) {
            ssize_t rc = send(dev->fd, slot->data, (size_t)slot->len, 0);
            if (rc < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
            }
        }
        dev->tx_meta.head = (dev->tx_meta.head + 1) & dev->tx_meta.mask;
        processed++;
    }
    return processed;
}

int32_t cheng_hrt_netdev_stub_rx_push(void *handle, int32_t len, int32_t flags) {
    if (!handle) {
        return 0;
    }
    cheng_hrt_netdev_sys *dev = (cheng_hrt_netdev_sys *)handle;
    int32_t next = (dev->rx_meta.tail + 1) & dev->rx_meta.mask;
    if (next == dev->rx_meta.head) {
        return 0;
    }
    cheng_hrt_net_slot *slot = &dev->rx_slots[dev->rx_meta.tail & dev->rx_meta.mask];
    slot->len = len;
    slot->flags = flags;
    slot->reserved = 0;
    dev->rx_meta.tail = next;
    return 1;
}

void cheng_hrt_netdev_close(void *handle) {
    if (!handle) {
        return;
    }
    cheng_hrt_netdev_sys *dev = (cheng_hrt_netdev_sys *)handle;
    if (dev->fd >= 0) {
        close(dev->fd);
    }
    free(dev->rx_slots);
    free(dev->tx_slots);
    free(dev->rx_buffers);
    free(dev);
}

void *cheng_hrt_filedev_open(const char *path, int32_t submit_cap, int32_t complete_cap) {
    if (!path) {
        return NULL;
    }
    if (!hrt_is_pow2(submit_cap) || !hrt_is_pow2(complete_cap)) {
        return NULL;
    }
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        return NULL;
    }
    cheng_hrt_filedev_sys *dev = (cheng_hrt_filedev_sys *)calloc(1, sizeof(*dev));
    if (!dev) {
        close(fd);
        return NULL;
    }
    dev->fd = fd;
    dev->submit_cap = submit_cap;
    dev->complete_cap = complete_cap;
    dev->submit_slots = (cheng_hrt_file_req *)calloc((size_t)submit_cap, sizeof(cheng_hrt_file_req));
    dev->complete_slots = (cheng_hrt_file_cpl *)calloc((size_t)complete_cap, sizeof(cheng_hrt_file_cpl));
    if (!dev->submit_slots || !dev->complete_slots) {
        free(dev->submit_slots);
        free(dev->complete_slots);
        close(fd);
        free(dev);
        return NULL;
    }
    dev->submit_meta.head = 0;
    dev->submit_meta.tail = 0;
    dev->submit_meta.cap = submit_cap;
    dev->submit_meta.mask = submit_cap - 1;
    dev->complete_meta.head = 0;
    dev->complete_meta.tail = 0;
    dev->complete_meta.cap = complete_cap;
    dev->complete_meta.mask = complete_cap - 1;
    return dev;
}

int32_t cheng_hrt_filedev_map(void *handle, cheng_hrt_filedev_map_t *out) {
    if (!handle || !out) {
        return 0;
    }
    cheng_hrt_filedev_sys *dev = (cheng_hrt_filedev_sys *)handle;
    out->submit_meta = &dev->submit_meta;
    out->submit_slots = dev->submit_slots;
    out->complete_meta = &dev->complete_meta;
    out->complete_slots = dev->complete_slots;
    return 1;
}

int32_t cheng_hrt_filedev_poll(void *handle) {
    if (!handle) {
        return 0;
    }
    cheng_hrt_filedev_sys *dev = (cheng_hrt_filedev_sys *)handle;
    int32_t processed = 0;
    while (dev->submit_meta.head != dev->submit_meta.tail) {
        int32_t next_cpl = (dev->complete_meta.tail + 1) & dev->complete_meta.mask;
        if (next_cpl == dev->complete_meta.head) {
            break;
        }
        cheng_hrt_file_req *req = &dev->submit_slots[dev->submit_meta.head & dev->submit_meta.mask];
        cheng_hrt_file_cpl *cpl = &dev->complete_slots[dev->complete_meta.tail & dev->complete_meta.mask];
        ssize_t rc = -1;
        int err = 0;
        if (!req->data || req->len < 0) {
            err = EINVAL;
        } else if (req->op == 0) {
            rc = pread(dev->fd, req->data, (size_t)req->len, (off_t)req->offset);
            if (rc < 0) {
                err = errno;
            }
        } else if (req->op == 1) {
            rc = pwrite(dev->fd, req->data, (size_t)req->len, (off_t)req->offset);
            if (rc < 0) {
                err = errno;
            }
        } else {
            err = EINVAL;
        }
        cpl->op = req->op;
        cpl->file_id = req->file_id;
        cpl->offset = req->offset;
        cpl->data = req->data;
        if (rc >= 0) {
            cpl->len = (int32_t)rc;
            cpl->status = (int32_t)rc;
        } else {
            cpl->len = 0;
            cpl->status = -(int32_t)(err ? err : EINVAL);
        }
        dev->submit_meta.head = (dev->submit_meta.head + 1) & dev->submit_meta.mask;
        dev->complete_meta.tail = next_cpl;
        processed++;
    }
    return processed;
}

void cheng_hrt_filedev_close(void *handle) {
    if (!handle) {
        return;
    }
    cheng_hrt_filedev_sys *dev = (cheng_hrt_filedev_sys *)handle;
    if (dev->fd >= 0) {
        close(dev->fd);
    }
    free(dev->submit_slots);
    free(dev->complete_slots);
    free(dev);
}
#elif defined(CHENG_HRT_SHM)
#define HRT_NETDEV_RING_BUF_SIZE 2048

typedef struct {
    int fd;
    void *base;
    size_t size;
    cheng_hrt_ring_meta *rx_meta;
    cheng_hrt_ring_meta *tx_meta;
    cheng_hrt_net_slot *rx_slots;
    cheng_hrt_net_slot *tx_slots;
    uint8_t *rx_buffers;
    uint8_t *tx_buffers;
    int32_t rx_cap;
    int32_t tx_cap;
} cheng_hrt_netdev_shm;

static int hrt_netdev_parse_shm_path(const char *ifname,
                                     int32_t queue,
                                     char *out,
                                     size_t out_len) {
    if (!ifname || !out || out_len == 0) {
        return 0;
    }
    const char *name = NULL;
    if (strncmp(ifname, "ring:", 5) == 0) {
        name = ifname + 5;
    } else if (strncmp(ifname, "shm:", 4) == 0) {
        name = ifname + 4;
    } else {
        return 0;
    }
    if (!name || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '/') {
        snprintf(out, out_len, "%s", name);
        return 1;
    }
    const char *base = getenv("CHENG_HRT_RING_DIR");
    if (!base || base[0] == '\0') {
#ifdef __ANDROID__
        base = "/data/local/tmp";
#else
        base = "/tmp";
#endif
    }
    snprintf(out, out_len, "%s/%s_q%d.hrt", base, name, queue);
    return 1;
}

static size_t hrt_netdev_ring_size(int32_t rx_cap, int32_t tx_cap) {
    size_t header = sizeof(cheng_hrt_ring_meta) * 2u;
    size_t rx_slots = (size_t)rx_cap * sizeof(cheng_hrt_net_slot);
    size_t tx_slots = (size_t)tx_cap * sizeof(cheng_hrt_net_slot);
    size_t rx_buf = (size_t)rx_cap * (size_t)HRT_NETDEV_RING_BUF_SIZE;
    size_t tx_buf = (size_t)tx_cap * (size_t)HRT_NETDEV_RING_BUF_SIZE;
    return header + rx_slots + tx_slots + rx_buf + tx_buf;
}

static void hrt_netdev_ring_bind_slots(cheng_hrt_netdev_shm *dev, int init) {
    for (int32_t i = 0; i < dev->rx_cap; i++) {
        cheng_hrt_net_slot *slot = &dev->rx_slots[i];
        slot->data = dev->rx_buffers + ((size_t)i * HRT_NETDEV_RING_BUF_SIZE);
        if (init) {
            slot->len = 0;
            slot->flags = 0;
            slot->reserved = 0;
        }
    }
    for (int32_t i = 0; i < dev->tx_cap; i++) {
        cheng_hrt_net_slot *slot = &dev->tx_slots[i];
        slot->data = dev->tx_buffers + ((size_t)i * HRT_NETDEV_RING_BUF_SIZE);
        if (init) {
            slot->len = 0;
            slot->flags = 0;
            slot->reserved = 0;
        }
    }
}

void *cheng_hrt_netdev_open(const char *ifname, int32_t queue, int32_t rx_cap, int32_t tx_cap) {
    if (!hrt_is_pow2(rx_cap) || !hrt_is_pow2(tx_cap)) {
        return NULL;
    }
    char path[256];
    if (!hrt_netdev_parse_shm_path(ifname, queue, path, sizeof(path))) {
        return NULL;
    }
    int fd = open(path, O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        return NULL;
    }
    size_t size = hrt_netdev_ring_size(rx_cap, tx_cap);
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return NULL;
    }
    int init = 0;
    if (st.st_size == 0) {
        if (ftruncate(fd, (off_t)size) != 0) {
            close(fd);
            return NULL;
        }
        init = 1;
    } else if ((size_t)st.st_size < size) {
        close(fd);
        return NULL;
    }
    void *base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        close(fd);
        return NULL;
    }
    cheng_hrt_ring_meta *rx_meta = (cheng_hrt_ring_meta *)base;
    cheng_hrt_ring_meta *tx_meta = rx_meta + 1;
    if (!init) {
        if (rx_meta->cap != rx_cap || tx_meta->cap != tx_cap) {
            munmap(base, size);
            close(fd);
            return NULL;
        }
    }
    uint8_t *cursor = (uint8_t *)(tx_meta + 1);
    cheng_hrt_net_slot *rx_slots = (cheng_hrt_net_slot *)cursor;
    cursor += (size_t)rx_cap * sizeof(cheng_hrt_net_slot);
    cheng_hrt_net_slot *tx_slots = (cheng_hrt_net_slot *)cursor;
    cursor += (size_t)tx_cap * sizeof(cheng_hrt_net_slot);
    uint8_t *rx_buffers = cursor;
    cursor += (size_t)rx_cap * (size_t)HRT_NETDEV_RING_BUF_SIZE;
    uint8_t *tx_buffers = cursor;
    if (init) {
        rx_meta->head = 0;
        rx_meta->tail = 0;
        rx_meta->cap = rx_cap;
        rx_meta->mask = rx_cap - 1;
        tx_meta->head = 0;
        tx_meta->tail = 0;
        tx_meta->cap = tx_cap;
        tx_meta->mask = tx_cap - 1;
    }
    cheng_hrt_netdev_shm *dev = (cheng_hrt_netdev_shm *)calloc(1, sizeof(*dev));
    if (!dev) {
        munmap(base, size);
        close(fd);
        return NULL;
    }
    dev->fd = fd;
    dev->base = base;
    dev->size = size;
    dev->rx_meta = rx_meta;
    dev->tx_meta = tx_meta;
    dev->rx_slots = rx_slots;
    dev->tx_slots = tx_slots;
    dev->rx_buffers = rx_buffers;
    dev->tx_buffers = tx_buffers;
    dev->rx_cap = rx_cap;
    dev->tx_cap = tx_cap;
    hrt_netdev_ring_bind_slots(dev, init);
    return dev;
}

int32_t cheng_hrt_netdev_map(void *handle, cheng_hrt_netdev_map_t *out) {
    if (!handle || !out) {
        return 0;
    }
    cheng_hrt_netdev_shm *dev = (cheng_hrt_netdev_shm *)handle;
    out->rx_meta = dev->rx_meta;
    out->rx_slots = dev->rx_slots;
    out->tx_meta = dev->tx_meta;
    out->tx_slots = dev->tx_slots;
    return 1;
}

int32_t cheng_hrt_netdev_poll_rx(void *handle) {
    if (!handle) {
        return 0;
    }
    return 1;
}

int32_t cheng_hrt_netdev_kick_tx(void *handle) {
    if (!handle) {
        return 0;
    }
    return 1;
}

int32_t cheng_hrt_netdev_stub_rx_push(void *handle, int32_t len, int32_t flags) {
    if (!handle) {
        return 0;
    }
    cheng_hrt_netdev_shm *dev = (cheng_hrt_netdev_shm *)handle;
    int32_t next = (dev->rx_meta->tail + 1) & dev->rx_meta->mask;
    if (next == dev->rx_meta->head) {
        return 0;
    }
    cheng_hrt_net_slot *slot = &dev->rx_slots[dev->rx_meta->tail & dev->rx_meta->mask];
    slot->len = len;
    slot->flags = flags;
    slot->reserved = 0;
    dev->rx_meta->tail = next;
    return 1;
}

void cheng_hrt_netdev_close(void *handle) {
    if (!handle) {
        return;
    }
    cheng_hrt_netdev_shm *dev = (cheng_hrt_netdev_shm *)handle;
    if (dev->base && dev->size > 0) {
        munmap(dev->base, dev->size);
    }
    if (dev->fd >= 0) {
        close(dev->fd);
    }
    free(dev);
}

void *cheng_hrt_filedev_open(const char *path, int32_t submit_cap, int32_t complete_cap) {
    (void)path;
    (void)submit_cap;
    (void)complete_cap;
    return NULL;
}

int32_t cheng_hrt_filedev_map(void *handle, cheng_hrt_filedev_map_t *out) {
    (void)handle;
    (void)out;
    return 0;
}

int32_t cheng_hrt_filedev_poll(void *handle) {
    (void)handle;
    return 0;
}

void cheng_hrt_filedev_close(void *handle) {
    (void)handle;
}
#else
typedef struct {
    cheng_hrt_ring_meta rx_meta;
    cheng_hrt_ring_meta tx_meta;
    cheng_hrt_net_slot *rx_slots;
    cheng_hrt_net_slot *tx_slots;
    int32_t rx_cap;
    int32_t tx_cap;
} cheng_hrt_netdev_stub;

void *cheng_hrt_netdev_open(const char *ifname, int32_t queue, int32_t rx_cap, int32_t tx_cap) {
    (void)ifname;
    (void)queue;
    if (!hrt_is_pow2(rx_cap) || !hrt_is_pow2(tx_cap)) {
        return NULL;
    }
    cheng_hrt_netdev_stub *stub = (cheng_hrt_netdev_stub *)calloc(1, sizeof(*stub));
    if (!stub) {
        return NULL;
    }
    stub->rx_slots = (cheng_hrt_net_slot *)calloc((size_t)rx_cap, sizeof(cheng_hrt_net_slot));
    stub->tx_slots = (cheng_hrt_net_slot *)calloc((size_t)tx_cap, sizeof(cheng_hrt_net_slot));
    if (!stub->rx_slots || !stub->tx_slots) {
        free(stub->rx_slots);
        free(stub->tx_slots);
        free(stub);
        return NULL;
    }
    stub->rx_cap = rx_cap;
    stub->tx_cap = tx_cap;
    stub->rx_meta.head = 0;
    stub->rx_meta.tail = 0;
    stub->rx_meta.cap = rx_cap;
    stub->rx_meta.mask = rx_cap - 1;
    stub->tx_meta.head = 0;
    stub->tx_meta.tail = 0;
    stub->tx_meta.cap = tx_cap;
    stub->tx_meta.mask = tx_cap - 1;
    return stub;
}

int32_t cheng_hrt_netdev_map(void *handle, cheng_hrt_netdev_map_t *out) {
    if (!handle || !out) {
        return 0;
    }
    cheng_hrt_netdev_stub *stub = (cheng_hrt_netdev_stub *)handle;
    out->rx_meta = &stub->rx_meta;
    out->rx_slots = stub->rx_slots;
    out->tx_meta = &stub->tx_meta;
    out->tx_slots = stub->tx_slots;
    return 1;
}

int32_t cheng_hrt_netdev_poll_rx(void *handle) {
    if (!handle) {
        return 0;
    }
    return 1;
}

int32_t cheng_hrt_netdev_kick_tx(void *handle) {
    if (!handle) {
        return 0;
    }
    cheng_hrt_netdev_stub *stub = (cheng_hrt_netdev_stub *)handle;
    stub->tx_meta.head = stub->tx_meta.tail;
    return 1;
}

int32_t cheng_hrt_netdev_stub_rx_push(void *handle, int32_t len, int32_t flags) {
    if (!handle) {
        return 0;
    }
    cheng_hrt_netdev_stub *stub = (cheng_hrt_netdev_stub *)handle;
    int32_t next = (stub->rx_meta.tail + 1) & stub->rx_meta.mask;
    if (next == stub->rx_meta.head) {
        return 0;
    }
    cheng_hrt_net_slot *slot = &stub->rx_slots[stub->rx_meta.tail & stub->rx_meta.mask];
    slot->data = NULL;
    slot->len = len;
    slot->flags = flags;
    slot->reserved = 0;
    stub->rx_meta.tail = next;
    return 1;
}

void cheng_hrt_netdev_close(void *handle) {
    if (!handle) {
        return;
    }
    cheng_hrt_netdev_stub *stub = (cheng_hrt_netdev_stub *)handle;
    free(stub->rx_slots);
    free(stub->tx_slots);
    free(stub);
}

void *cheng_hrt_filedev_open(const char *path, int32_t submit_cap, int32_t complete_cap) {
    (void)path;
    (void)submit_cap;
    (void)complete_cap;
    return NULL;
}

int32_t cheng_hrt_filedev_map(void *handle, cheng_hrt_filedev_map_t *out) {
    (void)handle;
    (void)out;
    return 0;
}

int32_t cheng_hrt_filedev_poll(void *handle) {
    (void)handle;
    return 0;
}

void cheng_hrt_filedev_close(void *handle) {
    (void)handle;
}
#endif

static cheng_hrt_rtos_ops hrt_rtos_ops;
static int hrt_rtos_bound = 0;

static void hrt_rtos_clear(void) {
    memset(&hrt_rtos_ops, 0, sizeof(hrt_rtos_ops));
    hrt_rtos_bound = 0;
}

int32_t cheng_hrt_rtos_bind(const cheng_hrt_rtos_ops *ops) {
    if (!ops) {
        hrt_rtos_clear();
        return 0;
    }
    hrt_rtos_ops = *ops;
    hrt_rtos_bound = 1;
    return 0;
}

int32_t cheng_hrt_rtos_register_task(const cheng_hrt_task_desc *task) {
    if (hrt_rtos_bound && hrt_rtos_ops.register_task) {
        return hrt_rtos_ops.register_task(task);
    }
    (void)task;
    return -1;
}

int32_t cheng_hrt_rtos_start(void) {
    if (hrt_rtos_bound && hrt_rtos_ops.start) {
        return hrt_rtos_ops.start();
    }
    return -1;
}

void cheng_hrt_rtos_yield(void) {
    if (hrt_rtos_bound && hrt_rtos_ops.yield) {
        hrt_rtos_ops.yield();
    }
}

int32_t cheng_hrt_rtos_sleep_ns(int64_t ns) {
    if (hrt_rtos_bound && hrt_rtos_ops.sleep_ns) {
        return hrt_rtos_ops.sleep_ns(ns);
    }
    (void)ns;
    return -1;
}

int64_t cheng_hrt_rtos_now_ns(void) {
    if (hrt_rtos_bound && hrt_rtos_ops.now_ns) {
        return hrt_rtos_ops.now_ns();
    }
    return 0;
}

int32_t cheng_hrt_mutex_init(cheng_hrt_mutex *m) {
    if (!m) {
        return -1;
    }
    if (hrt_rtos_bound && hrt_rtos_ops.mutex_init) {
        return hrt_rtos_ops.mutex_init(m);
    }
    m->state = 0;
    return 0;
}

int32_t cheng_hrt_mutex_try_lock(cheng_hrt_mutex *m) {
    if (!m) {
        return -1;
    }
    if (hrt_rtos_bound && hrt_rtos_ops.mutex_lock) {
        return hrt_rtos_ops.mutex_lock(m, 0);
    }
    if (m->state == 0) {
        m->state = 1;
        return 0;
    }
    return -1;
}

int32_t cheng_hrt_mutex_lock(cheng_hrt_mutex *m, int64_t timeout_ns) {
    if (!m) {
        return -1;
    }
    if (hrt_rtos_bound && hrt_rtos_ops.mutex_lock) {
        return hrt_rtos_ops.mutex_lock(m, timeout_ns);
    }
    if (m->state == 0) {
        m->state = 1;
        return 0;
    }
    (void)timeout_ns;
    return -1;
}

int32_t cheng_hrt_mutex_unlock(cheng_hrt_mutex *m) {
    if (!m) {
        return -1;
    }
    if (hrt_rtos_bound && hrt_rtos_ops.mutex_unlock) {
        return hrt_rtos_ops.mutex_unlock(m);
    }
    if (m->state != 0) {
        m->state = 0;
        return 0;
    }
    return -1;
}

int32_t cheng_hrt_semaphore_init(cheng_hrt_semaphore *s, int32_t initial, int32_t max) {
    if (!s) {
        return -1;
    }
    if (hrt_rtos_bound && hrt_rtos_ops.semaphore_init) {
        return hrt_rtos_ops.semaphore_init(s, initial, max);
    }
    if (max < initial) {
        max = initial;
    }
    s->count = initial;
    s->max = max;
    return 0;
}

int32_t cheng_hrt_semaphore_take(cheng_hrt_semaphore *s, int64_t timeout_ns) {
    if (!s) {
        return -1;
    }
    if (hrt_rtos_bound && hrt_rtos_ops.semaphore_take) {
        return hrt_rtos_ops.semaphore_take(s, timeout_ns);
    }
    if (s->count > 0) {
        s->count -= 1;
        return 0;
    }
    (void)timeout_ns;
    return -1;
}

int32_t cheng_hrt_semaphore_give(cheng_hrt_semaphore *s) {
    if (!s) {
        return -1;
    }
    if (hrt_rtos_bound && hrt_rtos_ops.semaphore_give) {
        return hrt_rtos_ops.semaphore_give(s);
    }
    if (s->count < s->max) {
        s->count += 1;
        return 0;
    }
    return -1;
}

int32_t cheng_hrt_event_init(cheng_hrt_event *e) {
    if (!e) {
        return -1;
    }
    if (hrt_rtos_bound && hrt_rtos_ops.event_init) {
        return hrt_rtos_ops.event_init(e);
    }
    e->state = 0;
    return 0;
}

int32_t cheng_hrt_event_wait(cheng_hrt_event *e, int64_t timeout_ns) {
    if (!e) {
        return -1;
    }
    if (hrt_rtos_bound && hrt_rtos_ops.event_wait) {
        return hrt_rtos_ops.event_wait(e, timeout_ns);
    }
    if (e->state != 0) {
        e->state = 0;
        return 0;
    }
    (void)timeout_ns;
    return -1;
}

int32_t cheng_hrt_event_signal(cheng_hrt_event *e) {
    if (!e) {
        return -1;
    }
    if (hrt_rtos_bound && hrt_rtos_ops.event_signal) {
        return hrt_rtos_ops.event_signal(e);
    }
    e->state = 1;
    return 0;
}
