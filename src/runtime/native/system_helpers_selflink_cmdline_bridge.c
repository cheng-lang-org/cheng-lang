#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif

static int32_t cheng_saved_argc = 0;
static const char **cheng_saved_argv = NULL;

void __cheng_setCmdLine(int32_t argc, const char **argv) {
  if (argc <= 0 || argc > 4096 || argv == NULL) {
    cheng_saved_argc = 0;
    cheng_saved_argv = NULL;
    return;
  }
  cheng_saved_argc = argc;
  cheng_saved_argv = argv;
}

int32_t __cheng_rt_paramCount(void) {
  if (cheng_saved_argc > 0 && cheng_saved_argc <= 4096 && cheng_saved_argv != NULL) {
    return cheng_saved_argc;
  }
#if defined(__APPLE__)
  int *argc_ptr = _NSGetArgc();
  if (argc_ptr == NULL) {
    return 0;
  }
  int argc = *argc_ptr;
  if (argc <= 0 || argc > 4096) {
    return 0;
  }
  return (int32_t)argc;
#else
  return 0;
#endif
}

const char * __cheng_rt_paramStr(int32_t i) {
  if (i >= 0 && cheng_saved_argc > 0 && cheng_saved_argc <= 4096 && cheng_saved_argv != NULL &&
      i < cheng_saved_argc) {
    const char *s = cheng_saved_argv[i];
    return s != NULL ? s : "";
  }
#if defined(__APPLE__)
  if (i < 0) {
    return "";
  }
  int *argc_ptr = _NSGetArgc();
  char ***argv_ptr = _NSGetArgv();
  if (argc_ptr == NULL || argv_ptr == NULL || *argv_ptr == NULL) {
    return "";
  }
  int argc = *argc_ptr;
  if (argc <= 0 || argc > 4096 || i >= argc) {
    return "";
  }
  char *s = (*argv_ptr)[i];
  return s != NULL ? s : "";
#else
  (void)i;
  return "";
#endif
}

char * __cheng_rt_paramStrCopy(int32_t i) {
  const char *s = __cheng_rt_paramStr(i);
  if (s == NULL) {
    s = "";
  }
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1u);
  if (out == NULL) {
    return (char *)"";
  }
  if (n > 0) {
    memcpy(out, s, n);
  }
  out[n] = '\0';
  return out;
}
