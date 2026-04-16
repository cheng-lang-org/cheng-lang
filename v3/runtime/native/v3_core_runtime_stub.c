#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/statvfs.h>
#include "../../../src/runtime/native/system_helpers.h"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#endif

int32_t v3_core_runtime_stub_trace(void) {
  return 0;
}

int32_t cheng_v3_native_errno_code_bridge(void) {
  return errno;
}

int32_t cheng_v3_native_af_inet_bridge(void) {
  return AF_INET;
}

int32_t cheng_v3_native_af_inet6_bridge(void) {
  return AF_INET6;
}

int32_t cheng_v3_native_sock_stream_bridge(void) {
  return SOCK_STREAM;
}

int32_t cheng_v3_native_sock_dgram_bridge(void) {
  return SOCK_DGRAM;
}

int32_t cheng_v3_native_ipproto_ip_bridge(void) {
  return IPPROTO_IP;
}

int32_t cheng_v3_native_sol_socket_bridge(void) {
  return SOL_SOCKET;
}

int32_t cheng_v3_native_so_reuseaddr_bridge(void) {
  return SO_REUSEADDR;
}

int32_t cheng_v3_native_msg_waitall_bridge(void) {
  return MSG_WAITALL;
}

int32_t cheng_v3_native_sockaddr_use_len_field_bridge(void) {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
  return 1;
#else
  return 0;
#endif
}

int32_t cheng_v3_native_system_cpu_logical_cores_value_bridge(void) {
  long value = sysconf(_SC_NPROCESSORS_ONLN);
  if (value <= 0 || value > INT32_MAX) {
    return 0;
  }
  return (int32_t)value;
}

int64_t cheng_v3_native_system_memory_total_bytes_value_bridge(void) {
#if defined(__APPLE__)
  uint64_t mem = 0;
  size_t size = sizeof(mem);
  if (sysctlbyname("hw.memsize", &mem, &size, NULL, 0) == 0 && mem <= (uint64_t)INT64_MAX) {
    return (int64_t)mem;
  }
  return 0;
#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGE_SIZE)
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  if (pages <= 0 || page_size <= 0) {
    return 0;
  }
  if ((uint64_t)pages > ((uint64_t)INT64_MAX / (uint64_t)page_size)) {
    return 0;
  }
  return (int64_t)((uint64_t)pages * (uint64_t)page_size);
#else
  return 0;
#endif
}

int64_t cheng_v3_native_system_memory_available_bytes_value_bridge(void) {
#if defined(__APPLE__)
  vm_statistics64_data_t vmstat;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_size_t page_size = 0;
  if (host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS) {
    return 0;
  }
  if (host_statistics64(mach_host_self(),
                        HOST_VM_INFO64,
                        (host_info64_t)&vmstat,
                        &count) != KERN_SUCCESS) {
    return 0;
  }
  {
    uint64_t pages = (uint64_t)vmstat.free_count +
                     (uint64_t)vmstat.inactive_count +
                     (uint64_t)vmstat.speculative_count;
    uint64_t total = pages * (uint64_t)page_size;
    if (total > (uint64_t)INT64_MAX) {
      return 0;
    }
    return (int64_t)total;
  }
#elif defined(_SC_AVPHYS_PAGES) && defined(_SC_PAGE_SIZE)
  long pages = sysconf(_SC_AVPHYS_PAGES);
  long page_size = sysconf(_SC_PAGE_SIZE);
  if (pages <= 0 || page_size <= 0) {
    return 0;
  }
  if ((uint64_t)pages > ((uint64_t)INT64_MAX / (uint64_t)page_size)) {
    return 0;
  }
  return (int64_t)((uint64_t)pages * (uint64_t)page_size);
#else
  return 0;
#endif
}

int64_t cheng_v3_native_system_disk_total_bytes_value_bridge(void) {
  struct statvfs fsinfo;
  if (statvfs("/", &fsinfo) != 0) {
    return 0;
  }
  {
    uint64_t block_size = fsinfo.f_frsize > 0 ? (uint64_t)fsinfo.f_frsize : (uint64_t)fsinfo.f_bsize;
    uint64_t total = (uint64_t)fsinfo.f_blocks * block_size;
    if (block_size == 0 || total > (uint64_t)INT64_MAX) {
      return 0;
    }
    return (int64_t)total;
  }
}

int64_t cheng_v3_native_system_disk_available_bytes_value_bridge(void) {
  struct statvfs fsinfo;
  if (statvfs("/", &fsinfo) != 0) {
    return 0;
  }
  {
    uint64_t block_size = fsinfo.f_frsize > 0 ? (uint64_t)fsinfo.f_frsize : (uint64_t)fsinfo.f_bsize;
    uint64_t total = (uint64_t)fsinfo.f_bavail * block_size;
    if (block_size == 0 || total > (uint64_t)INT64_MAX) {
      return 0;
    }
    return (int64_t)total;
  }
}

static int cheng_v3_core_bio_hex_encode_utf8(const char* src, char* out, size_t out_cap) {
  static const char* digits = "0123456789abcdef";
  if (out == NULL || out_cap == 0u) {
    return 0;
  }
  if (src == NULL || src[0] == '\0') {
    out[0] = '\0';
    return 1;
  }
  size_t n = strlen(src);
  if (out_cap <= (n * 2u)) {
    return 0;
  }
  for (size_t i = 0u; i < n; i += 1u) {
    uint8_t value = (uint8_t)src[i];
    out[i * 2u] = digits[value >> 4u];
    out[i * 2u + 1u] = digits[value & 0x0fu];
  }
  out[n * 2u] = '\0';
  return 1;
}

__attribute__((weak)) ChengStrBridge cheng_v3_mobile_biometric_fingerprint_authorize_bridge_native(ChengStrBridge request_wire) {
  typedef ChengStrBridge (*ChengV3BioNativeImplFn)(ChengStrBridge);
  ChengV3BioNativeImplFn impl = (ChengV3BioNativeImplFn)dlsym(RTLD_DEFAULT, "cheng_v3_mobile_biometric_fingerprint_authorize_bridge_native_impl");
  if (impl != NULL) {
    return impl(request_wire);
  }
  char error_hex[1024];
  char response_raw[2048];
  error_hex[0] = '\0';
  (void)cheng_v3_core_bio_hex_encode_utf8("v3 biometric: mobile host bridge missing", error_hex, sizeof(error_hex));
  (void)snprintf(response_raw,
                 sizeof(response_raw),
                 "ok=0\nfeature32_hex=\ndevice_binding_seed_hex=\ndevice_label_hex=\nsensor_id_hex=\nhardware_attestation_hex=\nerror_hex=%s\n",
                 error_hex);
  return driver_c_str_from_utf8_copy_bridge(response_raw, (int32_t)strlen(response_raw));
}
