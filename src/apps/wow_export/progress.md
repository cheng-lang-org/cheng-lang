# wowExport Progress

当前阶段只接受真实本机数据。缺少已审计 fileDataID 时，北郡预览必须失败，不能生成占位几何。

验证：

- `artifacts/backend_driver/cheng run-host-smokes wow_export_casc_smoke wow_export_md5_smoke wow_export_salsa20_smoke wow_export_blte_smoke wow_export_northshire_mvp_smoke` 通过。
- `wow_export_tool_main probe` 通过，识别 `wow 12.0.5.67165` 和本地 `.idx`。
- `wow_export_casc_smoke` 曾因 `.idx` 热循环逐条转 hex 字符串超时；已改为字节级 key prefix 比较后通过。
- `wow_export_salsa20_smoke` 通过标准 Salsa20 32-byte key/zero nonce 向量；原测试期望中的 `c7cbdd3f` 已修为正确的 `c7cbbd3f`。
- `wow_export_blte_smoke` 覆盖普通块、容器 MD5、坏 MD5、合成 Salsa20 加密块和缺 key 失败路径。

阻塞：

- `artifacts/backend_driver/cheng build-backend-driver` 已通过。
- `primary_object_body_semantics_missing` 已修复；原因是去旧版本前缀后留下同名同签名自递归导出壳，另有 `ref10.feSet` 的 `f. = val` 真源码错误。
- `artifacts/backend_driver/cheng run-host-smokes perf_memory_contract_smoke` 当前剩余失败是 stage3 no-handoff 大 libp2p 冷编译耗时超过默认 30000ms；直接编译报告中 `primary_object_missing_reasons=-`、`object_missing_reasons=-`、`native_link_missing_reasons=-`。
