#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/statvfs.h>

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
