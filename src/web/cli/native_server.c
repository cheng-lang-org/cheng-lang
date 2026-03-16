#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef SERVER_HANDLE
#define SERVER_HANDLE server_handle
#endif

extern void *SERVER_HANDLE(void *req_ptr, int req_len, int *out_len);

typedef struct {
  int max_header;
  int max_body;
  int timeout_ms;
} ServerConfig;

typedef struct {
  int keep_alive;
} HttpConnectionInfo;

typedef enum {
  REQ_OK = 0,
  REQ_TIMEOUT = 1,
  REQ_TOO_LARGE_HEADER = 2,
  REQ_TOO_LARGE_BODY = 3,
  REQ_BAD = 4
} RequestStatus;

static int find_substr(const char *buf, int len, const char *needle) {
  int nlen = (int)strlen(needle);
  if (nlen == 0 || len < nlen) {
    return -1;
  }
  for (int i = 0; i <= len - nlen; i += 1) {
    int matched = 1;
    for (int j = 0; j < nlen; j += 1) {
      if (buf[i + j] != needle[j]) {
        matched = 0;
        break;
      }
    }
    if (matched) {
      return i;
    }
  }
  return -1;
}

static int ci_prefix(const char *line, int len, const char *key) {
  int klen = (int)strlen(key);
  if (len < klen) {
    return 0;
  }
  for (int i = 0; i < klen; i += 1) {
    char a = line[i];
    char b = key[i];
    if (tolower((unsigned char)a) != tolower((unsigned char)b)) {
      return 0;
    }
  }
  return 1;
}

static void send_error_response(int client, int status, const char *reason) {
  const char *text = reason ? reason : "";
  char body[128];
  int body_len = 0;
  if (*text) {
    body_len = snprintf(body, sizeof(body), "%d %s", status, text);
  }
  char header[256];
  int header_len = snprintf(
      header,
      sizeof(header),
      "HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\n\r\n",
      status,
      text,
      body_len);
  if (header_len > 0) {
    send(client, header, (size_t)header_len, 0);
  }
  if (body_len > 0) {
    send(client, body, (size_t)body_len, 0);
  }
}

static int parse_content_length(const char *header, int header_len) {
  const char *p = header;
  const char *end = header + header_len;
  while (p < end) {
    const char *line_end = memchr(p, '\n', (size_t)(end - p));
    if (!line_end) {
      line_end = end;
    }
    int line_len = (int)(line_end - p);
    if (line_len > 0 && p[line_len - 1] == '\r') {
      line_len -= 1;
    }
    if (line_len > 0 && ci_prefix(p, line_len, "Content-Length:")) {
      const char *val = p + strlen("Content-Length:");
      const char *val_end = p + line_len;
      while (val < val_end && (*val == ' ' || *val == '\t')) {
        val++;
      }
      int out = 0;
      while (val < val_end && *val >= '0' && *val <= '9') {
        out = out * 10 + (*val - '0');
        val++;
      }
      return out;
    }
    if (line_end == end) {
      break;
    }
    p = line_end + 1;
  }
  return 0;
}

static void parse_connection_info(const char *header, int header_len, HttpConnectionInfo *info) {
  if (!info) {
    return;
  }
  info->keep_alive = 1;
  const char *p = header;
  const char *end = header + header_len;
  while (p < end) {
    const char *line_end = memchr(p, '\n', (size_t)(end - p));
    if (!line_end) {
      line_end = end;
    }
    int line_len = (int)(line_end - p);
    if (line_len > 0 && p[line_len - 1] == '\r') {
      line_len -= 1;
    }
    if (line_len > 0 && ci_prefix(p, line_len, "Connection:")) {
      const char *val = p + strlen("Connection:");
      const char *val_end = p + line_len;
      while (val < val_end && (*val == ' ' || *val == '\t')) {
        val++;
      }
      if ((val_end - val) >= 5 && ci_prefix(val, 5, "close")) {
        info->keep_alive = 0;
      } else if ((val_end - val) >= 10 && ci_prefix(val, 10, "keep-alive")) {
        info->keep_alive = 1;
      }
    }
    if (line_end == end) {
      break;
    }
    p = line_end + 1;
  }
}

static char *read_request(int fd, int max_header, int max_body, int *out_len, int *out_status) {
  const int chunk = 4096;
  int cap = 0;
  int len = 0;
  char *buf = NULL;
  int header_end = -1;
  int total_expected = -1;
  RequestStatus status = REQ_OK;

  if (max_header <= 0) {
    max_header = 16384;
  }
  if (max_body < 0) {
    max_body = 0;
  }

  for (;;) {
    if (len + chunk + 1 > cap) {
      int next = cap == 0 ? 8192 : cap * 2;
      while (next < len + chunk + 1) {
        next *= 2;
      }
      char *next_buf = (char *)realloc(buf, (size_t)next);
      if (!next_buf) {
        free(buf);
        return NULL;
      }
      buf = next_buf;
      cap = next;
    }
    ssize_t n = recv(fd, buf + len, (size_t)chunk, 0);
    if (n <= 0) {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        status = REQ_TIMEOUT;
      } else if (n < 0) {
        status = REQ_BAD;
      }
      break;
    }
    len += (int)n;
    buf[len] = '\0';
    if (header_end < 0) {
      int idx = find_substr(buf, len, "\r\n\r\n");
      if (idx >= 0) {
        header_end = idx + 4;
        if (header_end > max_header) {
          status = REQ_TOO_LARGE_HEADER;
          break;
        }
        int content_len = parse_content_length(buf, header_end);
        if (content_len > max_body) {
          status = REQ_TOO_LARGE_BODY;
          break;
        }
        total_expected = header_end + content_len;
      }
    } else {
      if (header_end > max_header) {
        status = REQ_TOO_LARGE_HEADER;
        break;
      }
    }
    if (header_end >= 0 && total_expected >= 0 && len >= total_expected) {
      break;
    }
    if (len > max_header + max_body) {
      status = header_end < 0 ? REQ_TOO_LARGE_HEADER : REQ_TOO_LARGE_BODY;
      break;
    }
  }
  if (status != REQ_OK) {
    free(buf);
    if (out_len) {
      *out_len = 0;
    }
    if (out_status) {
      *out_status = (int)status;
    }
    return NULL;
  }
  if (out_len) {
    *out_len = len;
  }
  if (out_status) {
    *out_status = (int)status;
  }
  return buf;
}

static int parse_port(const char *value) {
  if (!value || !*value) {
    return 8787;
  }
  int out = 0;
  while (*value) {
    if (*value < '0' || *value > '9') {
      break;
    }
    out = out * 10 + (*value - '0');
    value++;
  }
  if (out <= 0) {
    return 8787;
  }
  return out;
}

static int parse_int(const char *value, int fallback) {
  if (!value || !*value) {
    return fallback;
  }
  int out = 0;
  int sign = 1;
  if (*value == '-') {
    sign = -1;
    value++;
  }
  while (*value) {
    if (*value < '0' || *value > '9') {
      break;
    }
    out = out * 10 + (*value - '0');
    value++;
  }
  out *= sign;
  return out == 0 ? fallback : out;
}

static ServerConfig load_config(void) {
  ServerConfig cfg;
  cfg.max_header = parse_int(getenv("WEB_SERVER_MAX_HEADER"), 16384);
  cfg.max_body = parse_int(getenv("WEB_SERVER_MAX_BODY"), 1024 * 1024);
  cfg.timeout_ms = parse_int(getenv("WEB_SERVER_TIMEOUT_MS"), 5000);
  return cfg;
}

static void run_server(const char *host, int port, ServerConfig cfg) {
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    return;
  }
  int yes = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (host && *host) {
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
      addr.sin_addr.s_addr = INADDR_ANY;
    }
  } else {
    addr.sin_addr.s_addr = INADDR_ANY;
  }

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(server_fd);
    return;
  }
  if (listen(server_fd, 16) < 0) {
    perror("listen");
    close(server_fd);
    return;
  }
  printf("cheng native server on http://%s:%d\n", host, port);
  for (;;) {
    int client = accept(server_fd, NULL, NULL);
    if (client < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("accept");
      break;
    }
    if (cfg.timeout_ms > 0) {
      struct timeval tv;
      tv.tv_sec = cfg.timeout_ms / 1000;
      tv.tv_usec = (cfg.timeout_ms % 1000) * 1000;
      setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    for (;;) {
      int req_len = 0;
      int req_status = REQ_OK;
      char *req = read_request(client, cfg.max_header, cfg.max_body, &req_len, &req_status);
      if (!req || req_len <= 0) {
        if (req_status == REQ_TIMEOUT) {
          send_error_response(client, 408, "Request Timeout");
        } else if (req_status == REQ_TOO_LARGE_HEADER) {
          send_error_response(client, 431, "Request Header Fields Too Large");
        } else if (req_status == REQ_TOO_LARGE_BODY) {
          send_error_response(client, 413, "Payload Too Large");
        } else if (req_status == REQ_BAD) {
          send_error_response(client, 400, "Bad Request");
        }
        free(req);
        break;
      }
      HttpConnectionInfo conn;
      int header_end = find_substr(req, req_len, "\r\n\r\n");
      if (header_end >= 0) {
        parse_connection_info(req, header_end + 4, &conn);
      } else {
        conn.keep_alive = 0;
      }
      int out_len = 0;
      void *out = SERVER_HANDLE(req, req_len, &out_len);
      if (out && out_len > 0) {
        send(client, out, (size_t)out_len, 0);
        free(out);
      }
      free(req);
      if (!conn.keep_alive) {
        break;
      }
    }
    close(client);
  }
  close(server_fd);
}

void cheng_native_server_run(const char *host, int port) {
  const char *final_host = host && *host ? host : "127.0.0.1";
  int final_port = port > 0 ? port : 8787;
  run_server(final_host, final_port, load_config());
}

int cheng_native_server_main(int argc, char **argv) {
  const char *host = getenv("WEB_SERVER_HOST");
  int port = parse_port(getenv("WEB_SERVER_PORT"));
  ServerConfig cfg = load_config();
  if (!host || !*host) {
    host = "127.0.0.1";
  }
  if (port <= 0) {
    port = 8787;
  }
  for (int i = 1; i < argc; i += 1) {
    const char *arg = argv[i];
    if (!arg) {
      continue;
    }
    if (strncmp(arg, "--host:", 7) == 0) {
      host = arg + 7;
      continue;
    }
    if (strncmp(arg, "--port:", 7) == 0) {
      port = parse_port(arg + 7);
      continue;
    }
    if (strncmp(arg, "--max-header:", 13) == 0) {
      cfg.max_header = parse_int(arg + 13, cfg.max_header);
      continue;
    }
    if (strncmp(arg, "--max-body:", 11) == 0) {
      cfg.max_body = parse_int(arg + 11, cfg.max_body);
      continue;
    }
    if (strncmp(arg, "--timeout-ms:", 13) == 0) {
      cfg.timeout_ms = parse_int(arg + 13, cfg.timeout_ms);
      continue;
    }
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      printf("usage: native_server --host:127.0.0.1 --port:8787 --max-header:16384 --max-body:1048576 --timeout-ms:5000\n");
      return 0;
    }
  }
  run_server(host, port, cfg);
  return 0;
}
