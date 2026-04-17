# v3/runtime

这里只留最薄宿主 ABI。

硬规则：

- C 宿主只提供进程、文件、时间、MsQuic 句柄桥
- 固定布局 `str` 这类字节级原语如果需要 C 侧复用，放在共享 runtime 目录，不要再让 `src/runtime/native` 反向依赖 `v3/runtime/native`
- 不承载编译器、链协议、密码学语义
- 违反前置条件直接 crash，不做兜底
