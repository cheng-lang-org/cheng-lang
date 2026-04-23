# wowExport Progress

当前阶段只接受真实本机数据。缺少已审计 fileDataID 时，北郡预览必须失败，不能生成占位几何。

最新进展（2026-04-24）：

- Northshire 地形已从 WDT `MAID` + listfile 坐标收窄到四个真实基础 ADT：`Azeroth_31_48`、`Azeroth_32_48`、`Azeroth_31_49`、`Azeroth_32_49`；四条 fileDataID `777827/778027/777832/778032` 均已审计 content key、encoding key、archive、offset、encoded size。
- `asset_formats.ParseAdtTerrainSummary` 已严格解析 ADT `KNCM/MCNK -> TVCM/MCVT`，每个 `MCNK` 必须带一个 `MCVT`；四个 Northshire tile 实测 `1024` 个 terrain chunk、`148480` 个高度样本，高度范围 `32500..384127`。
- `preview-northshire` 现在会把 ADT terrain 纳入 scene 判定和摘要；本机输出 `auditedFiles=22 loadedFiles=22 adtTiles=4 adtChunks=1024 adtHeightSamples=148480 assetBytes=3346856`。
- `export-map --out-dir` 现在导出 `22` 个真实文件、合计 `3346856` 字节；四个 ADT 解码 payload 分别为 `375707/359854/322706/366237` 字节。
- `render-northshire` 现在先绘制真实 ADT terrain 高度点，再叠加 WMO group 和 M2 点云；`640x360` 输出仍为 `921618` 字节，绘制点数从 `31153` 提升到 `179633`。
- `casc_index.LoadLocalIndexEntry` 不再因为目录路径或悬挂 `ListDir` 结果崩溃；现在会稳定返回真实错误。
- `std/os.ListDir` 现已在库层修正：目录 listing raw buffer 会在 `ListDir` 内部释放，返回文件名保持 owned 字符串，不再把释放责任和悬挂风险留给业务层。
- 当前本机重跑最小链路时，阻塞已经从“路径桥/悬挂字符串”收敛到“本地 journal 查不到 config 里的 encoding encoding key `00f6dcef63eafe6254ab5408ea8baa09`”。
- 已新增纯 Cheng `tact_index` 解析器，现已用真实本机 `Data/indices/bd643d2dac9bfdc365890dafbbea83d6.index` 钉住现代 `.index` footer 和 24 字节 record 布局：`formatRevision=1`、`blockSizeKB=4`、`offsetBytes=4`、`sizeBytes=4`、`keyBytes=16`、`hashBytes=8`，首条 record 的 ekey/size/offset 也已经按实值解出。
- `wow_export_tact_index_smoke` 现在验证“固定本机 `.index` 样本 -> footer -> 首条 record -> `entryIndex=0` 回读同一条 record”，说明 `.index` 文件格式本身已经从猜测变成 Cheng 代码事实。
- 现在已经不是“`encoding/root/vfs-root` 在本机找不到”。新增的 `wow_export_tact_index_lookup_smoke` 已经证明这些关键 ekey 会真实落到 `Data/indices/303b4155efa35ae393a48c869f1d1899.index`，并且 `vfs-root` 的 block-key 链能继续追到终点 key `8bdfef615338a75d9199dd5a74913d14`。
- 已确认 `303b4155efa35ae393a48c869f1d1899.index` 来自本地 CDN config 的 `file-index` 字段，不是普通 data archive 索引；`encoding` 和 `vfs-root` 这类 config ekey 的 loose blob 应走 CDN `data/xx/yy/eKey` 路径。
- 已新增纯 Cheng `cdn_config` 模块读取本地 CDN config，并生成 `data/00/f6/00f6...`、`data/2a/ec/2aec...` 这类 loose data 路径和完整 CDN URL；终点 block key `8bdf...` 是 file-index 内部 block key，不是最终 loose object 名。
- 已新增纯 Cheng `cdn_fetch`，用 HTTP/1.1 over TCP 直连 CDN，流式写入 `.tmp`，校验 `status=200`、`Content-Length` 和期望大小后再原子 rename 到本地 cache；不会把 CDN blob 整体载入内存。
- `cdn_fetch.CdnDecodeCachedBlte` 已经能读回缓存的 CDN BLTE blob 并复用现有 BLTE/zlib 解码器；`vfs-root` 从 `34973` 字节 encoded blob 解成 build config 声明的 `55471` 字节 payload。
- 新增 `tvfs.TvfsParseSummary`，已经能严格解析 CDN `vfs-root` 的 `TVFS` header、ESpec、container table、path tree 和首个 `.root` 文件 span；本机 payload 为 `1178` 个 path node、`311` 个目录、`867` 个文件，`.root` 映射到 content key `4d538d779c8ce517fed1b8718bea2b79`。
- 新增 `tact_index` partial ekey 唯一补全查询；`wow_export_cdn_root_smoke` 现在直接用 CDN config 的 `file-index` 打开单个 `.index`，把 TVFS `.root` 的 `108aaa378a9a07d9a9` 补全为 `108aaa378a9a07d9a926150259ecbdad`，并拉取 root CDN BLTE。
- `cdn_fetch.CdnDecodeCachedBlteFirstBlock` 已经能只读缓存 BLTE header + 首块，解出 root 首个 `262144` 字节块并通过 TSFM root header/first entry 验证；避免把 49MB encoded root 和 65MB decoded root 同时常驻。
- `cdn_fetch.CdnDecodeCachedBlteRange` 已能只读 BLTE header + 目标 block，校验 block MD5 后只解码覆盖目标 decoded range 的块；不再为了查 root 条目全量解码 49MB encoded / 65MB decoded root。
- `cdn_root.CdnRootFindFileDataIDs` 已改成一趟批量 root lookup，按 decoded block window 扫 group，找到北郡 manifest 全部 fileDataID 后立即停止；不再每个 fileDataID 重复扫 root。
- 纯 Cheng zlib 热核已从“逐 bit 读 + Huffman 逐 symbol 线性扫”改成字节 bit-buffer、10-bit Huffman fast table、back-reference 非重叠分块复制/distance=1 填充；当前慢点不是 Cheng 运行时整体慢，而是之前解码器热路径粒度错误。
- 北郡预览不再为了 4 个已审计资产每次全量解码 root/encoding 扫表；新增 `AuditManifestPayloadsAgainstLocal`，只校验 manifest 身份、encoding key 的本地 `.index` 条目、archive/offset/size 和解码 payload MD5。
- `asset_formats.ParseM2GeometrySummary` 已解析真实 `MD21 -> MD20` M2 顶点数组，按 48 字节顶点 stride 读取 position，并用纯 Cheng IEEE754 fixed-point 解码算出模型包围盒跨度。
- `wow_export_tool_main preview-northshire --frames 1` 已编译并运行通过，本机输出 `scene=non-empty`、`wmoChunks=17`、`characterVertices=370`、`modelSpan1000=1916,5856,25208`，普通计时约 `2.15s`。
- CLI 可见摘要函数已避开嵌套 helper 插值导致的 seed materialize 限制；`wow_export_tool_main` 当前可重新编译。
- 新增 `asset_export` 模块和 CLI 真实导出路径：`extract-file` 支持已审计 Northshire `--label` / `--file-data-id`，`export-m2`、`export-wmo`、`export-map` 会写出 BLTE 解码后的真实 asset payload；输出存在时直接失败，不覆盖。
- `export-map --out-dir` 现在会导出 4 个 Northshire 审计资产：WDT `294988` 字节、WMO `9784` 字节、BLP `44876` 字节、M2 `20308` 字节，合计 `369956` 字节。
- `blp.Blp2DecodeFirstMipTga` 已实现 BLP2 DXT1/DXT3/DXT5 首 mip 到无压缩 32-bit TGA；`convert-blp` 现在支持默认 Northshire 贴图、`--label`、`--file-data-id` 和 `--in` 本地 BLP 输入。
- `asset_formats.ParseWdtSummary` 已解析 WDT `MAIN` 活跃 tile、`MODF` placement 和 `MAID` entry；`ParseWmoRootSummary` 已解析 WMO `MOHD` 材质、group、portal、light、model、doodad、doodad set 计数。
- `preview-northshire` 的 scene 判定已升级：必须有 WDT 活跃 tile、WMO group/material、M2 bounds、动画序列和 BLP->TGA 解码结果，不能只靠 header 非空。
- WDT/WMO/M2 依赖发现已接入 preview：WDT `MAID` 解析出 `9408` 个 referenced fileDataID，首项 `6173014`；WMO `GFID` 解析出 `13` 个 group fileDataID，首项 `107075`；WMO `MODI` 解析出 `17` 个非零 model fileDataID，首项 `198056`；M2 `SFID/TXID` 解析出 skin `494438` 和 texture fileDataID `127489/189598`。
- 已确认当前 Northshire manifest 4 条 root entry 在 root 中都是 ID-only，没有 name hash；因此不能靠路径 hash 严谨发现这些文件，下一步应从资产内嵌 `GFID/SFID/TXID/MAID/MODI` fileDataID 扩展审计 manifest。
- Northshire manifest 已扩展到 `18` 个真实审计资产：原 WDT/WMO/BLP/M2，加上 WMO root `GFID` 指向的 `13` 个 `NSabbey_000..012` group，以及 M2 `SFID` 指向的 skin `494438`。这些新增条目均用本地 root/encoding/index 审计出真实 content key、encoding key、archive、offset、encoded size。
- `asset_formats.ParseWmoGroupSummary` 已解析真实 WMO group 的 `MOGP(PGOM)` 内嵌 chunk，统计 `MOVT/TVOM` 顶点、`MOVI/IVOM` 索引、`MOBA/ABOM` batch 和 `MOPY/YPOM` 材质信息；13 个 group 合计 `29304` 顶点、`91689` 个索引。
- `asset_formats.ParseM2SkinSummary` 已解析真实 `.skin` header 和 index/triangle/property/submesh/texture-unit 数组边界；`abbey-bell-skin-00` 实测 `370` indices、`1302` triangle indices、`2` submeshes。
- `preview-northshire` 现在会读取并验证全部 18 个真实资产，scene 非空条件已升级为必须加载 WMO group 几何和 M2 skin/index 数据；本机输出 `wmoGroupAssets=13 wmoGroupVertices=29304 wmoGroupIndices=91689 m2SkinIndices=370 m2SkinTriangleIndices=1302`。
- `export-map --out-dir` 现在导出 18 个文件、合计 `1922352` 字节；新增的 WMO group 解码 payload 最大为 group 010 的 `290592` 字节，skin payload 为 `5088` 字节。
- 新增纯 Cheng `render` 模块和 CLI `render-northshire`：生成无压缩 32-bit TGA，主视图来自 13 个真实 WMO group `MOVT` 顶点，右上角 inset 来自真实 M2 顶点，skin 必须解析成功。`render-northshire --out /tmp/wow_export_northshire_render.tga --width 640 --height 360` 输出 `921618` 字节，绘制 `31153` 个真实几何点。

验证：

- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_asset_formats_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_asset_formats_smoke` 通过，`/tmp/wow_export_asset_formats_smoke` 通过。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_northshire_mvp_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_northshire_mvp_smoke` 通过，`/tmp/wow_export_northshire_mvp_smoke` 输出 `adtTiles=4 adtChunks=1024 adtHeightSamples=148480`。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_render_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_render_smoke` 通过，`/tmp/wow_export_render_smoke` 通过。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_asset_export_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_asset_export_smoke` 通过，`/tmp/wow_export_asset_export_smoke` 通过。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_memory_probe_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_memory_probe_smoke` 通过，`/tmp/wow_export_memory_probe_smoke` 通过；preview steady delta 当前为 `10470`。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/apps/wow_export/wow_export_tool_main.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_tool_main` 通过。
- `/tmp/wow_export_tool_main preview-northshire --frames 1` 通过，输出 `auditedFiles=22 loadedFiles=22 adtHeightSamples=148480 assetBytes=3346856`。
- `/tmp/wow_export_tool_main export-map --out-dir /tmp/wow_export_map_adt_current` 通过，输出 `exportedBundle files=22 bytes=3346856`。
- `/tmp/wow_export_tool_main render-northshire --out /tmp/wow_export_northshire_render_adt.tga --width 640 --height 360` 通过，输出 `adtHeightSamples=148480 pixels=179633`。
- `artifacts/backend_driver/cheng run-host-smokes wow_export_casc_smoke wow_export_md5_smoke wow_export_salsa20_smoke wow_export_blte_smoke wow_export_zlib_smoke wow_export_tact_keys_smoke wow_export_encoding_smoke` 通过。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/os_list_dir_stress_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/os_list_dir_stress_smoke && /usr/bin/time -l /tmp/os_list_dir_stress_smoke` 通过。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/os_list_dir_large_snapshot_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/os_list_dir_large_snapshot_smoke && /tmp/os_list_dir_large_snapshot_smoke` 通过。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/os_list_dir_negative_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/os_list_dir_negative_smoke && /tmp/os_list_dir_negative_smoke` 通过。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_tact_index_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_tact_index_smoke && /tmp/wow_export_tact_index_smoke` 通过。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_tact_index_lookup_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_tact_index_lookup_smoke && /usr/bin/time -l /tmp/wow_export_tact_index_lookup_smoke` 通过；本机观测 `real 5.04s`、`peak memory footprint 156631472`。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_cdn_config_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_cdn_config_smoke && /usr/bin/time -l /tmp/wow_export_cdn_config_smoke` 通过；本机观测 `real 0.66s`、`peak memory footprint 1130784`。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_cdn_fetch_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_cdn_fetch_smoke && /usr/bin/time -l /tmp/wow_export_cdn_fetch_smoke` 通过；本机观测 `real 2.12s`、`peak memory footprint 2113848`。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_tvfs_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_tvfs_smoke && /usr/bin/time -l /tmp/wow_export_tvfs_smoke` 通过；本机观测 `real 0.99s`、`peak memory footprint 4456760`。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_zlib_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_zlib_smoke` 通过，`/usr/bin/time -l /tmp/wow_export_zlib_smoke` 通过；本机观测 `real 0.65s`、`peak memory footprint 1179936`。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_cdn_root_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_cdn_root_smoke` 通过，`/usr/bin/time -l /tmp/wow_export_cdn_root_smoke` 通过；本机观测 `real 0.76s`、`peak memory footprint 5767480`。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_cdn_root_lookup_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_cdn_root_lookup_smoke` 通过；`/tmp/wow_export_cdn_root_lookup_smoke` 通过并输出 `wow_export_cdn_root_lookup_smoke ok`，普通运行约 9.45s，运行中 `ps` 采样 RSS `45632KB`。`/usr/bin/time -l` 包装该二进制时会无输出杀进程，未作为有效业务失败采信。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/apps/wow_export/wow_export_tool_main.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_tool_main` 通过。
- `/usr/bin/time -p /tmp/wow_export_tool_main preview-northshire --frames 1` 通过；本机观测 `real 2.15`。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_asset_formats_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_asset_formats_smoke` 通过，`/tmp/wow_export_asset_formats_smoke` 通过。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_northshire_mvp_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_northshire_mvp_smoke` 通过，`/tmp/wow_export_northshire_mvp_smoke` 通过。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_memory_probe_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_memory_probe_smoke` 通过，`/tmp/wow_export_memory_probe_smoke` 通过；preview steady delta 当前为 `479`。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_asset_export_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_asset_export_smoke` 通过，`/tmp/wow_export_asset_export_smoke` 通过。
- `/tmp/wow_export_tool_main extract-file --file-data-id 189599 --out /tmp/wow_export_cli_extract_file_189599.m2` 通过，导出 `abbey-bell-model` 解码后 M2 `20308` 字节。
- `/tmp/wow_export_tool_main export-map --out-dir /tmp/wow_export_cli_export_map_probe` 通过，导出 bundle `4` 文件、`369956` 字节。
- `CHENG_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/bootstrap/cheng.stage3 system-link-exec --root:/Users/lbcheng/cheng-lang --in:/Users/lbcheng/cheng-lang/src/tests/wow_export_blp_convert_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/wow_export_blp_convert_smoke` 通过，`/tmp/wow_export_blp_convert_smoke` 通过。
- `/tmp/wow_export_tool_main convert-blp --out /tmp/wow_export_cli_abbeyBell.tga` 通过，输出 `262162` 字节 TGA。
- `/tmp/wow_export_tool_main convert-blp --in /tmp/wow_export_cli_export_map_probe/abbey-bell-texture.blp --out /tmp/wow_export_cli_abbeyBell_in.tga` 通过，输出 `262162` 字节 TGA。
- 重新编译并运行 `wow_export_asset_formats_smoke` 通过，覆盖 WDT `MAIN/MAID`、WMO `MOHD/GFID/MODI`、M2 `SFID/TXID` 依赖解析。
- 重新编译并运行 `wow_export_northshire_mvp_smoke` 通过，输出 `wdtMaidFileIDs=9408`、`wmoGroupFileIDs=13`。
- 重新编译并运行 `wow_export_memory_probe_smoke` 通过；新增依赖解析后 preview steady delta 当前为 `506`。
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
- `Data/data/data.NNN` 的 30 字节本地 header 继续只解释传统 local archive；`.index` 这边的真实文件格式也已经钉住。下一步应该把 CDN root 从“首块验证”推进成按需 range/block 解码，而不是全量 65MB 解码后再扫表。
- `Data/indices` 大目录现在能稳定快照真实文件名，但 `os_list_dir_stress_smoke` 当前在 256 文件 * 6000 轮下的 `peak memory footprint` 约 `219005456`；这说明库层崩溃已修掉，长热路径的 allocator 回收表现还需要后续单独压。
- 北郡预览现在会先跑本地审计，再读取已审计资产；剩余缺口是把真实几何/动画数据继续推进成更完整的 MVP，而不是只验证头部和 chunk 边界。
- 当前 `extract-file`、`export-m2`、`export-wmo`、`export-map`、`convert-blp`、`preview-northshire`、`render-northshire` 已从占位推进到可用；剩余真正缺口是材质、光照、相机/世界摆放、更多 doodad/model 链路，而不是 ADT/WMO/M2 基础几何。
