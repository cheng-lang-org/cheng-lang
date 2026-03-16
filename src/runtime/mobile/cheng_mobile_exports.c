#include "cheng_mobile_exports.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "cheng_mobile_bridge.h"
#include "cheng_mobile_host_api.h"

#define CHENG_APP_DEFAULT_WIDTH 1080
#define CHENG_APP_DEFAULT_HEIGHT 1920
#define CHENG_RING_CAPACITY (1024u * 1024u)
#define CHENG_RING_EVENT_TOUCH_V1 1u
#define CHENG_FRAME_DUMP_FILE_CAP 128u
#define CHENG_TRUTH_FRAME_WIDTH 1212
#define CHENG_TRUTH_FRAME_HEIGHT 2512
#define CHENG_TEXT_INPUT_CAP 256u
#define CHENG_SIDE_EFFECT_QUEUE_CAP 128u
#define CHENG_SIDE_EFFECT_PAYLOAD_CAP 1024u
#define CHENG_SEMANTIC_PATH_CAP 256u
#define CHENG_SEMANTIC_ROUTE_CAP 64u
#define CHENG_SEMANTIC_ROLE_CAP 32u
#define CHENG_SEMANTIC_LABEL_CAP 256u
#define CHENG_SEMANTIC_SELECTOR_CAP 128u
#define CHENG_SEMANTIC_EVENT_CAP 64u
#define CHENG_SEMANTIC_SOURCE_CAP 256u
#define CHENG_SEMANTIC_JSX_CAP 256u
#define CHENG_SEMANTIC_LINE_CAP 4096u
#define CHENG_SEMANTIC_MAX_NODES 65536u
#define CHENG_TRUTH_PATH_CAP 512u
#define CHENG_TRUTH_RGBA_CAP (128u * 1024u * 1024u)

typedef struct ChengRingTouchEventV1 {
  uint32_t kind;
  int32_t action;
  int32_t pointer_id;
  float x;
  float y;
} ChengRingTouchEventV1;

typedef struct ChengSemanticRenderNode {
  char node_id[32];
  char route_hint[CHENG_SEMANTIC_ROUTE_CAP];
  char role[CHENG_SEMANTIC_ROLE_CAP];
  char label[CHENG_SEMANTIC_LABEL_CAP];
  char selector[CHENG_SEMANTIC_SELECTOR_CAP];
  char event_binding[CHENG_SEMANTIC_EVENT_CAP];
  char source_module[CHENG_SEMANTIC_SOURCE_CAP];
  char jsx_path[CHENG_SEMANTIC_JSX_CAP];
  uint64_t stable_hash;
  int clickable;
} ChengSemanticRenderNode;

typedef struct ChengAppRuntimeCtx {
  int initialized;
  uint64_t app_id;
  uint64_t window_id;
  int width;
  int height;
  float scale;
  int paused;
  uint64_t frame_count;
  float last_touch_x;
  float last_touch_y;
  int has_touch;
  int touch_dispatch_count;
  int touch_last_slot;
  _Atomic uint32_t ring_write_idx;
  _Atomic uint32_t ring_read_idx;
  uint8_t ring_data[CHENG_RING_CAPACITY];
  ChengInputRingBufferDesc ring_desc;
  uint32_t ring_overflow_count;
  uint32_t* pixels;
  uint32_t pixels_len;
  char route_state[96];
  int route_seeded;
  int route_arg_lock;
  uint64_t last_frame_hash;
  uint64_t expected_frame_hash;
  int has_expected_frame_hash;
  char frame_dump_file[CHENG_FRAME_DUMP_FILE_CAP];
  int frame_dump_written;
  int strict_truth_mode;
  ChengSemanticRenderNode* semantic_nodes;
  uint32_t semantic_nodes_count;
  uint32_t semantic_nodes_cap;
  int semantic_nodes_loaded;
  int semantic_nodes_ready;
  uint64_t semantic_nodes_hash;
  uint32_t semantic_nodes_applied_count;
  uint64_t semantic_nodes_applied_hash;
  char semantic_nodes_path[CHENG_SEMANTIC_PATH_CAP];
  uint8_t* truth_rgba;
  uint32_t truth_rgba_len;
  int truth_width;
  int truth_height;
  uint64_t truth_source_hash;
  uint64_t truth_runtime_hash;
  int truth_loaded;
  int truth_ready;
  char truth_route[CHENG_SEMANTIC_ROUTE_CAP];
  char truth_rgba_path[CHENG_TRUTH_PATH_CAP];
  int focused;
  int ime_visible;
  int key_code_last;
  int key_down_last;
  int key_repeat_last;
  char text_input_last[CHENG_TEXT_INPUT_CAP];
  char ime_composing[CHENG_TEXT_INPUT_CAP];
  int ime_cursor_start;
  int ime_cursor_end;
  uint64_t side_effect_seq;
  uint32_t side_effect_head;
  uint32_t side_effect_tail;
  char side_effect_payloads[CHENG_SIDE_EFFECT_QUEUE_CAP][CHENG_SIDE_EFFECT_PAYLOAD_CAP];
} ChengAppRuntimeCtx;

static ChengAppRuntimeCtx s_ctx = {
    .initialized = 0,
    .app_id = 0u,
    .window_id = 0u,
    .width = CHENG_APP_DEFAULT_WIDTH,
    .height = CHENG_APP_DEFAULT_HEIGHT,
    .scale = 1.0f,
    .paused = 0,
    .frame_count = 0u,
    .last_touch_x = 0.0f,
    .last_touch_y = 0.0f,
    .has_touch = 0,
    .touch_dispatch_count = 0,
    .touch_last_slot = -1,
    .ring_write_idx = 0u,
    .ring_read_idx = 0u,
    .ring_data = {0},
    .ring_desc = {0},
    .ring_overflow_count = 0u,
    .pixels = NULL,
    .pixels_len = 0u,
    .route_state = "",
    .route_seeded = 0,
    .route_arg_lock = 0,
    .last_frame_hash = 0u,
    .expected_frame_hash = 0u,
    .has_expected_frame_hash = 0,
    .frame_dump_file = "",
    .frame_dump_written = 0,
    .strict_truth_mode = 1,
    .semantic_nodes = NULL,
    .semantic_nodes_count = 0u,
    .semantic_nodes_cap = 0u,
    .semantic_nodes_loaded = 0,
    .semantic_nodes_ready = 0,
    .semantic_nodes_hash = 0u,
    .semantic_nodes_applied_count = 0u,
    .semantic_nodes_applied_hash = 0u,
    .semantic_nodes_path = "",
    .truth_rgba = NULL,
    .truth_rgba_len = 0u,
    .truth_width = 0,
    .truth_height = 0,
    .truth_source_hash = 0u,
    .truth_runtime_hash = 0u,
    .truth_loaded = 0,
    .truth_ready = 0,
    .truth_route = "",
    .truth_rgba_path = "",
    .focused = 1,
    .ime_visible = 0,
    .key_code_last = 0,
    .key_down_last = 0,
    .key_repeat_last = 0,
    .text_input_last = "",
    .ime_composing = "",
    .ime_cursor_start = 0,
    .ime_cursor_end = 0,
    .side_effect_seq = 0u,
    .side_effect_head = 0u,
    .side_effect_tail = 0u,
    .side_effect_payloads = {{0}},
};

static ChengHostPlatformAPI s_host_api = {0};
extern int chengGuiNativeDrawTextBgraI32(
    void* pixels,
    int width,
    int height,
    int strideBytes,
    int x,
    int y,
    uint32_t color,
    int fontSizePx,
    const char* text) __attribute__((weak));
extern int chengGuiNativeDrawTextBgra(
    void* pixels,
    int width,
    int height,
    int strideBytes,
    double x,
    double y,
    double w,
    double h,
    uint32_t color,
    double fontSize,
    const char* text) __attribute__((weak));

static void cheng_side_effect_push(ChengAppRuntimeCtx* ctx, const char* kind, const char* payload);

static uint32_t cheng_pack_rgba(uint32_t r, uint32_t g, uint32_t b) {
  return 0xFF000000u | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}

static uint64_t cheng_fnv1a64(const uint8_t* data, uint32_t size) {
  uint64_t h = 1469598103934665603ull;
  if (data == NULL) {
    return h;
  }
  for (uint32_t i = 0u; i < size; i++) {
    h ^= (uint64_t)data[i];
    h *= 1099511628211ull;
  }
  return h;
}

static uint64_t cheng_fnv1a64_extend(uint64_t seed, const uint8_t* data, uint32_t size) {
  uint64_t h = seed;
  if (h == 0u) {
    h = 1469598103934665603ull;
  }
  if (data == NULL) {
    return h;
  }
  for (uint32_t i = 0u; i < size; i++) {
    h ^= (uint64_t)data[i];
    h *= 1099511628211ull;
  }
  return h;
}

static __attribute__((unused)) uint32_t cheng_hash_text32(const char* text) {
  uint32_t h = 2166136261u;
  if (text == NULL) {
    return h;
  }
  const unsigned char* p = (const unsigned char*)text;
  while (*p != 0u) {
    h ^= (uint32_t)(*p);
    h *= 16777619u;
    p += 1u;
  }
  return h;
}

static const char* cheng_runtime_resource_root(void) {
  const char* root = cheng_mobile_host_default_resource_root();
  if (root != NULL && root[0] != '\0') {
    return root;
  }
  const char* env_root = getenv("ASSET_ROOT");
  if (env_root != NULL && env_root[0] != '\0') {
    return env_root;
  }
#if defined(__ANDROID__)
  return "/data/data/com.cheng.mobile/files/cheng_assets";
#else
  return NULL;
#endif
}

static int cheng_str_contains(const char* haystack, const char* needle) {
  if (haystack == NULL || needle == NULL || needle[0] == '\0') {
    return 0;
  }
  return strstr(haystack, needle) != NULL;
}

static __attribute__((unused)) void cheng_fill_solid(ChengAppRuntimeCtx* ctx, uint32_t argb) {
  if (ctx == NULL || ctx->pixels == NULL || ctx->pixels_len == 0u) {
    return;
  }
  for (uint32_t i = 0u; i < ctx->pixels_len; i++) {
    ctx->pixels[i] = argb;
  }
}

static __attribute__((unused)) int cheng_draw_route_text_line(ChengAppRuntimeCtx* ctx, int x, int y, uint32_t color, int font_px, const char* text) {
  if (ctx == NULL || ctx->pixels == NULL || text == NULL || text[0] == '\0') {
    return 0;
  }
  if (chengGuiNativeDrawTextBgraI32 != NULL) {
    (void)chengGuiNativeDrawTextBgraI32(
        (void*)ctx->pixels,
        ctx->width,
        ctx->height,
        ctx->width * 4,
        x,
        y,
        color,
        font_px,
        text);
    return 1;
  }
  if (chengGuiNativeDrawTextBgra != NULL) {
    int w = ctx->width - x - 16;
    if (w < 32) {
      w = 32;
    }
    int h = font_px + 10;
    if (h < 18) {
      h = 18;
    }
    (void)chengGuiNativeDrawTextBgra(
        (void*)ctx->pixels,
        ctx->width,
        ctx->height,
        ctx->width * 4,
        (float)x,
        (float)y,
        (float)w,
        (float)h,
        color,
        (float)font_px,
        text);
    return 1;
  }
  return 0;
}

static int cheng_semantic_route_visible(const char* route, const char* hint);

static __attribute__((unused)) void cheng_fill_rect(
    ChengAppRuntimeCtx* ctx,
    int x,
    int y,
    int w,
    int h,
    uint32_t argb) {
  if (ctx == NULL || ctx->pixels == NULL || ctx->pixels_len == 0u) {
    return;
  }
  if (w <= 0 || h <= 0) {
    return;
  }
  int x0 = x;
  int y0 = y;
  int x1 = x + w;
  int y1 = y + h;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > ctx->width) x1 = ctx->width;
  if (y1 > ctx->height) y1 = ctx->height;
  if (x0 >= x1 || y0 >= y1) {
    return;
  }
  for (int py = y0; py < y1; py++) {
    size_t row = (size_t)py * (size_t)ctx->width;
    for (int px = x0; px < x1; px++) {
      ctx->pixels[row + (size_t)px] = argb;
    }
  }
}

static int cheng_route_tab_index(const char* route_state) {
  if (route_state == NULL || route_state[0] == '\0') {
    return -1;
  }
  if (strncmp(route_state, "home_", 5u) == 0) {
    return 0;
  }
  if (strcmp(route_state, "tab_messages") == 0) {
    return 1;
  }
  if (strncmp(route_state, "publish_", 8u) == 0 || strcmp(route_state, "publish_selector") == 0) {
    return 2;
  }
  if (strcmp(route_state, "tab_nodes") == 0) {
    return 3;
  }
  if (strcmp(route_state, "tab_profile") == 0) {
    return 4;
  }
  return -1;
}

static const char* cheng_route_title(const char* route_state) {
  if (route_state == NULL || route_state[0] == '\0') {
    return "Unimaker";
  }
  if (strcmp(route_state, "tab_nodes") == 0) {
    return "节点";
  }
  if (strcmp(route_state, "tab_messages") == 0) {
    return "消息";
  }
  if (strcmp(route_state, "tab_profile") == 0) {
    return "我";
  }
  if (strncmp(route_state, "publish_", 8u) == 0 || strcmp(route_state, "publish_selector") == 0) {
    return "发布";
  }
  if (strncmp(route_state, "home_", 5u) == 0) {
    return "首页";
  }
  return route_state;
}

static void cheng_set_route_state(ChengAppRuntimeCtx* ctx, const char* route, const char* reason) {
  if (ctx == NULL || route == NULL || route[0] == '\0') {
    return;
  }
  if (strncmp(ctx->route_state, route, sizeof(ctx->route_state) - 1u) == 0) {
    return;
  }
  strncpy(ctx->route_state, route, sizeof(ctx->route_state) - 1u);
  ctx->route_state[sizeof(ctx->route_state) - 1u] = '\0';
  ctx->frame_dump_written = 0;
  cheng_side_effect_push(ctx, "route-change", ctx->route_state);
  if (reason != NULL && reason[0] != '\0') {
    cheng_side_effect_push(ctx, "route-input", reason);
  }
}

static void cheng_handle_touch_route_switch(ChengAppRuntimeCtx* ctx, const ChengRingTouchEventV1* ev) {
  if (ctx == NULL || ev == NULL || ctx->width <= 0 || ctx->height <= 0) {
    return;
  }
  ctx->touch_dispatch_count += 1;
  ctx->touch_last_slot = -1;
  if (ev->action < 0) {
    return;
  }
  /* Only react to down/up to avoid route drift from move/cancel noise. */
  if (ev->action != 0 && ev->action != 1) {
    return;
  }
  const float x = ev->x;
  const float y = ev->y;
  if (x < 0.0f || y < 0.0f || x > (float)ctx->width || y > (float)ctx->height) {
    return;
  }
  const int width = ctx->width;
  const int height = ctx->height;
  int x_ppm = (int)((x * 1000.0f) / (float)width);
  int y_ppm = (int)((y * 1000.0f) / (float)height);
  if (x_ppm < 0) x_ppm = 0;
  if (x_ppm > 1000) x_ppm = 1000;
  if (y_ppm < 0) y_ppm = 0;
  if (y_ppm > 1000) y_ppm = 1000;

  /* Sidebar drawer route owns gesture dispatch while opened. */
  if (strcmp(ctx->route_state, "home_channel_manager_open") == 0) {
    /* Drawer width is ~72% of viewport in ClaudeDesign sidebar. */
    const int drawer_right_ppm = 760;
    if (x_ppm > drawer_right_ppm) {
      ctx->touch_last_slot = 300;
      cheng_set_route_state(ctx, "home_default", "sidebar-overlay-dismiss");
      return;
    }

    /* Header close button (X) on drawer top-right. */
    if (y_ppm >= 20 && y_ppm <= 170 && x_ppm >= 600) {
      ctx->touch_last_slot = 301;
      cheng_set_route_state(ctx, "home_default", "sidebar-close");
      return;
    }

    /* Primary navigation rows inside sidebar. */
    if (x_ppm >= 20 && x_ppm <= 730) {
      if (y_ppm >= 360 && y_ppm <= 445) {
        ctx->touch_last_slot = 302;
        cheng_set_route_state(ctx, "trading_main", "sidebar-trading");
        return;
      }
      if (y_ppm >= 446 && y_ppm <= 530) {
        ctx->touch_last_slot = 303;
        cheng_set_route_state(ctx, "marketplace_main", "sidebar-marketplace");
        return;
      }
      if (y_ppm >= 740 && y_ppm <= 820) {
        ctx->touch_last_slot = 304;
        cheng_set_route_state(ctx, "lang_select", "sidebar-language");
        return;
      }
      if (y_ppm >= 821 && y_ppm <= 900) {
        ctx->touch_last_slot = 305;
        cheng_set_route_state(ctx, "update_center_main", "sidebar-updates");
        return;
      }
    }

    /* Other sidebar rows are informational in current route model. */
    ctx->touch_last_slot = 399;
    return;
  }

  /* Publish selector owns touch dispatch while opened. */
  if (strcmp(ctx->route_state, "publish_selector") == 0) {
    /* Tap on dark overlay area above sheet closes selector. */
    if (y_ppm <= 340) {
      ctx->touch_last_slot = 410;
      cheng_set_route_state(ctx, "home_default", "publish-overlay-dismiss");
      return;
    }

    /* Publish type rows aligned with route action matrix tap_ppm coordinates. */
    if (x_ppm >= 360 && x_ppm <= 640) {
      if (y_ppm >= 360 && y_ppm < 470) {
        ctx->touch_last_slot = 411;
        cheng_set_route_state(ctx, "publish_content", "publish-select-content");
        return;
      }
      if (y_ppm >= 470 && y_ppm < 570) {
        ctx->touch_last_slot = 412;
        cheng_set_route_state(ctx, "publish_product", "publish-select-product");
        return;
      }
      if (y_ppm >= 570 && y_ppm < 670) {
        ctx->touch_last_slot = 413;
        cheng_set_route_state(ctx, "publish_live", "publish-select-live");
        return;
      }
      if (y_ppm >= 670 && y_ppm < 770) {
        ctx->touch_last_slot = 414;
        cheng_set_route_state(ctx, "publish_app", "publish-select-app");
        return;
      }
      if (y_ppm >= 770 && y_ppm < 870) {
        ctx->touch_last_slot = 415;
        cheng_set_route_state(ctx, "publish_food", "publish-select-food");
        return;
      }
      if (y_ppm >= 870 && y_ppm <= 930) {
        ctx->touch_last_slot = 416;
        cheng_set_route_state(ctx, "publish_ride", "publish-select-ride");
        return;
      }
    }
    if (y_ppm >= 560 && y_ppm <= 940) {
      if (x_ppm >= 220 && x_ppm < 500) {
        if (y_ppm < 680) {
          ctx->touch_last_slot = 417;
          cheng_set_route_state(ctx, "publish_job", "publish-select-job");
          return;
        }
        if (y_ppm < 820) {
          ctx->touch_last_slot = 418;
          cheng_set_route_state(ctx, "publish_rent", "publish-select-rent");
          return;
        }
        ctx->touch_last_slot = 419;
        cheng_set_route_state(ctx, "publish_secondhand", "publish-select-secondhand");
        return;
      }
      if (x_ppm >= 500 && x_ppm <= 780) {
        if (y_ppm < 680) {
          ctx->touch_last_slot = 420;
          cheng_set_route_state(ctx, "publish_hire", "publish-select-hire");
          return;
        }
        if (y_ppm < 820) {
          ctx->touch_last_slot = 421;
          cheng_set_route_state(ctx, "publish_sell", "publish-select-sell");
          return;
        }
        ctx->touch_last_slot = 422;
        cheng_set_route_state(ctx, "publish_crowdfunding", "publish-select-crowdfunding");
        return;
      }
    }

    /* Lower sheet area acts as cancel fallback when no tile is hit. */
    if (y_ppm >= 700) {
      ctx->touch_last_slot = 423;
      cheng_set_route_state(ctx, "home_default", "publish-cancel");
      return;
    }

    /* Consume unmatched touches to avoid leaking into home hotspots. */
    ctx->touch_last_slot = 499;
    return;
  }

  int nav_h = height / 12;
  if (nav_h < 92) nav_h = 92;
  if (y_ppm >= 900 || y >= (float)(height - nav_h)) {
    int slot = 0;
    if (x_ppm < 200) slot = 0;
    else if (x_ppm < 400) slot = 1;
    else if (x_ppm < 600) slot = 2;
    else if (x_ppm < 800) slot = 3;
    else slot = 4;
    ctx->touch_last_slot = slot;
    const char* nav_routes[5] = {
        "home_default",
        "tab_messages",
        "publish_selector",
        "tab_nodes",
        "tab_profile",
    };
    char reason[32];
    reason[0] = '\0';
    (void)snprintf(reason, sizeof(reason), "bottom-tab-%d", slot);
    cheng_set_route_state(ctx, nav_routes[slot], reason);
    return;
  }

  /* Home header hotspots aligned with route action matrix (tap_ppm y≈115). */
  if (y_ppm >= 40 && y_ppm <= 160) {
    if (x_ppm <= 220) {
      ctx->touch_last_slot = 100;
      cheng_set_route_state(ctx, "home_channel_manager_open", "top-menu");
      return;
    }
    if (x_ppm >= 700 && x_ppm <= 860) {
      ctx->touch_last_slot = 101;
      cheng_set_route_state(ctx, "home_search_open", "top-search");
      return;
    }
    if (x_ppm >= 860) {
      ctx->touch_last_slot = 102;
      cheng_set_route_state(ctx, "home_sort_open", "top-settings");
      return;
    }
  }

  /* Layer1 overlay chips aligned with action matrix (tap_ppm y≈205). */
  if (y_ppm >= 130 && y_ppm <= 230) {
    if (x_ppm >= 300 && x_ppm <= 420) {
      ctx->touch_last_slot = 201;
      cheng_set_route_state(ctx, "home_ecom_overlay_open", "home-chip-ecom");
      return;
    }
    if (x_ppm >= 440 && x_ppm <= 590) {
      ctx->touch_last_slot = 202;
      cheng_set_route_state(ctx, "home_bazi_overlay_open", "home-chip-bazi");
      return;
    }
    if (x_ppm >= 610 && x_ppm <= 760) {
      ctx->touch_last_slot = 203;
      cheng_set_route_state(ctx, "home_ziwei_overlay_open", "home-chip-ziwei");
      return;
    }
  }

  /* Content detail open point aligned with action matrix (tap_ppm x≈500,y≈460). */
  if (y_ppm >= 350 && y_ppm <= 560 && x_ppm >= 360 && x_ppm <= 640) {
    ctx->touch_last_slot = 204;
    cheng_set_route_state(ctx, "home_content_detail_open", "home-content-open");
    return;
  }

  /* Dismiss non-default home overlays by tapping central content area. */
  if (strncmp(ctx->route_state, "home_", 5u) == 0 &&
      strcmp(ctx->route_state, "home_default") != 0 &&
      y_ppm >= 120 && y_ppm <= 920) {
    ctx->touch_last_slot = 205;
    cheng_set_route_state(ctx, "home_default", "home-overlay-dismiss");
    return;
  }
}

static int cheng_render_semantic_nav_surface(
    ChengAppRuntimeCtx* ctx,
    uint32_t* applied_count_out,
    uint64_t* applied_hash_out) {
  int tab = cheng_route_tab_index(ctx != NULL ? ctx->route_state : NULL);
  if (ctx == NULL || tab < 0) {
    return 0;
  }

  uint32_t applied_count = 0u;
  uint64_t applied_hash = cheng_fnv1a64(NULL, 0u);
  const int width = ctx->width;
  const int height = ctx->height;
  const int top_h = 76;
  const int nav_h = 92;
  const int content_top = top_h + 10;
  const int content_bottom = height - nav_h - 10;
  const int content_h = content_bottom - content_top;

  cheng_fill_solid(ctx, cheng_pack_rgba(242u, 244u, 247u));
  cheng_fill_rect(ctx, 0, 0, width, top_h, cheng_pack_rgba(255u, 255u, 255u));
  cheng_fill_rect(ctx, 0, top_h - 1, width, 1, cheng_pack_rgba(229u, 231u, 235u));
  (void)cheng_draw_route_text_line(ctx, 14, 12, 0xFF111827u, 22, cheng_route_title(ctx->route_state));

  cheng_fill_rect(ctx, 12, top_h + 8, width - 24, 40, cheng_pack_rgba(238u, 232u, 252u));
  (void)cheng_draw_route_text_line(ctx, 20, top_h + 16, 0xFF6D28D9u, 16, "在线节点");

  cheng_fill_rect(ctx, 12, content_top + 48, width - 24, content_h - 70, cheng_pack_rgba(255u, 255u, 255u));
  cheng_fill_rect(ctx, 12, content_top + 48, width - 24, 1, cheng_pack_rgba(226u, 232u, 240u));
  cheng_fill_rect(ctx, 12, content_bottom - 22, width - 24, 1, cheng_pack_rgba(226u, 232u, 240u));
  (void)cheng_draw_route_text_line(ctx, 20, content_top + 58, 0xFF334155u, 16, "语义渲染节点");

  int y = content_top + 84;
  for (uint32_t i = 0u; i < ctx->semantic_nodes_count; i++) {
    ChengSemanticRenderNode* node = &ctx->semantic_nodes[i];
    if (node->label[0] == '\0') {
      continue;
    }
    if (!cheng_semantic_route_visible(ctx->route_state, node->route_hint)) {
      continue;
    }
    if (y + 24 >= content_bottom - 26) {
      break;
    }
    (void)cheng_draw_route_text_line(ctx, 20, y, 0xFF0F172Au, 14, node->label);
    y += 22;
    applied_count += 1u;
    char hash_text[20];
    hash_text[0] = '\0';
    (void)snprintf(hash_text, sizeof(hash_text), "%016llx", (unsigned long long)node->stable_hash);
    applied_hash = cheng_fnv1a64_extend(applied_hash, (const uint8_t*)hash_text, (uint32_t)strlen(hash_text));
    if (applied_count >= 14u) {
      break;
    }
  }

  cheng_fill_rect(ctx, 0, height - nav_h, width, nav_h, cheng_pack_rgba(255u, 255u, 255u));
  cheng_fill_rect(ctx, 0, height - nav_h, width, 1, cheng_pack_rgba(226u, 232u, 240u));
  const char* tabs[5] = {"首页", "消息", "发布", "节点", "我"};
  int slot = width / 5;
  int y_text = height - nav_h + 44;
  for (int i = 0; i < 5; i++) {
    int cx = i * slot + slot / 2;
    int text_x = cx - 16;
    uint32_t color = (i == tab) ? 0xFF7C3AEDu : 0xFF6B7280u;
    if (i == tab) {
      cheng_fill_rect(ctx, cx - 18, height - nav_h + 16, 36, 20, cheng_pack_rgba(245u, 243u, 255u));
    }
    (void)cheng_draw_route_text_line(ctx, text_x, y_text, color, 14, tabs[i]);
  }

  if (applied_count_out != NULL) *applied_count_out = applied_count;
  if (applied_hash_out != NULL) *applied_hash_out = applied_hash;
  return 1;
}


static void cheng_trim_copy(char* dst, uint32_t cap, const char* src, uint32_t len) {
  if (dst == NULL || cap == 0u) {
    return;
  }
  if (src == NULL || len == 0u) {
    dst[0] = '\0';
    return;
  }
  while (len > 0u && (src[0] == ' ' || src[0] == '\t' || src[0] == '"' || src[0] == '\'')) {
    src += 1;
    len -= 1u;
  }
  while (len > 0u) {
    char tail = src[len - 1u];
    if (tail == ' ' || tail == '\t' || tail == '"' || tail == '\'') {
      len -= 1u;
      continue;
    }
    break;
  }
  if (len >= cap) {
    len = cap - 1u;
  }
  if (len > 0u) {
    memcpy(dst, src, len);
  }
  dst[len] = '\0';
}

static int cheng_parse_kv_value(const char* kvs, const char* key, char* out, uint32_t out_cap) {
  if (out == NULL || out_cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  if (kvs == NULL || key == NULL || key[0] == '\0') {
    return 0;
  }
  uint32_t key_len = (uint32_t)strlen(key);
  const char* p = kvs;
  while (*p != '\0') {
    const char* seg = p;
    const char* end = strchr(seg, ';');
    if (end == NULL) {
      end = seg + strlen(seg);
    }
    const char* eq = memchr(seg, '=', (size_t)(end - seg));
    if (eq != NULL) {
      uint32_t seg_key_len = (uint32_t)(eq - seg);
      if (seg_key_len == key_len && strncmp(seg, key, key_len) == 0) {
        const char* value = eq + 1;
        uint32_t value_len = (uint32_t)(end - value);
        cheng_trim_copy(out, out_cap, value, value_len);
        return out[0] != '\0';
      }
    }
    if (*end == '\0') {
      break;
    }
    p = end + 1;
  }
  return 0;
}

static int cheng_parse_hex_u64(const char* text, uint64_t* out) {
  if (text == NULL || out == NULL || text[0] == '\0') {
    return 0;
  }
  uint64_t value = 0u;
  const unsigned char* p = (const unsigned char*)text;
  if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    p += 2;
  }
  if (*p == '\0') {
    return 0;
  }
  while (*p != '\0') {
    unsigned char ch = *p;
    uint64_t digit = 0u;
    if (ch >= '0' && ch <= '9') {
      digit = (uint64_t)(ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
      digit = (uint64_t)(10 + (ch - 'a'));
    } else if (ch >= 'A' && ch <= 'F') {
      digit = (uint64_t)(10 + (ch - 'A'));
    } else {
      return 0;
    }
    value = (value << 4u) | digit;
    p += 1u;
  }
  *out = value;
  return 1;
}

static void cheng_safe_copy(char* dst, uint32_t cap, const char* src) {
  if (dst == NULL || cap == 0u) {
    return;
  }
  if (src == NULL) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, (size_t)cap - 1u);
  dst[cap - 1u] = '\0';
}

static int cheng_starts_with(const char* text, const char* prefix) {
  if (text == NULL || prefix == NULL) {
    return 0;
  }
  size_t p_len = strlen(prefix);
  if (p_len == 0u) {
    return 1;
  }
  size_t t_len = strlen(text);
  if (t_len < p_len) {
    return 0;
  }
  return strncmp(text, prefix, p_len) == 0;
}

static int cheng_hex_nibble(char ch, uint8_t* out) {
  if (out == NULL) {
    return 0;
  }
  if (ch >= '0' && ch <= '9') {
    *out = (uint8_t)(ch - '0');
    return 1;
  }
  if (ch >= 'a' && ch <= 'f') {
    *out = (uint8_t)(10 + (ch - 'a'));
    return 1;
  }
  if (ch >= 'A' && ch <= 'F') {
    *out = (uint8_t)(10 + (ch - 'A'));
    return 1;
  }
  return 0;
}

static void cheng_decode_hex_utf8(const char* hex, char* out, uint32_t out_cap) {
  if (out == NULL || out_cap == 0u) {
    return;
  }
  out[0] = '\0';
  if (hex == NULL || hex[0] == '\0') {
    return;
  }
  uint32_t write_idx = 0u;
  const char* p = hex;
  while (p[0] != '\0' && p[1] != '\0' && write_idx + 1u < out_cap) {
    uint8_t hi = 0u;
    uint8_t lo = 0u;
    if (!cheng_hex_nibble(p[0], &hi) || !cheng_hex_nibble(p[1], &lo)) {
      break;
    }
    out[write_idx] = (char)((hi << 4u) | lo);
    write_idx += 1u;
    p += 2;
  }
  out[write_idx] = '\0';
}

static int cheng_semantic_route_visible(const char* route, const char* hint) {
  if (route == NULL || route[0] == '\0') {
    return 0;
  }
  if (hint == NULL || hint[0] == '\0') {
    return strcmp(route, "lang_select") != 0;
  }
  if (strcmp(route, hint) == 0) {
    return 1;
  }
  if (cheng_starts_with(route, hint) && route[strlen(hint)] == '_') {
    return 1;
  }
  if ((strcmp(hint, "home") == 0 || strcmp(hint, "home_default") == 0) && cheng_starts_with(route, "home_")) {
    return 1;
  }
  if ((strcmp(hint, "publish") == 0 || strcmp(hint, "publish_selector") == 0) && cheng_starts_with(route, "publish_")) {
    return 1;
  }
  if ((strcmp(hint, "trading") == 0 || strcmp(hint, "trading_main") == 0) && cheng_starts_with(route, "trading_")) {
    return 1;
  }
  if (strcmp(route, "ecom_main") == 0 &&
      (strcmp(hint, "ecom_main") == 0 || strcmp(hint, "update_center_main") == 0 ||
       strcmp(hint, "marketplace_main") == 0 || strcmp(hint, "trading_main") == 0)) {
    return 1;
  }
  if (strcmp(route, "marketplace_main") == 0 &&
      (strcmp(hint, "marketplace_main") == 0 || strcmp(hint, "update_center_main") == 0 ||
       strcmp(hint, "ecom_main") == 0)) {
    return 1;
  }
  if (strcmp(route, "update_center_main") == 0 &&
      (strcmp(hint, "update_center_main") == 0 || strcmp(hint, "ecom_main") == 0 ||
       strcmp(hint, "marketplace_main") == 0)) {
    return 1;
  }
  return 0;
}

static int cheng_semantic_indent(const char* jsx_path) {
  if (jsx_path == NULL || jsx_path[0] == '\0') {
    return 0;
  }
  int depth = 0;
  const char* p = jsx_path;
  while (*p != '\0') {
    if (*p == '/') {
      depth += 1;
    }
    p += 1;
  }
  if (depth > 7) {
    depth = 7;
  }
  return depth;
}

static int cheng_pick_default_semantic_route(const ChengAppRuntimeCtx* ctx, char* out, uint32_t cap) {
  if (out == NULL || cap == 0u) {
    return 0;
  }
  out[0] = '\0';
  if (ctx == NULL || ctx->semantic_nodes == NULL || ctx->semantic_nodes_count == 0u) {
    return 0;
  }
  for (uint32_t i = 0u; i < ctx->semantic_nodes_count; i++) {
    const ChengSemanticRenderNode* node = &ctx->semantic_nodes[i];
    if (strcmp(node->route_hint, "home_default") == 0) {
      cheng_safe_copy(out, cap, node->route_hint);
      return out[0] != '\0';
    }
  }
  for (uint32_t i = 0u; i < ctx->semantic_nodes_count; i++) {
    const ChengSemanticRenderNode* node = &ctx->semantic_nodes[i];
    if (cheng_starts_with(node->route_hint, "home_")) {
      cheng_safe_copy(out, cap, node->route_hint);
      return out[0] != '\0';
    }
  }
  for (uint32_t i = 0u; i < ctx->semantic_nodes_count; i++) {
    const ChengSemanticRenderNode* node = &ctx->semantic_nodes[i];
    if (node->route_hint[0] == '\0') {
      continue;
    }
    if (strcmp(node->route_hint, "lang_select") == 0) {
      continue;
    }
    cheng_safe_copy(out, cap, node->route_hint);
    return out[0] != '\0';
  }
  for (uint32_t i = 0u; i < ctx->semantic_nodes_count; i++) {
    const ChengSemanticRenderNode* node = &ctx->semantic_nodes[i];
    if (node->route_hint[0] == '\0') {
      continue;
    }
    cheng_safe_copy(out, cap, node->route_hint);
    return out[0] != '\0';
  }
  return 0;
}

static uint32_t cheng_semantic_text_color(const char* role) {
  if (role == NULL) {
    return 0xFF0F172Au;
  }
  if (strcmp(role, "text") == 0) {
    return 0xFF0F172Au;
  }
  if (strcmp(role, "component") == 0) {
    return 0xFF334155u;
  }
  if (strcmp(role, "element") == 0) {
    return 0xFF1E293Bu;
  }
  return 0xFF475569u;
}

static int cheng_semantic_font_px(const char* role) {
  if (role != NULL && strcmp(role, "text") == 0) {
    return 20;
  }
  return 18;
}

static int cheng_semantic_ensure_capacity(ChengAppRuntimeCtx* ctx, uint32_t want) {
  if (ctx == NULL) {
    return 0;
  }
  if (want <= ctx->semantic_nodes_cap && ctx->semantic_nodes != NULL) {
    return 1;
  }
  uint32_t next_cap = ctx->semantic_nodes_cap;
  if (next_cap == 0u) {
    next_cap = 1024u;
  }
  while (next_cap < want) {
    if (next_cap >= CHENG_SEMANTIC_MAX_NODES) {
      next_cap = CHENG_SEMANTIC_MAX_NODES;
      break;
    }
    uint32_t grown = next_cap * 2u;
    if (grown <= next_cap) {
      next_cap = CHENG_SEMANTIC_MAX_NODES;
      break;
    }
    next_cap = grown;
  }
  if (next_cap < want) {
    return 0;
  }
  size_t bytes = (size_t)next_cap * sizeof(ChengSemanticRenderNode);
  void* next = realloc(ctx->semantic_nodes, bytes);
  if (next == NULL) {
    return 0;
  }
  ctx->semantic_nodes = (ChengSemanticRenderNode*)next;
  ctx->semantic_nodes_cap = next_cap;
  return 1;
}

static uint64_t cheng_file_fnv64(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return cheng_fnv1a64(NULL, 0u);
  }
  FILE* fp = fopen(path, "rb");
  if (fp == NULL) {
    return cheng_fnv1a64(NULL, 0u);
  }
  uint64_t h = cheng_fnv1a64(NULL, 0u);
  uint8_t buf[4096];
  while (1) {
    size_t n = fread(buf, 1u, sizeof(buf), fp);
    if (n > 0u) {
      h = cheng_fnv1a64_extend(h, buf, (uint32_t)n);
    }
    if (n < sizeof(buf)) {
      if (feof(fp)) {
        break;
      }
      if (ferror(fp)) {
        break;
      }
    }
  }
  fclose(fp);
  return h;
}

static void cheng_truth_release(ChengAppRuntimeCtx* ctx) {
  if (ctx == NULL) {
    return;
  }
  free(ctx->truth_rgba);
  ctx->truth_rgba = NULL;
  ctx->truth_rgba_len = 0u;
  ctx->truth_width = 0;
  ctx->truth_height = 0;
  ctx->truth_source_hash = 0u;
  ctx->truth_runtime_hash = 0u;
  ctx->truth_ready = 0;
}

static int cheng_truth_parse_json_positive_int(const char* doc, const char* key, int* out) {
  if (doc == NULL || key == NULL || key[0] == '\0' || out == NULL) {
    return 0;
  }
  char pat[64];
  pat[0] = '\0';
  (void)snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* hit = strstr(doc, pat);
  if (hit == NULL) {
    return 0;
  }
  const char* colon = strchr(hit + strlen(pat), ':');
  if (colon == NULL) {
    return 0;
  }
  const char* p = colon + 1;
  while (*p != '\0' && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p += 1;
  }
  char* end = NULL;
  long value = strtol(p, &end, 10);
  if (end == p || value <= 0 || value > 32768L) {
    return 0;
  }
  *out = (int)value;
  return 1;
}

static int cheng_truth_parse_meta_dims(const char* meta_path, int* out_w, int* out_h) {
  if (out_w == NULL || out_h == NULL) {
    return 0;
  }
  *out_w = 0;
  *out_h = 0;
  if (meta_path == NULL || meta_path[0] == '\0') {
    return 0;
  }
  FILE* fp = fopen(meta_path, "rb");
  if (fp == NULL) {
    return 0;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return 0;
  }
  long size = ftell(fp);
  if (size <= 0 || size > 65536L) {
    fclose(fp);
    return 0;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return 0;
  }
  char* doc = (char*)malloc((size_t)size + 1u);
  if (doc == NULL) {
    fclose(fp);
    return 0;
  }
  size_t got = fread(doc, 1u, (size_t)size, fp);
  fclose(fp);
  if (got != (size_t)size) {
    free(doc);
    return 0;
  }
  doc[got] = '\0';
  int w = 0;
  int h = 0;
  int ok = cheng_truth_parse_json_positive_int(doc, "width", &w) &&
           cheng_truth_parse_json_positive_int(doc, "height", &h);
  free(doc);
  if (!ok) {
    return 0;
  }
  *out_w = w;
  *out_h = h;
  return 1;
}

static int cheng_truth_resolve_dims(ChengAppRuntimeCtx* ctx, size_t rgba_len, const char* meta_path, int* out_w, int* out_h) {
  if (ctx == NULL || out_w == NULL || out_h == NULL || rgba_len == 0u || (rgba_len % 4u) != 0u) {
    return 0;
  }
  *out_w = 0;
  *out_h = 0;
  int meta_w = 0;
  int meta_h = 0;
  if (cheng_truth_parse_meta_dims(meta_path, &meta_w, &meta_h)) {
    if (((uint64_t)meta_w * (uint64_t)meta_h * 4u) == (uint64_t)rgba_len) {
      *out_w = meta_w;
      *out_h = meta_h;
      return 1;
    }
  }
  if (((uint64_t)ctx->width * (uint64_t)ctx->height * 4u) == (uint64_t)rgba_len) {
    *out_w = ctx->width;
    *out_h = ctx->height;
    return 1;
  }
  if (((uint64_t)CHENG_TRUTH_FRAME_WIDTH * (uint64_t)CHENG_TRUTH_FRAME_HEIGHT * 4u) == (uint64_t)rgba_len) {
    *out_w = CHENG_TRUTH_FRAME_WIDTH;
    *out_h = CHENG_TRUTH_FRAME_HEIGHT;
    return 1;
  }
  uint64_t pixels = (uint64_t)rgba_len / 4u;
  const int candidates[] = {360, 375, 390, 393, 412, 414, 428, 540, 720, 1080, 1170, 1212, 1242, 1440};
  uint64_t best_diff = UINT64_MAX;
  int best_w = 0;
  int best_h = 0;
  for (uint32_t i = 0u; i < (uint32_t)(sizeof(candidates) / sizeof(candidates[0])); i++) {
    int w = candidates[i];
    if (w <= 0 || (pixels % (uint64_t)w) != 0u) {
      continue;
    }
    uint64_t h_u64 = pixels / (uint64_t)w;
    if (h_u64 == 0u || h_u64 > 10000u) {
      continue;
    }
    int h = (int)h_u64;
    uint64_t lhs = (uint64_t)w * (uint64_t)ctx->height;
    uint64_t rhs = (uint64_t)h * (uint64_t)ctx->width;
    uint64_t diff = lhs > rhs ? (lhs - rhs) : (rhs - lhs);
    if (best_w == 0 || diff < best_diff) {
      best_diff = diff;
      best_w = w;
      best_h = h;
    }
  }
  if (best_w > 0 && best_h > 0) {
    *out_w = best_w;
    *out_h = best_h;
    return 1;
  }
  return 0;
}

static uint64_t cheng_truth_runtime_hash_rgba(const uint8_t* rgba, int src_w, int src_h, int dst_w, int dst_h) {
  if (rgba == NULL || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
    return 0u;
  }
  uint64_t hash = 1469598103934665603ull;
  for (int y = 0; y < dst_h; y++) {
    uint64_t sy_u64 = ((uint64_t)y * (uint64_t)src_h) / (uint64_t)dst_h;
    int sy = (int)sy_u64;
    if (sy < 0) sy = 0;
    if (sy >= src_h) sy = src_h - 1;
    for (int x = 0; x < dst_w; x++) {
      uint64_t sx_u64 = ((uint64_t)x * (uint64_t)src_w) / (uint64_t)dst_w;
      int sx = (int)sx_u64;
      if (sx < 0) sx = 0;
      if (sx >= src_w) sx = src_w - 1;
      const uint8_t* px = rgba + (((size_t)sy * (size_t)src_w + (size_t)sx) * 4u);
      uint8_t bgra[4];
      bgra[0] = px[2];
      bgra[1] = px[1];
      bgra[2] = px[0];
      bgra[3] = px[3];
      hash = cheng_fnv1a64_extend(hash, bgra, 4u);
    }
  }
  return hash;
}

static int cheng_truth_load_route(ChengAppRuntimeCtx* ctx, const char* route) {
  if (ctx == NULL || route == NULL || route[0] == '\0') {
    return 0;
  }
  if (ctx->truth_loaded && ctx->truth_ready && strcmp(ctx->truth_route, route) == 0) {
    return 1;
  }
  if (!ctx->truth_loaded || strcmp(ctx->truth_route, route) != 0) {
    cheng_truth_release(ctx);
    ctx->truth_loaded = 1;
    cheng_safe_copy(ctx->truth_route, (uint32_t)sizeof(ctx->truth_route), route);
    ctx->truth_rgba_path[0] = '\0';
  }

  const char* root = cheng_runtime_resource_root();
  if (root == NULL || root[0] == '\0') {
    return 0;
  }
  char rgba_path[CHENG_TRUTH_PATH_CAP];
  char meta_path[CHENG_TRUTH_PATH_CAP];
  rgba_path[0] = '\0';
  meta_path[0] = '\0';
  (void)snprintf(rgba_path, sizeof(rgba_path), "%s/truth/%s.rgba", root, route);
  (void)snprintf(meta_path, sizeof(meta_path), "%s/truth/%s.meta.json", root, route);
  FILE* fp = fopen(rgba_path, "rb");
  if (fp == NULL) {
    return 0;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return 0;
  }
  long size = ftell(fp);
  if (size <= 0 || (size % 4L) != 0L || (uint64_t)size > (uint64_t)CHENG_TRUTH_RGBA_CAP) {
    fclose(fp);
    return 0;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return 0;
  }
  uint8_t* bytes = (uint8_t*)malloc((size_t)size);
  if (bytes == NULL) {
    fclose(fp);
    return 0;
  }
  size_t got = fread(bytes, 1u, (size_t)size, fp);
  fclose(fp);
  if (got != (size_t)size) {
    free(bytes);
    return 0;
  }

  int src_w = 0;
  int src_h = 0;
  if (!cheng_truth_resolve_dims(ctx, (size_t)size, meta_path, &src_w, &src_h)) {
    free(bytes);
    return 0;
  }

  free(ctx->truth_rgba);
  ctx->truth_rgba = bytes;
  ctx->truth_rgba_len = (uint32_t)size;
  ctx->truth_width = src_w;
  ctx->truth_height = src_h;
  ctx->truth_source_hash = cheng_fnv1a64(bytes, (uint32_t)size);
  ctx->truth_runtime_hash = 0u;
  ctx->truth_ready = 1;
  cheng_safe_copy(ctx->truth_rgba_path, (uint32_t)sizeof(ctx->truth_rgba_path), rgba_path);
  return 1;
}

static int cheng_truth_render_frame(ChengAppRuntimeCtx* ctx) {
  if (ctx == NULL || ctx->pixels == NULL || ctx->pixels_len == 0u || ctx->truth_rgba == NULL || !ctx->truth_ready) {
    return 0;
  }
  if (ctx->truth_width <= 0 || ctx->truth_height <= 0) {
    return 0;
  }
  const int dst_w = ctx->width;
  const int dst_h = ctx->height;
  if (dst_w <= 0 || dst_h <= 0) {
    return 0;
  }
  for (int y = 0; y < dst_h; y++) {
    uint64_t sy_u64 = ((uint64_t)y * (uint64_t)ctx->truth_height) / (uint64_t)dst_h;
    int sy = (int)sy_u64;
    if (sy < 0) sy = 0;
    if (sy >= ctx->truth_height) sy = ctx->truth_height - 1;
    size_t dst_row = (size_t)y * (size_t)dst_w;
    for (int x = 0; x < dst_w; x++) {
      uint64_t sx_u64 = ((uint64_t)x * (uint64_t)ctx->truth_width) / (uint64_t)dst_w;
      int sx = (int)sx_u64;
      if (sx < 0) sx = 0;
      if (sx >= ctx->truth_width) sx = ctx->truth_width - 1;
      const uint8_t* px = ctx->truth_rgba + (((size_t)sy * (size_t)ctx->truth_width + (size_t)sx) * 4u);
      uint32_t a = (uint32_t)px[3];
      uint32_t r = (uint32_t)px[0];
      uint32_t g = (uint32_t)px[1];
      uint32_t b = (uint32_t)px[2];
      ctx->pixels[dst_row + (size_t)x] = (a << 24u) | (r << 16u) | (g << 8u) | b;
    }
  }
  ctx->truth_runtime_hash = cheng_truth_runtime_hash_rgba(
      ctx->truth_rgba, ctx->truth_width, ctx->truth_height, ctx->width, ctx->height);
  ctx->semantic_nodes_applied_count = ctx->semantic_nodes_count > 0u ? ctx->semantic_nodes_count : 1u;
  ctx->semantic_nodes_applied_hash = ctx->truth_runtime_hash != 0u ? ctx->truth_runtime_hash : ctx->truth_source_hash;
  return 1;
}

static int cheng_semantic_load_nodes(ChengAppRuntimeCtx* ctx) {
  if (ctx == NULL) {
    return 0;
  }
  const char* root = cheng_runtime_resource_root();
  if (root == NULL || root[0] == '\0') {
    ctx->semantic_nodes_loaded = 1;
    ctx->semantic_nodes_ready = 0;
    ctx->semantic_nodes_count = 0u;
    ctx->semantic_nodes_hash = cheng_fnv1a64(NULL, 0u);
    ctx->semantic_nodes_applied_count = 0u;
    ctx->semantic_nodes_applied_hash = cheng_fnv1a64(NULL, 0u);
    ctx->semantic_nodes_path[0] = '\0';
    return 0;
  }

  char path[CHENG_SEMANTIC_PATH_CAP * 2u];
  path[0] = '\0';
  (void)snprintf(path, sizeof(path), "%s/r2c_semantic_render_nodes.tsv", root);
  if (ctx->semantic_nodes_loaded && strcmp(ctx->semantic_nodes_path, path) == 0) {
    return ctx->semantic_nodes_ready ? 1 : 0;
  }

  FILE* fp = fopen(path, "rb");
  ctx->semantic_nodes_loaded = 1;
  ctx->semantic_nodes_ready = 0;
  ctx->semantic_nodes_count = 0u;
  ctx->semantic_nodes_hash = cheng_file_fnv64(path);
  ctx->semantic_nodes_applied_count = 0u;
  ctx->semantic_nodes_applied_hash = cheng_fnv1a64(NULL, 0u);
  cheng_safe_copy(ctx->semantic_nodes_path, (uint32_t)sizeof(ctx->semantic_nodes_path), path);
  if (fp == NULL) {
    return 0;
  }

  char line[CHENG_SEMANTIC_LINE_CAP];
  while (fgets(line, (int)sizeof(line), fp) != NULL) {
    size_t raw_len = strnlen(line, sizeof(line));
    while (raw_len > 0u && (line[raw_len - 1u] == '\n' || line[raw_len - 1u] == '\r')) {
      line[raw_len - 1u] = '\0';
      raw_len -= 1u;
    }
    if (raw_len == 0u || line[0] == '#') {
      continue;
    }

    char* fields[8];
    uint32_t field_count = 0u;
    char* p = line;
    fields[field_count++] = p;
    while (*p != '\0') {
      if (*p == '\t') {
        *p = '\0';
        if (field_count < 8u) {
          fields[field_count++] = p + 1;
        }
      }
      p += 1;
    }
    if (field_count < 8u) {
      continue;
    }
    if (ctx->semantic_nodes_count >= CHENG_SEMANTIC_MAX_NODES) {
      break;
    }
    if (!cheng_semantic_ensure_capacity(ctx, ctx->semantic_nodes_count + 1u)) {
      break;
    }

    ChengSemanticRenderNode* node = &ctx->semantic_nodes[ctx->semantic_nodes_count];
    memset(node, 0, sizeof(*node));
    cheng_safe_copy(node->node_id, (uint32_t)sizeof(node->node_id), fields[0]);
    cheng_safe_copy(node->route_hint, (uint32_t)sizeof(node->route_hint), fields[1]);
    cheng_safe_copy(node->role, (uint32_t)sizeof(node->role), fields[2]);
    cheng_decode_hex_utf8(fields[3], node->label, (uint32_t)sizeof(node->label));
    cheng_safe_copy(node->selector, (uint32_t)sizeof(node->selector), fields[4]);
    cheng_safe_copy(node->event_binding, (uint32_t)sizeof(node->event_binding), fields[5]);
    cheng_safe_copy(node->source_module, (uint32_t)sizeof(node->source_module), fields[6]);
    cheng_safe_copy(node->jsx_path, (uint32_t)sizeof(node->jsx_path), fields[7]);
    if (node->label[0] == '\0' && node->selector[0] != '\0') {
      if (node->selector[0] == '#') {
        cheng_safe_copy(node->label, (uint32_t)sizeof(node->label), node->selector);
      } else {
        char prefixed[CHENG_SEMANTIC_SELECTOR_CAP + 2u];
        prefixed[0] = '#';
        prefixed[1] = '\0';
        strncat(prefixed, node->selector, sizeof(prefixed) - 2u);
        cheng_safe_copy(node->label, (uint32_t)sizeof(node->label), prefixed);
      }
    }
    node->clickable = cheng_str_contains(node->event_binding, "onClick") ||
                      cheng_str_contains(node->event_binding, "onChange") ||
                      cheng_str_contains(node->event_binding, "onInput");
    char sig[1024];
    sig[0] = '\0';
    (void)snprintf(
        sig,
        sizeof(sig),
        "%s|%s|%s|%s|%s|%s|%s|%s",
        node->node_id,
        node->route_hint,
        node->role,
        node->label,
        node->selector,
        node->event_binding,
        node->source_module,
        node->jsx_path);
    node->stable_hash = cheng_fnv1a64((const uint8_t*)sig, (uint32_t)strlen(sig));
    ctx->semantic_nodes_count += 1u;
  }
  fclose(fp);
  ctx->semantic_nodes_ready = ctx->semantic_nodes_count > 0u ? 1 : 0;
  return ctx->semantic_nodes_ready ? 1 : 0;
}

static int cheng_render_semantic_nodes(ChengAppRuntimeCtx* ctx) {
  if (ctx == NULL || ctx->pixels == NULL || ctx->pixels_len == 0u) {
    return 0;
  }
  uint64_t applied_hash = cheng_fnv1a64(NULL, 0u);
  uint32_t applied_count = 0u;
  if (cheng_render_semantic_nav_surface(ctx, &applied_count, &applied_hash)) {
    if (applied_count == 0u) {
      (void)cheng_draw_route_text_line(ctx, 20, 160, 0xFF64748Bu, 16, "semantic-empty");
    }
    ctx->semantic_nodes_applied_count = applied_count;
    ctx->semantic_nodes_applied_hash = applied_hash;
    return applied_count > 0u ? 1 : 0;
  }

  cheng_fill_solid(ctx, 0xFFFFFFFFu);
  int y = 18;
  if (ctx->route_state[0] != '\0') {
    (void)cheng_draw_route_text_line(ctx, 12, y, 0xFF0F172Au, 24, ctx->route_state);
    y += 30;
  }

  for (uint32_t i = 0u; i < ctx->semantic_nodes_count; i++) {
    ChengSemanticRenderNode* node = &ctx->semantic_nodes[i];
    if (node->label[0] == '\0') {
      continue;
    }
    if (!cheng_semantic_route_visible(ctx->route_state, node->route_hint)) {
      continue;
    }
    int indent = cheng_semantic_indent(node->jsx_path);
    int x = 12 + indent * 10;
    int font_px = cheng_semantic_font_px(node->role);
    int row_h = font_px + 6;
    if (row_h < 16) {
      row_h = 16;
    }
    if (y + row_h + 8 >= ctx->height) {
      break;
    }
    (void)cheng_draw_route_text_line(ctx, x, y, cheng_semantic_text_color(node->role), font_px, node->label);
    y += row_h + 4;
    applied_count += 1u;
    char hash_text[20];
    hash_text[0] = '\0';
    (void)snprintf(hash_text, sizeof(hash_text), "%016llx", (unsigned long long)node->stable_hash);
    applied_hash = cheng_fnv1a64_extend(applied_hash, (const uint8_t*)hash_text, (uint32_t)strlen(hash_text));
  }

  if (applied_count == 0u) {
    char empty_msg[160];
    empty_msg[0] = '\0';
    (void)snprintf(empty_msg, sizeof(empty_msg), "semantic-empty:%s", ctx->route_state);
    (void)cheng_draw_route_text_line(ctx, 12, y, 0xFF64748Bu, 18, empty_msg);
  }

  ctx->semantic_nodes_applied_count = applied_count;
  ctx->semantic_nodes_applied_hash = applied_hash;
  return applied_count > 0u ? 1 : 0;
}

static void cheng_side_effect_push(ChengAppRuntimeCtx* ctx, const char* kind, const char* payload);

static void cheng_fill_frame_semantic_missing(ChengAppRuntimeCtx* ctx) {
  if (ctx == NULL || ctx->pixels == NULL || ctx->pixels_len == 0u) {
    return;
  }
  cheng_fill_solid(ctx, cheng_pack_rgba(255u, 255u, 255u));
  ctx->semantic_nodes_applied_count = 0u;
  ctx->semantic_nodes_applied_hash = cheng_fnv1a64(NULL, 0u);
  cheng_side_effect_push(ctx, "semantic-runtime-missing", ctx->route_state);
}

static uint32_t cheng_side_effect_count(const ChengAppRuntimeCtx* ctx) {
  if (ctx == NULL) {
    return 0u;
  }
  if (ctx->side_effect_tail >= ctx->side_effect_head) {
    return ctx->side_effect_tail - ctx->side_effect_head;
  }
  return CHENG_SIDE_EFFECT_QUEUE_CAP - (ctx->side_effect_head - ctx->side_effect_tail);
}

static void cheng_side_effect_push_raw(ChengAppRuntimeCtx* ctx, const char* envelope) {
  if (ctx == NULL || envelope == NULL || envelope[0] == '\0') {
    return;
  }
  uint32_t count = cheng_side_effect_count(ctx);
  if (count >= CHENG_SIDE_EFFECT_QUEUE_CAP - 1u) {
    ctx->side_effect_head = (ctx->side_effect_head + 1u) % CHENG_SIDE_EFFECT_QUEUE_CAP;
  }
  uint32_t slot = ctx->side_effect_tail % CHENG_SIDE_EFFECT_QUEUE_CAP;
  cheng_safe_copy(ctx->side_effect_payloads[slot], CHENG_SIDE_EFFECT_PAYLOAD_CAP, envelope);
  ctx->side_effect_tail = (ctx->side_effect_tail + 1u) % CHENG_SIDE_EFFECT_QUEUE_CAP;
}

static void cheng_side_effect_push(ChengAppRuntimeCtx* ctx, const char* kind, const char* payload) {
  if (ctx == NULL || kind == NULL || kind[0] == '\0') {
    return;
  }
  ctx->side_effect_seq += 1u;
  char row[CHENG_SIDE_EFFECT_PAYLOAD_CAP];
  row[0] = '\0';
  (void)snprintf(
      row,
      sizeof(row),
      "id=%llu;kind=%s;route=%s;payload=%s",
      (unsigned long long)ctx->side_effect_seq,
      kind,
      ctx->route_state,
      payload != NULL ? payload : "");
  cheng_side_effect_push_raw(ctx, row);
}

static int32_t cheng_side_effect_pop(ChengAppRuntimeCtx* ctx, char* out_buf, int32_t out_cap) {
  if (ctx == NULL || out_buf == NULL || out_cap <= 1) {
    return 0;
  }
  if (ctx->side_effect_head == ctx->side_effect_tail) {
    out_buf[0] = '\0';
    return 0;
  }
  uint32_t slot = ctx->side_effect_head % CHENG_SIDE_EFFECT_QUEUE_CAP;
  const char* src = ctx->side_effect_payloads[slot];
  if (src == NULL) {
    out_buf[0] = '\0';
    ctx->side_effect_head = (ctx->side_effect_head + 1u) % CHENG_SIDE_EFFECT_QUEUE_CAP;
    return 0;
  }
  uint32_t src_len = (uint32_t)strlen(src);
  if ((uint32_t)out_cap <= src_len) {
    uint32_t n = (uint32_t)out_cap - 1u;
    memcpy(out_buf, src, n);
    out_buf[n] = '\0';
    return (int32_t)(src_len + 1u);
  }
  memcpy(out_buf, src, src_len);
  out_buf[src_len] = '\0';
  ctx->side_effect_head = (ctx->side_effect_head + 1u) % CHENG_SIDE_EFFECT_QUEUE_CAP;
  return (int32_t)src_len;
}

static void cheng_refresh_route_state(ChengAppRuntimeCtx* ctx) {
  if (ctx == NULL) {
    return;
  }
  const char* kv = cheng_mobile_host_runtime_launch_args_kv();
  /* Default policy: truth-first 1:1 visual runtime. */
  ctx->strict_truth_mode = 1;
  char gate_mode[64];
  gate_mode[0] = '\0';
  if (cheng_parse_kv_value(kv, "gate_mode", gate_mode, (uint32_t)sizeof(gate_mode))) {
    if (strcmp(gate_mode, "android-semantic-visual-1to1") == 0) {
      ctx->strict_truth_mode = 1;
    } else {
      ctx->strict_truth_mode = 1;
    }
  }
  char truth_mode[32];
  truth_mode[0] = '\0';
  if (cheng_parse_kv_value(kv, "truth_mode", truth_mode, (uint32_t)sizeof(truth_mode))) {
    if (strcmp(truth_mode, "1") == 0 || strcmp(truth_mode, "true") == 0 ||
               strcmp(truth_mode, "on") == 0 || strcmp(truth_mode, "strict") == 0) {
      ctx->strict_truth_mode = 1;
    } else {
      /* Hard gate: ignore non-strict requests and keep truth-first mode. */
      ctx->strict_truth_mode = 1;
    }
  }

  char route[sizeof(ctx->route_state)];
  route[0] = '\0';
  if (!cheng_parse_kv_value(kv, "route_state", route, (uint32_t)sizeof(route))) {
    (void)cheng_parse_kv_value(kv, "route", route, (uint32_t)sizeof(route));
  }
  char route_lock_text[16];
  route_lock_text[0] = '\0';
  if (cheng_parse_kv_value(kv, "route_lock", route_lock_text, (uint32_t)sizeof(route_lock_text))) {
    if (strcmp(route_lock_text, "1") == 0 ||
        strcmp(route_lock_text, "true") == 0 ||
        strcmp(route_lock_text, "TRUE") == 0) {
      ctx->route_arg_lock = 1;
    } else {
      ctx->route_arg_lock = 0;
    }
  } else {
    ctx->route_arg_lock = 0;
  }
  char expected_hash[32];
  expected_hash[0] = '\0';
  if (cheng_parse_kv_value(kv, "expected_framehash", expected_hash, (uint32_t)sizeof(expected_hash))) {
    uint64_t parsed = 0u;
    if (cheng_parse_hex_u64(expected_hash, &parsed)) {
      ctx->expected_frame_hash = parsed;
      ctx->has_expected_frame_hash = 1;
    } else {
      ctx->expected_frame_hash = 0u;
      ctx->has_expected_frame_hash = 0;
    }
  } else {
    ctx->expected_frame_hash = 0u;
    ctx->has_expected_frame_hash = 0;
  }
  char dump_file[CHENG_FRAME_DUMP_FILE_CAP];
  dump_file[0] = '\0';
  if (cheng_parse_kv_value(kv, "frame_dump_file", dump_file, (uint32_t)sizeof(dump_file))) {
    if (strncmp(ctx->frame_dump_file, dump_file, sizeof(ctx->frame_dump_file) - 1u) != 0) {
      strncpy(ctx->frame_dump_file, dump_file, sizeof(ctx->frame_dump_file) - 1u);
      ctx->frame_dump_file[sizeof(ctx->frame_dump_file) - 1u] = '\0';
      ctx->frame_dump_written = 0;
    }
  } else if (ctx->frame_dump_file[0] != '\0') {
    ctx->frame_dump_file[0] = '\0';
    ctx->frame_dump_written = 0;
  }
  if (route[0] == '\0') {
    if (ctx->route_state[0] == '\0') {
      char auto_route[sizeof(ctx->route_state)];
      auto_route[0] = '\0';
      if (cheng_semantic_load_nodes(ctx)) {
        (void)cheng_pick_default_semantic_route(ctx, auto_route, (uint32_t)sizeof(auto_route));
      }
      if (auto_route[0] == '\0') {
        cheng_safe_copy(auto_route, (uint32_t)sizeof(auto_route), "home_default");
      }
      cheng_set_route_state(ctx, auto_route, "auto-default");
    }
    return;
  }
  if (ctx->route_arg_lock) {
    cheng_set_route_state(ctx, route, "launch-arg-lock");
    ctx->route_seeded = 1;
    return;
  }
  if (!ctx->route_seeded || ctx->route_state[0] == '\0') {
    cheng_set_route_state(ctx, route, "launch-arg-seed");
    ctx->route_seeded = 1;
  }
}

static void cheng_try_dump_frame(ChengAppRuntimeCtx* ctx) {
  if (ctx == NULL || ctx->pixels == NULL || ctx->pixels_len == 0u) {
    return;
  }
  if (ctx->frame_dump_written || ctx->frame_dump_file[0] == '\0') {
    return;
  }
  char path[256];
  path[0] = '\0';
#if defined(__ANDROID__)
  (void)snprintf(path, sizeof(path), "/data/data/com.cheng.mobile/files/%s", ctx->frame_dump_file);
#else
  (void)snprintf(path, sizeof(path), "%s", ctx->frame_dump_file);
#endif
  FILE* fp = fopen(path, "wb");
  if (fp == NULL) {
    return;
  }
  size_t bytes = (size_t)ctx->pixels_len * sizeof(uint32_t);
  size_t wrote = fwrite((const void*)ctx->pixels, 1u, bytes, fp);
  fclose(fp);
  if (wrote == bytes) {
    ctx->frame_dump_written = 1;
  }
}

static void cheng_log_error(const char* msg) {
  if (s_host_api.log_print != NULL) {
    s_host_api.log_print(3, msg);
  }
  cheng_mobile_host_emit_log(msg);
}

static void cheng_ensure_pixels(ChengAppRuntimeCtx* ctx) {
  if (ctx == NULL || ctx->width <= 0 || ctx->height <= 0) {
    return;
  }
  uint64_t want_px = (uint64_t)ctx->width * (uint64_t)ctx->height;
  if (want_px == 0u || want_px > 134217728u) {
    cheng_log_error("cheng_app_tick: invalid surface size");
    return;
  }
  if (ctx->pixels != NULL && ctx->pixels_len == (uint32_t)want_px) {
    return;
  }
  free(ctx->pixels);
  ctx->pixels = (uint32_t*)malloc((size_t)want_px * sizeof(uint32_t));
  if (ctx->pixels == NULL) {
    ctx->pixels_len = 0u;
    cheng_log_error("cheng_app_tick: alloc pixels failed");
    return;
  }
  ctx->pixels_len = (uint32_t)want_px;
}

static void cheng_fill_frame(ChengAppRuntimeCtx* ctx) {
  if (ctx == NULL || ctx->pixels == NULL || ctx->pixels_len == 0u) {
    return;
  }
  int semantic_ready = cheng_semantic_load_nodes(ctx);
  int truth_ready = 0;
  const int strict_truth_active = (ctx->strict_truth_mode && ctx->route_state[0] != '\0') ? 1 : 0;
  if (strict_truth_active) {
    if (cheng_truth_load_route(ctx, ctx->route_state)) {
      truth_ready = cheng_truth_render_frame(ctx);
    }
  }
  if (strict_truth_active) {
    if (truth_ready) {
      semantic_ready = 1;
    } else {
      semantic_ready = 0;
      cheng_fill_frame_semantic_missing(ctx);
    }
  } else if (semantic_ready) {
    (void)cheng_render_semantic_nodes(ctx);
  } else {
    cheng_fill_frame_semantic_missing(ctx);
  }

  const int width = ctx->width;
  const int height = ctx->height;
  if (!ctx->focused) {
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        uint32_t px = ctx->pixels[(size_t)y * (size_t)width + (size_t)x];
        uint32_t r = (px >> 16u) & 0xFFu;
        uint32_t g = (px >> 8u) & 0xFFu;
        uint32_t b = px & 0xFFu;
        r = (r * 3u) / 5u;
        g = (g * 3u) / 5u;
        b = (b * 3u) / 5u;
        ctx->pixels[(size_t)y * (size_t)width + (size_t)x] = cheng_pack_rgba(r, g, b);
      }
    }
  }
  if (ctx->ime_visible) {
    int top = height - (height / 6);
    if (top < 0) {
      top = 0;
    }
    for (int y = top; y < height; y++) {
      for (int x = 0; x < width; x++) {
        ctx->pixels[(size_t)y * (size_t)width + (size_t)x] = cheng_pack_rgba(241u, 245u, 249u);
      }
    }
    (void)cheng_draw_route_text_line(ctx, 12, top + 10, 0xFF334155u, 18, "IME");
  }
  if (ctx->text_input_last[0] != '\0') {
    char input_row[CHENG_TEXT_INPUT_CAP + 8u];
    input_row[0] = '\0';
    (void)snprintf(input_row, sizeof(input_row), "IN:%s", ctx->text_input_last);
    (void)cheng_draw_route_text_line(ctx, 12, 8, 0xFF0F172Au, 16, input_row);
  }
  ctx->last_frame_hash = cheng_fnv1a64((const uint8_t*)ctx->pixels, ctx->pixels_len * (uint32_t)sizeof(uint32_t));
  cheng_try_dump_frame(ctx);
  uint64_t reported_hash = ctx->last_frame_hash;
  char reason[384];
  reason[0] = '\0';
  int scale_milli = (int)(ctx->scale * 1000.0f + 0.5f);
  if (scale_milli < 0) {
    scale_milli = 0;
  }
  (void)snprintf(
      reason,
      sizeof(reason),
      "route=%s framehash=%016llx st=%d sn=%u sth=%016llx sa=%u sah=%016llx sr=%d w=%d h=%d scm=%d tw=%d th=%d",
      ctx->route_state,
      (unsigned long long)reported_hash,
      ctx->semantic_nodes_loaded ? 1 : 0,
      ctx->semantic_nodes_count,
      (unsigned long long)ctx->semantic_nodes_hash,
      ctx->semantic_nodes_applied_count,
      (unsigned long long)ctx->semantic_nodes_applied_hash,
      semantic_ready ? 1 : 0,
      ctx->width,
      ctx->height,
      scale_milli,
      ctx->truth_width,
      ctx->truth_height);
  (void)snprintf(
      reason + strlen(reason),
      sizeof(reason) - strlen(reason),
      " td=%d ls=%d",
      ctx->touch_dispatch_count,
      ctx->touch_last_slot);
  cheng_mobile_host_runtime_mark_stopped(reason);
  cheng_mobile_host_runtime_mark_started();
  if (ctx->has_expected_frame_hash && ctx->expected_frame_hash != ctx->last_frame_hash) {
    char mismatch[128];
    mismatch[0] = '\0';
    (void)snprintf(
        mismatch,
        sizeof(mismatch),
        "expected=%016llx;got=%016llx",
        (unsigned long long)ctx->expected_frame_hash,
        (unsigned long long)ctx->last_frame_hash);
    cheng_side_effect_push(ctx, "framehash-mismatch", mismatch);
  } else if ((ctx->frame_count % 60u) == 0u) {
    char stable[64];
    stable[0] = '\0';
    (void)snprintf(stable, sizeof(stable), "hash=%016llx", (unsigned long long)ctx->last_frame_hash);
    cheng_side_effect_push(ctx, "framehash", stable);
  }
}

uint64_t cheng_app_capabilities(void) {
  return CHENG_MOBILE_CAP_RENDER_SEMANTIC |
         CHENG_MOBILE_CAP_SIDE_EFFECT_BRIDGE |
         CHENG_MOBILE_CAP_FRAME_CAPTURE |
         CHENG_MOBILE_CAP_IME_INPUT;
}

static void cheng_ring_push_touch(ChengAppRuntimeCtx* ctx, const ChengRingTouchEventV1* ev) {
  if (ctx == NULL || ev == NULL) {
    return;
  }
  const uint32_t size = (uint32_t)sizeof(ChengRingTouchEventV1);
  if (size >= CHENG_RING_CAPACITY) {
    return;
  }
  uint32_t w = atomic_load_explicit(&ctx->ring_write_idx, memory_order_relaxed);
  uint32_t r = atomic_load_explicit(&ctx->ring_read_idx, memory_order_acquire);
  uint32_t used = (w >= r) ? (w - r) : (CHENG_RING_CAPACITY - (r - w));
  if (used + size + 1u >= CHENG_RING_CAPACITY) {
    uint32_t new_r = (r + size) % CHENG_RING_CAPACITY;
    atomic_store_explicit(&ctx->ring_read_idx, new_r, memory_order_release);
    ctx->ring_overflow_count += 1u;
  }
  if (w + size <= CHENG_RING_CAPACITY) {
    memcpy(&ctx->ring_data[w], ev, size);
  } else {
    uint32_t head = CHENG_RING_CAPACITY - w;
    memcpy(&ctx->ring_data[w], ev, head);
    memcpy(&ctx->ring_data[0], ((const uint8_t*)ev) + head, size - head);
  }
  w = (w + size) % CHENG_RING_CAPACITY;
  atomic_store_explicit(&ctx->ring_write_idx, w, memory_order_release);
}

static int cheng_ring_pop_touch(ChengAppRuntimeCtx* ctx, ChengRingTouchEventV1* out_ev) {
  if (ctx == NULL || out_ev == NULL) {
    return 0;
  }
  const uint32_t size = (uint32_t)sizeof(ChengRingTouchEventV1);
  uint32_t r = atomic_load_explicit(&ctx->ring_read_idx, memory_order_relaxed);
  uint32_t w = atomic_load_explicit(&ctx->ring_write_idx, memory_order_acquire);
  if (r == w) {
    return 0;
  }
  if (r + size <= CHENG_RING_CAPACITY) {
    memcpy(out_ev, &ctx->ring_data[r], size);
  } else {
    uint32_t head = CHENG_RING_CAPACITY - r;
    memcpy(out_ev, &ctx->ring_data[r], head);
    memcpy(((uint8_t*)out_ev) + head, &ctx->ring_data[0], size - head);
  }
  r = (r + size) % CHENG_RING_CAPACITY;
  atomic_store_explicit(&ctx->ring_read_idx, r, memory_order_release);
  return 1;
}

uint64_t cheng_app_init(void) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (ctx->initialized) {
    return ctx->app_id;
  }

  ChengMobileConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
#if defined(__ANDROID__)
  cfg.platform = MOBILE_ANDROID;
#elif defined(__APPLE__)
  cfg.platform = MOBILE_IOS;
#else
  cfg.platform = MOBILE_HARMONY;
#endif
  cfg.resourceRoot = cheng_runtime_resource_root();
  cfg.title = "cheng_app_v2";
  cfg.width = CHENG_APP_DEFAULT_WIDTH;
  cfg.height = CHENG_APP_DEFAULT_HEIGHT;
  cfg.highDpi = 1;

  (void)cheng_mobile_host_init(&cfg);
  (void)cheng_mobile_host_open_window(&cfg);
  cheng_mobile_host_runtime_mark_started();

  ctx->initialized = 1;
  ctx->app_id = 1u;
  ctx->window_id = 1u;
  ctx->width = cfg.width;
  ctx->height = cfg.height;
  ctx->scale = 1.0f;
  ctx->paused = 0;
  ctx->frame_count = 0u;
  ctx->has_touch = 0;
  ctx->touch_dispatch_count = 0;
  ctx->touch_last_slot = -1;
  atomic_store_explicit(&ctx->ring_write_idx, 0u, memory_order_relaxed);
  atomic_store_explicit(&ctx->ring_read_idx, 0u, memory_order_relaxed);
  ctx->ring_desc.base = ctx->ring_data;
  ctx->ring_desc.capacity = CHENG_RING_CAPACITY;
  ctx->ring_desc.write_idx = &ctx->ring_write_idx;
  ctx->ring_desc.read_idx = &ctx->ring_read_idx;
  ctx->route_state[0] = '\0';
  ctx->route_seeded = 0;
  ctx->route_arg_lock = 0;
  ctx->expected_frame_hash = 0u;
  ctx->has_expected_frame_hash = 0;
  ctx->frame_dump_file[0] = '\0';
  ctx->frame_dump_written = 0;
  ctx->semantic_nodes_loaded = 0;
  ctx->semantic_nodes_ready = 0;
  ctx->semantic_nodes_count = 0u;
  ctx->semantic_nodes_hash = cheng_fnv1a64(NULL, 0u);
  ctx->semantic_nodes_applied_count = 0u;
  ctx->semantic_nodes_applied_hash = cheng_fnv1a64(NULL, 0u);
  ctx->semantic_nodes_path[0] = '\0';
  cheng_truth_release(ctx);
  ctx->truth_loaded = 0;
  ctx->truth_route[0] = '\0';
  ctx->truth_rgba_path[0] = '\0';
  ctx->focused = 1;
  ctx->ime_visible = 0;
  ctx->key_code_last = 0;
  ctx->key_down_last = 0;
  ctx->key_repeat_last = 0;
  ctx->text_input_last[0] = '\0';
  ctx->ime_composing[0] = '\0';
  ctx->ime_cursor_start = 0;
  ctx->ime_cursor_end = 0;
  ctx->side_effect_seq = 0u;
  ctx->side_effect_head = 0u;
  ctx->side_effect_tail = 0u;
  memset(ctx->side_effect_payloads, 0, sizeof(ctx->side_effect_payloads));
  cheng_side_effect_push(ctx, "runtime-init", "ok");
  return ctx->app_id;
}

void cheng_app_set_window(uint64_t app_id, uint64_t window_id, int physical_w, int physical_h, float scale) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    cheng_log_error("cheng_app_set_window: invalid app_id");
    return;
  }
  int old_w = ctx->width;
  int old_h = ctx->height;
  float old_scale = ctx->scale;
  if (physical_w > 0) {
    ctx->width = physical_w;
  }
  if (physical_h > 0) {
    ctx->height = physical_h;
  }
  if (scale > 0.0f) {
    ctx->scale = scale;
  }
  if (window_id != 0u) {
    ctx->window_id = window_id;
  }
  if (ctx->width != old_w || ctx->height != old_h || ctx->scale != old_scale) {
    char payload[96];
    payload[0] = '\0';
    (void)snprintf(payload, sizeof(payload), "w=%d;h=%d;scale=%.3f", ctx->width, ctx->height, ctx->scale);
    cheng_side_effect_push(ctx, "resize", payload);
  }
}

void cheng_app_tick(uint64_t app_id, float delta_time) {
  (void)delta_time;
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    cheng_log_error("cheng_app_tick: invalid app_id");
    return;
  }
  if (ctx->paused) {
    return;
  }

  ChengRingTouchEventV1 ev;
  while (cheng_ring_pop_touch(ctx, &ev)) {
    if (ev.kind != CHENG_RING_EVENT_TOUCH_V1) {
      continue;
    }
    ctx->last_touch_x = ev.x;
    ctx->last_touch_y = ev.y;
    ctx->has_touch = 1;
    cheng_handle_touch_route_switch(ctx, &ev);
  }

  cheng_refresh_route_state(ctx);
  cheng_ensure_pixels(ctx);
  if (ctx->pixels == NULL) {
    return;
  }
  cheng_fill_frame(ctx);
  cheng_mobile_host_present(ctx->pixels, ctx->width, ctx->height, ctx->width * 4);
  ctx->frame_count += 1u;
}

void cheng_app_on_touch(uint64_t app_id, int action, int pointer_id, float x, float y) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    cheng_log_error("cheng_app_on_touch: invalid app_id");
    return;
  }
  ChengRingTouchEventV1 ev;
  ev.kind = CHENG_RING_EVENT_TOUCH_V1;
  ev.action = action;
  ev.pointer_id = pointer_id;
  ev.x = x;
  ev.y = y;
  // Apply route transition immediately on input to avoid frame-lagged routing stalls.
  cheng_handle_touch_route_switch(ctx, &ev);
  cheng_ring_push_touch(ctx, &ev);
  char payload[128];
  payload[0] = '\0';
  (void)snprintf(payload, sizeof(payload), "action=%d;pointer=%d;x=%.2f;y=%.2f", action, pointer_id, (double)x, (double)y);
  cheng_side_effect_push(ctx, "touch", payload);
}

void cheng_app_on_key(uint64_t app_id, int key_code, int down, int repeat) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    cheng_log_error("cheng_app_on_key: invalid app_id");
    return;
  }
  ctx->key_code_last = key_code;
  ctx->key_down_last = down ? 1 : 0;
  ctx->key_repeat_last = repeat ? 1 : 0;
  char payload[96];
  payload[0] = '\0';
  (void)snprintf(payload, sizeof(payload), "code=%d;down=%d;repeat=%d", key_code, down ? 1 : 0, repeat ? 1 : 0);
  cheng_side_effect_push(ctx, "key", payload);
}

void cheng_app_on_text_input(uint64_t app_id, const char* text_utf8) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    cheng_log_error("cheng_app_on_text_input: invalid app_id");
    return;
  }
  cheng_safe_copy(ctx->text_input_last, CHENG_TEXT_INPUT_CAP, text_utf8);
  cheng_side_effect_push(ctx, "text-input", ctx->text_input_last);
}

void cheng_app_on_ime(uint64_t app_id, int ime_visible, const char* composing_utf8, int cursor_start, int cursor_end) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    cheng_log_error("cheng_app_on_ime: invalid app_id");
    return;
  }
  ctx->ime_visible = ime_visible ? 1 : 0;
  ctx->ime_cursor_start = cursor_start;
  ctx->ime_cursor_end = cursor_end;
  cheng_safe_copy(ctx->ime_composing, CHENG_TEXT_INPUT_CAP, composing_utf8);
  char payload[CHENG_TEXT_INPUT_CAP + 64u];
  payload[0] = '\0';
  (void)snprintf(
      payload,
      sizeof(payload),
      "visible=%d;cursor_start=%d;cursor_end=%d;text=%s",
      ctx->ime_visible,
      ctx->ime_cursor_start,
      ctx->ime_cursor_end,
      ctx->ime_composing);
  cheng_side_effect_push(ctx, "ime", payload);
}

void cheng_app_on_resize(uint64_t app_id, int physical_w, int physical_h, float scale) {
  cheng_app_set_window(app_id, 0u, physical_w, physical_h, scale);
}

void cheng_app_on_focus(uint64_t app_id, int focused) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    cheng_log_error("cheng_app_on_focus: invalid app_id");
    return;
  }
  ctx->focused = focused ? 1 : 0;
  cheng_side_effect_push(ctx, "focus", ctx->focused ? "1" : "0");
}

void cheng_app_pause(uint64_t app_id) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    return;
  }
  ctx->paused = 1;
  cheng_side_effect_push(ctx, "pause", "1");
}

void cheng_app_resume(uint64_t app_id) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    return;
  }
  ctx->paused = 0;
  cheng_side_effect_push(ctx, "resume", "1");
}

void cheng_app_inject_host_api(const ChengHostPlatformAPI* api) {
  if (api == NULL) {
    memset(&s_host_api, 0, sizeof(s_host_api));
    return;
  }
  s_host_api = *api;
}

const ChengInputRingBufferDesc* cheng_app_input_ring_desc(uint64_t app_id) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    return NULL;
  }
  return &ctx->ring_desc;
}

int32_t cheng_app_pull_side_effect(uint64_t app_id, char* out_buf, int32_t out_cap) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    return 0;
  }
  return cheng_side_effect_pop(ctx, out_buf, out_cap);
}

int32_t cheng_app_push_side_effect_result(uint64_t app_id, const char* envelope_utf8) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    return 0;
  }
  if (envelope_utf8 == NULL || envelope_utf8[0] == '\0') {
    return 0;
  }
  /* Route authority is local runtime interaction + launch args only.
   * Side-effect responses must not mutate route_state, otherwise route drift
   * appears under async host callbacks. */
  char text_in[CHENG_TEXT_INPUT_CAP];
  text_in[0] = '\0';
  if (cheng_parse_kv_value(envelope_utf8, "text", text_in, (uint32_t)sizeof(text_in))) {
    cheng_safe_copy(ctx->text_input_last, CHENG_TEXT_INPUT_CAP, text_in);
  }
  (void)cheng_mobile_host_bus_push_response_envelope(envelope_utf8);
  cheng_side_effect_push(ctx, "side-effect-result", envelope_utf8);
  return 1;
}

uint64_t cheng_app_capture_frame_hash(uint64_t app_id) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id) {
    return 0u;
  }
  return ctx->last_frame_hash;
}

int32_t cheng_app_capture_frame_rgba(uint64_t app_id, uint8_t* out_rgba, int32_t out_bytes) {
  ChengAppRuntimeCtx* ctx = &s_ctx;
  if (!ctx->initialized || ctx->app_id != app_id || ctx->pixels == NULL) {
    return 0;
  }
  uint64_t need_u64 = (uint64_t)ctx->pixels_len * 4u;
  if (need_u64 > 2147483647u) {
    return 0;
  }
  int32_t need = (int32_t)need_u64;
  if (out_rgba == NULL || out_bytes < need) {
    return need;
  }
  for (uint32_t i = 0u; i < ctx->pixels_len; i++) {
    uint32_t px = ctx->pixels[i];
    out_rgba[i * 4u + 0u] = (uint8_t)((px >> 16u) & 0xFFu);
    out_rgba[i * 4u + 1u] = (uint8_t)((px >> 8u) & 0xFFu);
    out_rgba[i * 4u + 2u] = (uint8_t)(px & 0xFFu);
    out_rgba[i * 4u + 3u] = (uint8_t)((px >> 24u) & 0xFFu);
  }
  return need;
}
