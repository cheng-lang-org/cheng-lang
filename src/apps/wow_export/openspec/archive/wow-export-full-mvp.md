# wowExport Full Feature + Northshire MVP Archive

已落地第一阶段可审计基础。

完成：

- 本机 Retail build 探测。
- build config 解析。
- 本地 CASC `.idx` 解析和 encoding key prefix 字节级查找。
- 纯 Cheng MD5、Salsa20。
- BLTE header、容器 MD5、block MD5、normal block 解码、Salsa20 加密块解析/解密入口。
- BLP2/M2/chunked 格式边界骨架。
- 北郡 manifest 入口，当前 `auditedFileCount=0`，预览拒绝生成假场景。
- CLI 命令面。
- `wow_export_casc_smoke`、`wow_export_md5_smoke`、`wow_export_salsa20_smoke`、`wow_export_blte_smoke`、`wow_export_northshire_mvp_smoke`。

未归档为完成：

- 任何未审计 fileDataID。
- 任何暴雪素材。
- zlib/root/encoding 的生产解码。
- TACT key table。
