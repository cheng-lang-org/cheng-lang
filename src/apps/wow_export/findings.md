# wowExport Findings

- 本机 `.build.info` 有 active `wow 12.0.5.67165`，build key 为 `02482dc9c788698c83e7ae0e24ab2bb7`。
- 本机 build config 暴露 root、install、encoding keys；后续必须通过 BLTE + encoding/root 才能把 fileDataID 精确映射到本地 archive。
- 当前仓内仍没有可复用的纯 Cheng zlib/deflate；不能用脚本或 C 依赖绕过。
- `build-backend-driver` 已通过；`primary_object_body_semantics_missing` 不是 wowExport 缺 primary-object 实现，而是同名自递归导出壳和一处 `ref10.feSet` 字段写错导致的编译链路问题。
- `perf_memory_contract_smoke` 当前剩余问题是 no-handoff stage3 编译 libp2p 冷路径耗时超阈值；语义报告中 primary/object/native missing 均已清零。
- 已补纯 Cheng MD5，BLTE 容器 hash 和首块 block hash 已接入真实校验。
- 已补全本地 `.idx` 按 encoding key prefix 精确查找，且热循环已改为字节级比较，能稳定定位 build config 的 `encoding` archive entry。
- 已补纯 Cheng Salsa20 core、hex key 解析和 BLTE 加密块 metadata/nonce/decrypt 边界；缺少 TACT key table 时必须失败并报告 keyName。
