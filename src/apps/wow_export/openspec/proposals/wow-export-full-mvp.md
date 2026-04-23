# wowExport Full Feature + Northshire MVP Proposal

目标：在 `src/apps/wow_export` 用纯 Cheng 建立 wow.export 等价功能的生产路径，固定先支持本机 Retail `wow` `12.0.5.67165`，从本地 CASC 读取北郡 MVP 所需素材。

本次 apply 的最小闭环：

- 解析 `.build.info` 并严格锁定 product/version/build key。
- 解析 build config 的 root/install/encoding/build-name。
- 解析本地 CASC journal `.idx` 入口表。
- 解析 BLTE header、容器 MD5、block MD5、未压缩 normal block 和 Salsa20 加密块边界；缺 zlib 或缺 TACT key 直接失败。
- 解析 BLP2 header、M2 header、WDT/ADT/WMO chunk 边界骨架。
- 建北郡固定清单入口，但不编造 fileDataID。
- 建 CLI 命令面和三条 smoke。

未完成项继续保留为 apply 阻塞项：纯 Cheng inflate、TACT key table、encoding/root/install、WDC2-WDC5、完整 BLP DXT/BGRA 解码、M2/SKIN/ANIM、WDT/ADT/WMO 全解析、软件光栅窗口。
