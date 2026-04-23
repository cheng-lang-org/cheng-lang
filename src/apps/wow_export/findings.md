# wowExport Findings

- 本机 `.build.info` 有 active `wow 12.0.5.67165`，build key 为 `02482dc9c788698c83e7ae0e24ab2bb7`。
- 本机 build config 暴露 root、install、encoding keys；encoding 已能把 root/install content key 映射到本地 archive entry，后续缺口是北郡已审计 fileDataID manifest。
- 已新增可复用纯 Cheng zlib/deflate inflate；入口校验 zlib header、stored/fixed/dynamic deflate 和 Adler32，不依赖脚本或 C 解压。
- `build-backend-driver` 已通过；`primary_object_body_semantics_missing` 不是 wowExport 缺 primary-object 实现，而是同名自递归导出壳和一处 `ref10.feSet` 字段写错导致的编译链路问题。
- `perf_memory_contract_smoke` 当前剩余问题是 no-handoff stage3 编译 libp2p 冷路径耗时超阈值；语义报告中 primary/object/native missing 均已清零。
- 已补纯 Cheng MD5，BLTE 容器 hash 和首块 block hash 已接入真实校验。
- 已补全本地 `.idx` 按 encoding key prefix 精确查找，且热循环已改为字节级比较，能稳定定位 build config 的 `encoding` archive entry。
- 已补纯 Cheng Salsa20 core、hex key 解析和 BLTE 加密块 metadata/nonce/decrypt 边界；缺少 TACT key table 时必须失败并报告 keyName。
- 已补 TACT key table 解析与本地 keyring 加载，BLTE 加密块可从 keyName 查表解密；key 缺失仍硬失败。
- 本机 data archive 的 `.idx` entry 前置 30 字节本地 header，BLTE magic 位于 `entry.offset + 30`；读取层已严格支持 offset 0 和 offset 30 两种入口。
- 已打通 BLTE 多块解码和 encoding content key 查找；本机 encoding header 为 version 1、content pages 26321、encoding pages 17278。
- 本机 root content key `4d538d779c8ce517fed1b8718bea2b79` 映射到 encoding key `108aaa378a9a07d9a926150259ecbdad`；root 首块解析为 version 2、totalFiles 3191145、首个 fileDataID 121595。
- 本机 install encoding key `b3fcfc3083d76138ab9463a91cff3e81` 可解码并解析 install manifest；当前 entries 262，首项为 `BlizzardError.exe`。
- 北郡 manifest 的 4 个 fileDataID 已做本地反查审计；preview 入口现在强制校验 fileDataID -> content key -> encoding key -> archive/offset/size 全链一致，避免条目漂移后继续误读旧数据。
- 本机 `World/Azeroth/ELWYNN/ActiveDoodads/AbbeyBell/nsabbeyBell.m2` 不是“文件开头直接版本号”的旧读法，而是 `MD21` 包装里嵌一个真正的 `MD20` 头；真实头里版本 `272`、序列数 `2`、骨骼数 `7`、顶点数 `370`、skin 数 `1`、贴图数 `2`。
