#include "cheng_mobile_host_core.h"

#include <pthread.h>
#include <string.h>

#define MOBILE_EVENT_QUEUE_CAP 256
#define MOBILE_EVENT_TEXT_CAP 512
#define MOBILE_EVENT_MSG_CAP 512

typedef struct {
  ChengMobileEvent ev;
  char text[MOBILE_EVENT_TEXT_CAP];
  char message[MOBILE_EVENT_MSG_CAP];
} ChengMobileEventSlot;

static ChengMobileEventSlot g_queue[MOBILE_EVENT_QUEUE_CAP];
static int g_head = 0;
static int g_tail = 0;
static int g_count = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static void cheng_mobile_copy_string(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t n = strnlen(src, cap - 1);
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void cheng_mobile_host_core_reset(void) {
  pthread_mutex_lock(&g_mutex);
  g_head = 0;
  g_tail = 0;
  g_count = 0;
  pthread_mutex_unlock(&g_mutex);
}

int cheng_mobile_host_core_push(const ChengMobileEvent* ev) {
  if (!ev) {
    return 0;
  }
  pthread_mutex_lock(&g_mutex);
  if (g_count > 0) {
    int lastIndex = (g_tail + MOBILE_EVENT_QUEUE_CAP - 1) % MOBILE_EVENT_QUEUE_CAP;
    ChengMobileEventSlot *last = &g_queue[lastIndex];
    if (ev->kind == MRE_FRAME_TICK && last->ev.kind == MRE_FRAME_TICK) {
      last->ev = *ev;
      pthread_mutex_unlock(&g_mutex);
      return 1;
    }
    if (ev->kind == MRE_POINTER_MOVE && last->ev.kind == MRE_POINTER_MOVE &&
        last->ev.windowId == ev->windowId && last->ev.pointerId == ev->pointerId) {
      last->ev = *ev;
      pthread_mutex_unlock(&g_mutex);
      return 1;
    }
  }
  if (g_count >= MOBILE_EVENT_QUEUE_CAP) {
    g_head = (g_head + 1) % MOBILE_EVENT_QUEUE_CAP;
    g_count -= 1;
  }
  ChengMobileEventSlot *slot = &g_queue[g_tail];
  slot->ev = *ev;
  if (ev->text) {
    cheng_mobile_copy_string(slot->text, sizeof(slot->text), ev->text);
    slot->ev.text = slot->text;
  } else {
    slot->text[0] = '\0';
    slot->ev.text = NULL;
  }
  if (ev->message) {
    cheng_mobile_copy_string(slot->message, sizeof(slot->message), ev->message);
    slot->ev.message = slot->message;
  } else {
    slot->message[0] = '\0';
    slot->ev.message = NULL;
  }
  g_tail = (g_tail + 1) % MOBILE_EVENT_QUEUE_CAP;
  g_count += 1;
  pthread_mutex_unlock(&g_mutex);
  return 1;
}

int cheng_mobile_host_core_pop(ChengMobileEvent* out) {
  pthread_mutex_lock(&g_mutex);
  if (g_count <= 0) {
    pthread_mutex_unlock(&g_mutex);
    return 0;
  }
  if (out) {
    *out = g_queue[g_head].ev;
  }
  g_head = (g_head + 1) % MOBILE_EVENT_QUEUE_CAP;
  g_count -= 1;
  pthread_mutex_unlock(&g_mutex);
  return 1;
}
