# wowExport Progress

当前阶段只接受真实本机数据。缺少已审计 fileDataID 时，北郡预览必须失败，不能生成占位几何。

验证：

- `artifacts/backend_driver/cheng run-host-smokes wow_export_casc_smoke wow_export_md5_smoke wow_export_salsa20_smoke wow_export_blte_smoke wow_export_zlib_smoke wow_export_tact_keys_smoke wow_export_encoding_smoke` 通过。
- 纯 Cheng zlib/deflate inflate 已接入 BLTE `0x5a` 块，覆盖 stored/fixed Huffman smoke；dynamic Huffman 解析已实现，后续用真实 encoding/root 读取链路继续压实。
- `wow_export_tool_main probe` 通过，识别 `wow 12.0.5.67165` 和本地 `.idx`。
- `wow_export_casc_smoke` 曾因 `.idx` 热循环逐条转 hex 字符串超时；已改为字节级 key prefix 比较后通过。
- `wow_export_salsa20_smoke` 通过标准 Salsa20 32-byte key/zero nonce 向量；原测试期望中的 `c7cbdd3f` 已修为正确的 `c7cbbd3f`。
- `wow_export_blte_smoke` 覆盖普通块、多块 BLTE 合并、容器 MD5、坏 MD5、合成 Salsa20 加密块和缺 key 失败路径。
- `wow_export_zlib_smoke` 覆盖 zlib header、Adler32、stored block、fixed Huffman block 和 BLTE zlib block。
- `wow_export_tact_keys_smoke` 覆盖 TACT keyring 文本解析、本机 keyring 加载、keyName 查找和 BLTE 加密块 key 表解密。
- `wow_export_encoding_smoke` 覆盖本机 `.idx` 定位、`data.NNN` 读取、30 字节本地 data header 识别、BLTE 多块解码、encoding content key -> encoding key 映射、root 首块解析和 install manifest 解析。
- `wow_export_tool_main probe` 现在输出 encoding/root/install 摘要；本机观测为 `encodingVersion=1`、`cPages=26321`、`ePages=17278`，root encoding key 为 `108aaa378a9a07d9a926150259ecbdad`，root `totalFiles=3191145`，install entries 为 `262`。
- 北郡 manifest 不再只是手填常量；`AuditManifestAgainstLocal` 会用真实 root/encoding/idx 反查每个 fileDataID 的 content key、encoding key、archive、offset、size，任何漂移都直接失败。
- 北郡钟楼 `nsabbeyBell.m2` 现已按真实 `MD21 -> MD20` 头解析；本机实测版本 `272`、动画序列数 `2`、顶点数 `370`，preview 不再写死 `characterVertexCount=0`。
- `artifacts/backend_driver/cheng run-host-smokes wow_export_asset_formats_smoke` 通过，已把纯合成 `MD20` 和 `MD21 -> MD20` 两种头布局钉住。

阻塞：

- `artifacts/backend_driver/cheng build-backend-driver` 已通过。
- 之前那批由同名自递归导出壳和 `ref10.feSet` 真源码错误触发的 `primary_object_body_semantics_missing` 已修掉，但当前 wowExport 大闭包仍会触发另一批同类编译器限制。
- `artifacts/backend_driver/cheng run-host-smokes perf_memory_contract_smoke` 当前剩余失败是 stage3 no-handoff 大 libp2p 冷编译耗时超过默认 30000ms；直接编译报告中 `primary_object_missing_reasons=-`、`object_missing_reasons=-`、`native_link_missing_reasons=-`。
- 本机 `.build.info` 的 `KeyRing` 指向 `3ca57fe7319a297346440e4d2a03a0cd`，本地 keyring 已能通过 `LoadTactKeyTable` 解析。
- 北郡预览现在会先跑本地审计，再读取已审计资产；剩余缺口是把真实几何/动画数据继续推进成更完整的 MVP，而不是只验证头部和 chunk 边界。
- 当前工作区重新编译 `wow_export_root_install_smoke`、`wow_export_northshire_mvp_smoke`、`wow_export_memory_probe_smoke` 会撞到现有编译器的 `primary_object_body_semantics_missing`；这次改动的源码门禁已接上，但本地整链复验被这个现有问题阻塞。
