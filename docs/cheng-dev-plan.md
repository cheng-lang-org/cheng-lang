# Cheng 自举开发文档（基于 Cheng 确定性内存管理）

## 最新进度（2025-12-28）

说明：对外阶段收敛为 Stage0/Stage1；本文中 “Stage1” 指 `src/stage1` 的自托管前端源码实现，同时也是最终编译器阶段。
说明（2026-01-26）：旧 IR 链路已移除；当前自举/构建走直出 C（`--mode:c`）与 `.deps.list`，运行时头文件统一 `runtime/include`。

- [x] Cheng Codex：`codex-cheng` 接入 Responses API 与提示词文件，扩展 app-server RPC/命令执行/审批请求流与文档进度。
- [x] Cheng Codex：CLI 对齐扩展（全局 `-c/--config` 覆盖、`--enable/--disable` 特性开关、features list、execpolicy、completion/sandbox/app-server schema 生成、review/resume 子命令）并补齐命令委派入口。
- [x] Cheng Codex：`cloud`/`apply`/`responses-api-proxy`/`stdio-to-uds` 改为 Cheng 本地实现（Cloud tasks exec/status/diff/apply + 列表兜底、auth.json 读取、task diff 直应用、stdio/HTTP 代理）。
- [x] Cheng Codex：execpolicy runtime 与 sandbox runner 对齐（规则解析/allowlist amendment、审批缓存、seatbelt/linux sandbox 执行与失败重试，shell login/timeout、shell snapshot 与 profile 读取/写入对齐）。
- [x] Cheng Codex：web_search 支持 `tools.web_search`/`web_search` 旧键与 `web_search_call` 解析回填（事件与上下文）。
- [x] Cheng Codex：review 工具对齐（仅禁用 web_search/view_image）并在 config/read 回填 tools 配置。
- [x] Cheng IDE Codex：线程列表/历史回填、流式消息、web_search/patch 状态显示，TODO CodeLens 交互。
- [x] 单态化修复：仅对可比较类型实例化 `__cheng_vec_contains`，避免结构体无 `==` 导致的编译错误。
- [x] IDE 开发文档：新增 `doc/cheng-ide-dev-plan.md`，记录目标/里程碑/当前状态与快捷键基线。
- [x] Web 前端方向规划：新增 `doc/cheng-web-frontend-plan.md`，定义 SFC/编译器/运行时/SSR/工具链路线（博采众长，不绑定生态兼容）。
- [x] Web 前端骨架与 ABI 草案：新增 `cheng/web/*` 目录骨架与 `doc/cheng-web-wasm-abi.md`。
- [x] Web 前端运行时骨架：新增 `cheng/web/runtime/abi.cheng` 与 `cheng/web/runtime/host_glue.js`（WASM import/JS glue stub）。
- [x] Web 前端 JS glue 加固：事件 payload JSON、fetch 请求描述与分段读取、DOM 环境检测。
- [x] Web 前端运行时核心：补齐 DOM root/query、host 导出解析、事件分发与 signals/app 基础模块。
- [x] Web 前端运行时扩展：新增 view/bindings 模块与事件字段回退解析。
- [x] Web 前端示例基线：新增 counter 示例与静态加载入口（`cheng/web/examples/*`）。
- [x] Web 前端最小编译器：新增 `.cwc` 解析、模板 AST 与 Cheng 代码生成、`webc` CLI。
- [x] Web 前端模板指令：补齐 `on:*`/`bind:value`/`bind:checked`/`class:*`/`style:*`/`if`/`each`（支持 `item, idx in list` + `else` 空分支）/`await`（`await:pending/await:catch`）的最小绑定生成。
- [x] Web 前端 async 运行时：新增 `AwaitText` 状态与 `awaitFetchText`。
- [x] Web 前端 scoped CSS：自动注入 `data-cwc-*` 属性并做基础选择器重写。
- [x] Cheng IDE 功能推进：已具备标签页、Explorer、语法高亮、查找/替换、跳转（行号/定义）、自动补全与基础快捷键（详见 `doc/cheng-ide-dev-plan.md`）。
- [x] IDE 工程扫描：实现 `walkDir`/`walkDirRec`/`execCmdEx` 并接入 Explorer 与项目索引的真实文件扫描。
- [x] IDE 语法高亮模块化：新增 `cheng-gui/services/syntax.cheng`，GUI 侧改为基于 token 渲染。
- [x] IDE 诊断面板：新增最小语法检查（括号/字符串未闭合）与右侧列表/行标记。
- [x] IDE Explorer 树：按目录分组生成树形列表，显示目录项与文件项。
- [x] IDE Explorer 折叠/展开：支持鼠标点击与键盘左右键折叠目录并保持选中可见。
- [x] IDE 脏文件提示：关闭时弹出确认（可保存/取消）。
- [x] IDE 自动缩进与行注释切换：Enter 继承缩进并支持 `Cmd/Ctrl+/` 快捷键。
- [x] IDE 面包屑/缩进参考线/状态栏增强/自动保存：显示路径与符号，提供缩进参考线与当前行高亮，状态栏新增诊断/分支/自动保存状态。
- [x] IDE 任务集成：终端支持 build/run/test 与 exec 命令（基于 `execCmdEx`）。
- [x] IDE 语言服务诊断：终端 `diag` 触发编译器诊断并回填面板（解析/语义）。
- [x] IDE 诊断自动化：编辑后空闲触发诊断（`CHENG_IDE_DIAG_AUTO` 可关闭）。
- [x] IDE 引用/重命名/格式化：终端 `refs`/`rename`/`fmt` 轻量能力。
- [x] IDE 任务日志面板：右侧 TASKS 显示任务与诊断日志。
- [x] IDE 项目级搜索：`Cmd/Ctrl+Shift+F` 或终端 `search`。
- [x] IDE 打包脚本：新增 `src/tooling/package_ide.sh`（生成分发目录与压缩包）。
- [x] IDE 终端集成 `package` 命令（`CHENG_IDE_PACKAGE_CMD` 可覆盖）。
- [x] IDE 符号跳转增强：新增文件/工作区符号跳转（`Cmd/Ctrl+Shift+O`、`Cmd/Ctrl+T`）。
- [x] IDE 命令面板：`Cmd/Ctrl+Shift+P` 复用终端命令执行。
- [x] IDE 快速打开增强：支持模糊匹配与 `path:line:col` 定位。
- [x] IDE VCS 面板：显示 Git 状态与 `diff` 命令（`Cmd/Ctrl+Shift+G` 刷新）。
- [x] IDE 主题切换：dark/light 主题（命令面板 `theme`，支持 `CHENG_IDE_THEME`）。
- [x] IDE 工程文件管理：命令面板/终端新增 `new/mkdir/rm/mv`，支持文件/目录创建/移动/删除并刷新 Explorer/VCS。
- [x] IDE 工作区持久化：启动恢复打开文件/活动文件/Explorer 折叠状态（写入 `build/ide/workspace_state.txt`）。
- [x] IDE 工作区配置与过滤：支持 `workspace_config.txt` 的 include/exclude 过滤规则。
- [x] IDE 多根工作区：支持 `CHENG_IDE_ROOTS` 指定多根目录，Explorer 顶层根标签与路径解析。
- [x] IDE 多根工作区：VCS/任务按活动文件或目标路径选择工作目录。
- [x] IDE 工作区设置/布局持久化：主题/自动保存/分屏状态写入 `workspace_state.txt`。
- [x] IDE 多行表达式续行缩进：运算符/逗号/点号触发增量缩进。
- [x] IDE 闭合括号自动对齐：行首 `)`/`]`/`}` 自动对齐到匹配缩进。
- [x] IDE 括号续行对齐：对齐到开括号列。
- [x] IDE Git diff/暂存/提交视图：`diff`/`stage`/`unstage`/`commit` 命令与 VCS 计数展示。
- [x] IDE Git 分支/冲突处理：新增 `branch`/`checkout`/`switch`/`conflicts` 命令并支持冲突文件列表。
- [x] IDE Git Blame：新增 `blame` 命令支持当前行/指定行查询。
- [x] IDE Git 历史：新增 `history/log` 命令支持当前文件/指定行查询。
- [x] IDE PTY 终端基础接入：新增 `shell` 命令，接入 PTY 输出轮询与行输入（macOS/Linux）。
- [x] IDE PTY 终端交互增强：ANSI 控制序列过滤、`\r`/`\b` 处理。
- [x] IDE PTY 终端逐字节输入：字符/方向键/回车/退格直通。
- [x] IDE 多终端标签：支持 `term` 命令新建/切换/关闭会话（命令切换）。
- [x] IDE 多终端标签 UI：支持底部点击切换会话。
- [x] IDE 多终端标签持久化：会话标签与活动会话恢复。
- [x] IDE 调试器后端基础接入：新增调试面板与命令驱动断点/调用栈/变量/Watch，并支持 run/continue/step/next/pause/stop（`CHENG_IDE_DEBUG_CMD`，支持 `{action}`/`{args}`/`{breakpoints}`/`{watches}`，占位符自动 shell 引号包裹）。
- [x] IDE 基本体验增强：语法高亮对齐关键字/数值/字符串字面量；补全支持前缀/子串/大小写匹配与内建类型；工作区符号索引支持块式定义，提升跳转/补全准确性。
- [x] IDE 工程符号索引缓存：基于文件时间戳/大小复用 `projectSymbols`，减少重复扫描。
- [x] IDE 语法高亮渲染缓存：行级 token 缓存，减少重复扫描。
- [x] IDE Outline 缓存：面包屑/Outline 复用缓存，避免每帧扫描。
- [x] IDE 折叠可见行缓存：滚动/定位复用可见行索引，减少折叠扫描。
- [x] IDE Quick Fix：新增 import 排序（文件级）。
- [x] IDE Quick Fix：新增缩进对齐/闭合括号对齐（`action`）。
- [x] IDE Quick Fix：新增连续空行折叠（文件级）。
- [x] IDE 键盘多光标：新增 `Cmd/Ctrl+Alt+↑/↓` 添加多光标行。
- [x] IDE 选区缩进/反缩进：多行选区支持 `Tab/Shift+Tab` 缩进与反缩进。
- [x] IDE 块选扩展：新增 `Alt/Option+Shift+↑/↓` 快速列选扩展。
- [x] IDE 跨行选择合并：行号栏点击/拖选合并为连续多行选区。
- [x] IDE 行操作快捷键：`Alt/Option+↑/↓` 移动行，`Cmd/Ctrl+Shift+D` 复制行/选区，`Cmd/Ctrl+Shift+K` 删除行/选区，`Cmd/Ctrl+L` 整行选择。
- [x] IDE 选区扩展/收缩：新增 `Alt/Option+Shift+→/←` 快捷键与缩放逻辑。
- [x] IDE 轻量格式化器增强：基于缩进/注释感知的基础格式化（缩进规整 + 去尾随空白）。
- [x] IDE 语义缩进与自动配对扩展：`except/finally` 对齐、括号换行缩进。
- [x] IDE 代码折叠：支持缩进块折叠与行号栏 Alt/Option+点击切换，折叠行占位提示。
- [x] IDE 迷你地图：右侧概览栏显示文件分布并支持点击定位。
- [x] IDE 分屏（上下）：支持上下分屏视图与独立滚动。
- [x] IDE 悬浮提示：鼠标悬停显示诊断/局部符号信息（轻量实现）。
- [x] IDE 签名提示：当前文件/单行的轻量签名提示。
- [x] IDE 语义补全：命名参数提示（当前文件/工作区定义，补全 `param =`）。
- [x] IDE UI 布局对齐：Tabs 与编辑区对齐、行号右对齐、面板间距优化。
- [x] IDE 左侧列表 hover 高亮：Explorer/Search/SCM/Run 交互高亮，提升可用性。
- [x] IDE 右侧/底部列表 hover 高亮：Outline/Problems/VCS/Tasks/Output 交互高亮。
- [x] IDE 右侧/底部 Tabs hover 高亮：提升面板切换的可感知性。
- [x] IDE 中文渲染修复：macOS CoreText 字体回退 + 非 ASCII 行高亮降级。
- [x] IDE 开发文档更新：补齐现代化 IDE 功能清单（除断点调试）、生产级缺口与快捷键基线（见 `doc/cheng-ide-dev-plan.md`）。
- [x] IDE 选区/剪贴板/撤销：支持 Shift 选区、内置剪贴板、Undo/Redo（基础）。
- [x] IDE 鼠标拖选/系统剪贴板：支持拖选，基础系统剪贴板命令对接（可用环境变量覆盖）。
- [x] IDE 多光标/块选：支持 Alt/Option+点击/拖选的多光标列选。
- [x] IDE 自动配对与语义缩进规则：括号/引号配对，`case/type/object/enum` 等行触发缩进，`else/elif/of` 自动对齐。
- [x] IDE 本地语义索引：局部变量/参数/for 模式的补全与跳转增强。
- [x] IDE 基本体验增强：高亮字面量/关键字对齐，补全匹配提升，工作区符号索引支持块式定义。
- [x] IDE 输入法组合与退格/文本输入稳定性修复，补齐编辑器按键处理与回退逻辑。
- [x] IDE 修饰键判断修复（Shift 组合键符号输入）。
- [x] IDE 按词移动光标（macOS Option+←/→，Windows/Linux Ctrl+←/→）。
- [x] IDE 项目编译/运行命令：新增 `cheng build/run`，调用 `src/tooling/chengc.sh`。
- [x] IDE 移动端导出/构建命令：新增 `mobile export/android/ios`，调用 `build_mobile_export.sh` 与 `mobile_ci_*`。
- [x] IDE 跳转到定义增强：支持 `Cmd/Ctrl+Click`。
- [x] IDE 自动补全增强：输入时自动触发补全。
- [x] IDE 全选快捷键：新增 `Cmd/Ctrl+A` 覆盖编辑器全选。
- [x] IDE 面板分隔条拖拽调整：支持 Explorer/右侧面板/终端的可拖拽调整并持久化。
- [x] IDE 窗口缩放体验：禁用内容缩放拉伸，缩放时保持字体尺寸稳定。
- [x] Cheng Mobile：新增移动端开发文档（`doc/cheng-mobile-dev-plan.md`），落地移动运行时与 App 生命周期接口（`cheng/runtime/mobile*.cheng`）。
- [x] Cheng Mobile：提供移动端导出脚本（`src/tooling/build_mobile_export.sh`）与示例（`examples/mobile_hello.cheng`）。
- [x] Cheng Mobile：新增 Host ABI 桥接头与 stub（`~/.cheng-packages/cheng-mobile/bridge/cheng_mobile_bridge.h`）。
- [x] Cheng Mobile：新增 Android/iOS 模板说明（`~/.cheng-packages/cheng-mobile/android/README.md`、`~/.cheng-packages/cheng-mobile/ios/README.md`）。
- [x] Cheng Mobile：新增 Host core 队列与 Android/iOS skeleton（`~/.cheng-packages/cheng-mobile/bridge/cheng_mobile_host_core.c`、`~/.cheng-packages/cheng-mobile/*/cheng_mobile_host_*`）。
- [x] Cheng Mobile：新增 Host 示例与导出桥接（`examples/mobile_host_hello.cheng`，`build_mobile_export.sh --with-bridge`）。
- [x] Cheng Mobile：新增 Host 事件注入辅助（`~/.cheng-packages/cheng-mobile/bridge/cheng_mobile_host_api.*`）。
- [x] Cheng Mobile：新增 Android JNI / iOS glue 事件桥接（`~/.cheng-packages/cheng-mobile/android/cheng_mobile_android_jni.c`、`~/.cheng-packages/cheng-mobile/ios/cheng_mobile_ios_glue.*`）。
- [x] Cheng Mobile：新增 Android Kotlin 最小接入与 iOS UIKit ViewController（`~/.cheng-packages/cheng-mobile/android/Cheng*.kt`、`~/.cheng-packages/cheng-mobile/ios/ChengViewController.*`）。
- [x] Cheng Mobile：新增 Android/iOS 最小像素呈现（Surface/Layer blit）。
- [x] Cheng Mobile：新增 Android/iOS 工程模板（`~/.cheng-packages/cheng-mobile/android/project_template`、`~/.cheng-packages/cheng-mobile/ios/project_template`）。
- [x] Cheng Mobile：Host 示例新增像素缓冲呈现（`examples/mobile_host_hello.cheng`）。
- [x] Cheng Mobile：Android Gradle Wrapper + iOS XcodeGen 生成脚本（`~/.cheng-packages/cheng-mobile/android/project_template/gradlew`、`~/.cheng-packages/cheng-mobile/ios/project_template/project.yml`）。
- [x] Cheng Mobile：Android/iOS 文本输入桥接（IME/UIKeyInput）。
- [x] Cheng Mobile：Android NativeActivity NDK glue（生命周期/输入/渲染 tick；`cheng_mobile_android_ndk.c`）。
- [x] Cheng Mobile：Android OpenGL ES 渲染通道（EGL + 纹理上传；`cheng_mobile_android_gl.c`）。
- [x] Cheng Mobile：iOS Metal 渲染通道（CAMetalLayer + BGRA 上传）与 UIKit 生命周期完善。
- [x] Cheng Mobile：资源打包与 manifest 生成（`build_mobile_export.sh --assets`）。
- [x] Cheng Mobile：资源热更新辅助（`cheng/runtime/mobile_assets.cheng`）。
- [x] Cheng Mobile：Android/iOS 模板 CI 脚本（`src/tooling/mobile_ci_android.sh`、`src/tooling/mobile_ci_ios.sh`）。
- [x] Cheng Mobile：默认资源根目录支持（`cheng_mobile_host_default_resource_root` + Android 资产提取）。
- [x] 跑通直出 C 自举：Stage0c 直出 C 生成 `/tmp/stage1_runner.c`，编译并链接出 `stage1_runner`（可用 `src/tooling/update_seed_stage0c.sh` 更新 seed）。
- [x] Stage1 编译器 CLI：`./stage1_runner --mode:c --file:<in.cheng> --out:<out.c>` 可直接编译 `.cheng -> .c`（用于直出 C 工具链入口）。
- [x] 工具链改造起步：重写 `src/stage1/frontend_{lib,bootstrap,stage1}.cheng` 以恢复可维护结构；新增 `CHENG_STAGE1_USE_SEED`（显式 seed 自举）与内部编译 trace 开关（`CHENG_STAGE1_TRACE`/`CHENG_STAGE1_TRACE_FLUSH`）。
- [x] Stage1 内部编译热点诊断：`CHENG_STAGE1_TRACE_EMIT` 统计 emit 语句处理速率；type/procSig/typeDef/tuple/value 改用哈希映射；避免 per-stmt getenv。
- [x] Stage1 emit 进度提示与加速：trace 输出 processed/total/pct；seed `stage1_runner` 支持 `CHENG_STAGE1_CFLAGS`；调用签名匹配加入 posCount 快路径。
- [x] Stage1 内部编译 profiling：`CHENG_STAGE1_PROFILE=1` 输出 lex/load/sem/mono/ownership/emit 阶段耗时。
- [x] Stage0->bootstrap 提速：默认 `CHENG_STAGE1_SKIP_SEM=1` 跳过语义检查，可显式设为 0 恢复。
- [x] Stage0->bootstrap 内存加速：仅保留 ORC/ownership 路径，不再提供额外优化开关。
- [x] Stage1 AST 类型推断/managed payload 缓存：减少 `inferExprType`/`typeHasManagedPayload` 重复遍历，缓解 internal compile 长耗时。
- [x] 纯 Cheng 自举脚本：`src/tooling/bootstrap_pure.sh --fullspec` 不调用旧 Stage0（以 `stage1_runner` 为种子；若缺少则从 `src/stage1/stage1_runner.seed.c` 构建种子后闭环）。
- [x] Cheng 语言实现的原生 GUI IDE（macOS Cocoa）：入口 `cheng-ide/main.cheng`（启动：在 `cheng-ide` 根目录运行 `./build/main_local gui run`；支持 `--headless`）。Web 版本保留为 legacy（已拆分）。
- [x] Stage1 parser/lexer/codegen 已满足自举所需语法：`if/elif/else`、`when`、`defer`、`case/of/else`、`while/for`（range 形式 `..`/`..<`，且 `continue` 语义正确；并支持对已知 `str`/`seq[T]`/`Table[V]` 的最小迭代降级）、`T*`/`ref`/`var` 类型、以及 `@importc`/`seq[...]` 单态化调用等。
- [x] Stage1 语句分隔符：支持 `;` 作为 statement separator（顶层与单行 suite），确保 Stage1 自举时不会因 `a(); b();` 产生伪语法节点。
- [x] 旧 IR 覆盖扩展（已移除）：新增表达式级 `if/when/case`、`@[...]` 序列字面量与 `for ... in ...: expr` 推导式（通过 `lowerForNifc` 降级为语句级结构）。
- [x] trait/concept 约束落地（Stage1）：在泛型实例化时检查 required routines 的完整签名（参数个数/类型/返回类型，含 `Self` -> 具体类型；并支持 trait/concept 自身带泛型参数，如 `HasZero[int32]`），fullspec 增加覆盖断言。
- [x] 切片语义落地（Stage1）：支持 `str`/`seq[T]` 的 `x[a .. b]` 与 `x[a ..< b]`（降级为 `__cheng_slice_string` / `__cheng_slice_vec[T]`，内部实现为 `alloc+copyMem` / `newSeq+addPtr`），fullspec 增加断言覆盖。
- [x] tuple 一等值落地（Stage1）：支持 `tuple[...]` 类型与 `(...)` 字面量；在旧 IR 输出前执行 tuple type lifting，生成 `Tuple_*` 名义类型并用 `f<idx>` 字段承载元素。
- [x] named tuple element（Stage1）：tuple 类型/字面量支持 `name: ...` 元素写法（例如 `tuple[a: int64]`、`(a: 1)`），解析后忽略名称，字段仍按位置（`f<idx>`）承载。
- [x] tuple 解构类型推导增强（Stage1）：当 RHS 为“已知局部 tuple 变量”且为简单引用表达式时，允许省略 `: tuple[...]` 类型注解，直接从 RHS 的局部类型推导元素类型。
- [x] set[T] 最小落地（Stage1）：`set[T]` 类型映射为 `uint64` 位集；`{...}` 字面量（含 `..`/`..<` range 元素）在 lowering 中降级；`in/notin` 对 set 走位测试语义。
- [x] Result/? 最小落地（Stage1）：新增 `src/stdlib/bootstrap/core/result.cheng`；`expr?` 在返回同类型 `Result[T]` 的例程内 early-return 传播错误，否则 `panic(res.err)`；fullspec 增加断言覆盖。
- [x] formal spec 草案项落地（Stage1）：`where`（解析+保存，并在泛型实例化时做最小求值：`true/false`、`and/or/not`、以及类型相等 `T is int32`）/`async`（解析+保存）、命名实参重排与默认参数填充、`case of ... if ...:` guard（降级为 if 链）、`in/notin`（range + `seq[T]`/`str`(char/子串)/`Table[V]`(str key) 的最小语义降级）、以及 `<<` `>>` `&` `|` `^` `~` 位运算（见 `examples/stage1_codegen_fullspec.cheng`）。
- [x] 数值字面量对齐（Stage1）：支持 `0x/0b/0o`、`_` 分隔符与 `float` 字面量输出，fullspec 覆盖断言通过。
- [x] 自举性能修复：`src/stdlib/bootstrap/os.cheng:readAll` 改为 chunk 读取 + `realloc`，避免读取源码时的 O(n^2) 内存暴涨。
- [x] 规范对齐：`doc/cheng-formal-spec.md` 与词法实现对齐（补齐 `nil` 关键字、`@`/`..<' 符号；修复表格中的 `<code>&#124;</code>` 渲染问题），自举 hash 仍保持稳定。
- [x] 导出规则切换：移除后缀 `*` 导出语法，采用“首字母大写导出”的规则（并清理 Stage1/最小标准库中的 `*` 标记），自举闭环与确定性验收继续通过。
- [x] pattern 扩展：`let/var/const` 支持 literal pattern（匹配断言，不引入新绑定，不匹配时 panic）与 tuple 解构（右侧为 tuple 字面量/简单引用/可推断返回类型的例程调用），`case/of` 支持 wildcard（`of _:`）与 tuple pattern（降级为 if 链，且 selector 会被物化以保证副作用只发生一次），fullspec 增加断言覆盖。
- [x] pattern/in-notin/语义扩展（Stage1）：支持 range/seq/object/set pattern（含嵌套），`case` 直接匹配结构化 pattern；`in/notin` 支持 tuple/seq/bracket 字面量展开。
- [x] 旧 IR 全局变量（已移除）：顶层 `let/var` 输出为 `gvar`（避免被当作局部变量而丢失符号），fullspec 增加覆盖断言。
- [x] 例程字面量落地（Stage1）：支持 `fn/iterator` 字面量作为表达式；lowering 会把其提升到模块顶层并返回函数指针（旧 IR `proctype`），fullspec 增加覆盖断言（含命名递归）。
- [x] 泛型例程字面量实例化（Stage1）：支持 `(fn[T](...)=...)[int64]` 显式实例化，以及在 `let/var/const` 具备 `fn(...)` 类型注解时按期望类型做最小推导实例化。
- [x] Stage1 结构化诊断（最小落地）：`lexer/parser` 支持输出 `Diagnostic{severity, filename, line, col, message}`；`frontend_bootstrap` 在检测到 `svError` 时打印消息并中止。
- [x] 工具链骨架：Stage0 `bin/cheng` 支持 `--out:` 写文件；`src/tooling/chengc.sh` 生成直出 C 构建图（默认 `./stage1_runner`，无 stage0 回退），支持增量/并行（`--jobs:`）。
- [x] 模块系统（Stage1 最小落地）：`import` 会按出现顺序 DFS 递归加载模块（仅单模块导入 + `as`，group import 已移除）；语义阶段对 `import` 只导入“首字母大写”的导出符号；`alias.Symbol` 的模块限定名会在前端被降级为普通 `Symbol`，避免被后端误判为方法调用/字段访问（当前仍属于“单编译单元”模型）。
- [x] 移除 `from ... import ...` 语法糖：统一使用单行 `import`。
- [x] 泛型/模板最小落地（Stage1）：新增 `monomorphize` pass（仅支持显式实例化 `Foo[int64]`）；template 支持表达式体展开，且支持多语句体在“语句位置”展开并拼接到调用点；补齐对象构造 `Type(field: value)` lowering 与“单表达式 fn 体隐式 return”，`examples/stage1_codegen_fullspec.cheng` 覆盖通过。
- [x] 默认类型参数（Stage1）：`typeParam = typeExpr` 支持尾部缺省；实例化 `Pair[str]` 会补齐默认实参并参与实例名 mangling，确保与 `Pair[str, int64]` 一致，fullspec 增加断言覆盖。

## 目标与范围

- 基于现有编译管线实现 Cheng 语言：前端语法/语义/借用与后端生成接入任务图、增量/并行编译与 ARC/ORC 确定性内存管理（默认 ORC，循环引用按最佳实践优先显式打断，必要时再接入可选 cycle collector）。
- 提供 Stage0（最小引导前端，用于自举）、Stage1（Cheng 自托管主编译器，源码位于 `src/stage1`）与最小标准库，诊断/工具链保持中文输出与确定性。
- 近期目标：将现有 Cheng 前端骨架迁移为工具链插件/语言模式，跑通 lex/parse/sem 到 IR 的降级占位，使其能在并行/增量框架下编译样例。

## 目录结构（调整后）

- `cheng/`
  - `stage0c/`：宿主层引导前端（C 实现），用于生成自托管前端 runner 的直出 C（`bin/cheng`）。
  - `stage1/`：自托管前端源码（lexer/parser/codegen），用于内部自举生成 `stage1_runner`。
  - `stdlib/bootstrap/`：自举最小标准库（仓库根下为 `src/stdlib/bootstrap/`）。
  - `tooling/`：工具封装（`bootstrap.sh` 完全自举、`bootstrap_pure.sh` 纯 Cheng 自举、`chengc.sh` 直出 C 构建图入口等）。

## Stage0 C 等价替换（完全替换 旧前端）

目标：用 C 完整实现 Stage0，核心位于 `cheng/stage0c`，覆盖 lexer/parser/semantics/monomorphize/cache/import/parallel 逻辑；旧 IR 写出路径已移除。并保持 CLI/输出/依赖生成的等价性；语义对齐“默认 move + `var` 可变借用 + `share(x)` 共享”与 ORC 的确定性内存管理规则。

当前落地：
- 新增 C stage0 驱动 `src/stage0c/frontend_runner.c` 与构建脚本 `src/tooling/build_stage0c.sh`，支持 `--mode:c/--mode:deps/--mode:deps-list/--mode:sem/--mode:asm/--mode:hrt` 并通过 `stage1_runner` 编译（缺失时会从 seed 构建）。
- `src/stage0c/core` 已覆盖 bootstrap + stage1 frontend 的 `--mode:c`，并设为默认路径（`CHENG_STAGE0C_CORE` 默认 1，`CHENG_STAGE0C_CORE_ALLOW_FALLBACK` 默认 0；显式设 0 才会回退旧路径）。
- `src/tooling/bootstrap.sh` 默认构建并使用 C stage0（`bin/cheng`，同时生成 `bin/cheng_c` 兼容别名）。
- `bootstrap.sh` 使用 Stage1 内部编译生成 `stage1_runner`，不再依赖旧 Stage0 或旧 IR 工具。

生产闭环验收：
- `src/tooling/build_stage0c.sh` 可构建 `bin/cheng`/`bin/cheng_c`。
- `src/tooling/bootstrap.sh --fullspec` 默认走 C stage0（纯 core，无 fallback），闭环与 determinism 校验通过。
- `CHENG_STAGE0C_CORE=1 CHENG_STAGE0C_CORE_ALLOW_FALLBACK=0 ./src/tooling/bootstrap.sh --fullspec` 可显式验证纯 C 内核闭环。
- `src/tooling/bootstrap_pure.sh --fullspec` 不依赖旧 Stage0，可稳定自举。
- `src/tooling/update_seed_stage0c.sh` 可在纯 C 依赖链内更新 seed。
- `src/stage0c/core` 覆盖 `--mode:c/--mode:deps/--mode:deps-list`；`--mode:sem` 等仍由 `stage1_runner` 处理。

## Stage0 全语法一次性替换（直接切换方案）

目标：将完整语法/语义/泛型/所有权/模块导入/代码生成一次性迁入 `src/stage0c/core`，直接生成 C 与 ASM，Stage1 仅保留为回退与对照基线；最终默认路径不再调用 Stage1。

前置约束：
- 语言规范冻结一个迭代周期（避免边迁移边新增语法）。
- `stage1_runner` 作为对照基线固定版本，禁止无关变更。
- 回退开关明确：`CHENG_STAGE0C_CORE_ALLOW_FALLBACK=1` 仅用于紧急回退。

一次性迁移清单（必须同步落地）：
1) 语法/语义完整覆盖（按 `docs/cheng-formal-spec.md`）
   - 类型系统、泛型、trait/concept、模块导入与别名、所有权/ORC 规则。
2) AST 降级与 C 生成完整覆盖
   - 与 Stage1 输出结构一致（便于 diff 与 determinism 验收）。
3) 诊断对齐
   - 错误类型/错误码/位置/文本与 Stage1 对齐（允许可解释差异列表）。
4) 任务图与缓存键对齐
   - `--mode:deps` 输出解析路径（`CHENG_DEPS_RESOLVE=1`）并覆盖模块依赖。
   - C/ASM 生成的缓存键包含：编译器版本、`CHENG_MM`、目标平台/ABI、规范版本、依赖锁信息。
5) Stage0 直接 ASM 完整覆盖
   - 语法全量可落地 `.s`（直接 Cheng->S，移除旧 IR 汇编链路）。
   - `examples/stage1_codegen_fullspec.cheng` 可通过 `chengc.sh --emit-asm` 产出 `.s`（全语法闭环）。

验收闭环（一次性切换前必须全部通过）：
- `src/tooling/bootstrap.sh --fullspec` 与 `src/tooling/bootstrap_pure.sh --fullspec` 全链路通过。
- `src/tooling/bootstrap_stage0c_core.sh --fullspec` 与 `--asm` 通过。
- `CHENG_ASM_FULLSPEC=1 sh src/tooling/verify_asm_backend.sh` 通过。
- Stage0 与 Stage1 的 `.c` 输出在核心样例上 hash 一致（或差异白名单可解释）。
- 诊断一致性回归：错误位置与错误文本对齐（允许少量标注差异）。
- 性能基线：同输入集不低于 Stage1 基线（时延/内存峰值）。

回退策略（必须提前实现）：
- 保留 `stage1_runner` 与 `CHENG_STAGE0C_CORE_ALLOW_FALLBACK`。
- CI 默认禁用回退，出现阻断问题仅允许紧急分支开启回退。

## 纯 C 编译器内核替换路线（生产级闭环）

目标：在不依赖旧前端的前提下，用 C 实现 Cheng 编译器内核（lexer/parser/语义/单态化/ownership/C 生成），并以系统 C 编译器完成自举闭环与确定性校验。

### 1. 交付边界

- 纯 C 前端内核：`src/stage0c/core/*`（lexer/parser/ast/sem/monomorphize/ownership/emit C）。
- 工具链仅依赖 C：`bin/cheng`（stage0c 内核）+ 系统 C 编译器，不再调用旧版 Stage0。
- 旧 IR 已移除，直出 C 作为唯一中间层。

### 2. 里程碑与闭环点

M0 语法冻结  
- 基于 `doc/cheng-formal-spec.md` 固化“最终语法”，移除兼容分支。
- 标准库与 stage1 源码一次性对齐（不再保留旧语法）。

M1 C AST + Lexer/Parser  
- C 版 parser 覆盖 `src/stage1/*.cheng` 与 `src/stdlib/bootstrap/**` 全量解析。
- 产出与 Stage1 parser 等价的 AST（NodeKind/结构/位置信息一致）。

M2 C 语义最小集  
- 仅支持自举所需的符号解析/类型推断（与 Stage1 实现等价）。
- `import/as`、类型别名、命名实参/默认参数、`type T =`/object 构造等可用。

M3 C 单态化/模板展开  
- 泛型类型/例程实例化与实例命名规则与 Stage1 一致。
- `[]`/`[]=` 操作符按容器类型实例化，避免与 `str` 冲突。

M4 C ownership/ORC 插桩  
- 按当前 Stage1 规则插桩 `retain/release`，保持确定性释放语义。
- 逐步补齐 `share(x)`、`var` 借用等语义入口（与文档一致）。

M5 C 生成  
- 生成的 `.c` 与 Stage1 输出对齐（语义等价 + 确定性稳定）。
- 系统 C 编译器可编译生成 `stage1_bootstrap_runner`/`stage1_runner`。

M6 纯 C 自举闭环  
- `./src/tooling/bootstrap.sh --fullspec` 通过（纯 C 路径）。
- `stage1_runner` 再生成 `stage1_runner.c`，hash 不变。
- CI 仅保留 C 依赖链，不再调用旧工具。

生产闭环验收：
- `src/tooling/bootstrap_stage0c_core.sh --fullspec` 全链路通过（纯 C 依赖链）。
- `src/tooling/update_seed_stage0c.sh` 可在无旧环境下更新 `stage1_runner.seed.c`。
- `src/tooling/bootstrap.sh --fullspec` determinism 校验稳定。

### 2.1 核心内核完全替代（不依赖 stage1_runner）

当前状态：
- `src/stage0c/core` 已具备 lexer/parser/ast/diagnostics/import deps。
- `src/stage0c/core` 新增语义最小骨架（内建类型/符号表/重复定义/未定义诊断），支持 `--mode:sem` 自检。
- `src/stage0c/core` 新增 C 最小 emit（仅覆盖 `fn main` + `echo("...")`），用于验证纯 C 端到端输出。
- `src/stage0c/core` 扩展到 `var/if/for/continue/assign/return`，`examples/stage1_codegen_vars.cheng` 在 `CHENG_STAGE0C_CORE_ALLOW_FALLBACK=0` 下可生成 `.c`，且可由系统 C 编译器编译通过。
- `src/stage0c/core` 进一步覆盖 `when/break/defer/assert`（语义仍简化），`examples/stage1_codegen_control.cheng` 在纯 C 模式可生成 `.c` 并编译通过。
- `src/stage0c/core` 可在无 fallback 下直接编译 `src/stage1/frontend_stage1.cheng` 输出 `.c`（例如 `/tmp/core_stage1.c`）。
- `src/stage0c/core` 支持 `@[]`/推导式（仅 `seq[int32]` 常量路径），`examples/stage1_codegen_expr.cheng` 在纯 C 模式可生成 `.c` 并编译通过。
- `src/stage0c/core` 支持 `while`/`not`/`and`/`or`、`T(x)` 强转、`T*` 指针类型，`frontend_stage1.cheng` 可在纯 C 模式生成 `.c` 并完成代码生成（链接仍缺失导入模块函数）。

目标状态：
- `src/stage0c/core` 独立完成 `--mode:c` 全链路（语义/单态化/ownership/C 生成），不再调用 `stage1_runner`。
- `bin/cheng` 在 `CHENG_STAGE0C_CORE=1` 且禁用回退时仍可编译任意 `.cheng` 输入。

实施拆解（核心路径）：
1) AST/Token/Type 对齐  
   - 对齐 Stage1 `NodeKind/TokenKind` 与关键字段（位置、字面量、泛型参数）。
2) 语义最小集  
   - 符号/作用域/类型推断：覆盖自举所需语义。  
   - `import/as`、类型别名、命名实参/默认参数、`type T =`/object 构造可用。  
3) 单态化/模板展开  
   - 泛型类型/例程实例化规则与 Stage1 一致。  
4) ownership/ORC 插桩  
   - 对齐 Stage1 的 retain/release 插桩与 `share(x)`/`var` 借用入口。  
5) C 生成  
   - `.c` 输出结构与 Stage1 等价；系统 C 编译器可编译通过。  

验收（完成后）：
- `CHENG_STAGE0C_CORE=1 CHENG_STAGE0C_CORE_ALLOW_FALLBACK=0 bin/cheng --mode:c --file:examples/stage1_codegen_hello.cheng --out:/tmp/core_hello.c` 成功，且不调用 `stage1_runner`。
- `CHENG_STAGE0C_CORE=1 CHENG_STAGE0C_CORE_ALLOW_FALLBACK=0 ./src/tooling/bootstrap.sh --fullspec` 全链路通过。

### 3. 实施拆解（工程路径）

1) 建立 C 内核目录  
   - `src/stage0c/core/{lexer,parser,ast,sem,mono,ownership}.c/.h`（旧 IR 写出路径已移除）  
   - 与 Stage1 的 NodeKind/TokenKind 对齐，确保 AST 结构兼容。

2) 改造 stage0c 驱动  
   - `frontend_runner.c` 从“调用 stage1_runner”切换为“调用 C 内核”。  
   - 保持 CLI：`--mode:c/--mode:deps/--out`。

3) 迁移/重写关键逻辑  
   - `monomorphize`/`emit`/`ownership` 的最小等价实现，先满足自举。
   - 允许功能缺口，但必须保证 `frontend_bootstrap.cheng` 可编译。

4) Seed 生成与冻结  
   - 用现有 Stage1 生成一次 `src/stage1/stage1_runner.seed.c`，随后冻结。
   - C 内核应能从 seed 编译出 `stage1_runner`，并完成 determinism 校验。

5) CI/验收脚本  
   - 新增 `src/tooling/bootstrap_stage0c_core.sh`（只走 C 依赖链）。  
   - 验收项：fullspec + determinism + 典型样例编译时间基线。

### 4. 风险与对策

- 语义漂移：以 Stage1 输出为“黄金参考”，新增对比用例（C diff + hash）。
- 泛型命名冲突：统一 `mangleInstanceName` 规则（含操作符）。
- 性能回退：按阶段加 profile，先保正确性再逐步优化。

- CLI 模式对齐：`--mode:deps/--mode:deps-list/--mode:sem/--mode:c` 行为与 Stage1 对齐，`--out` 输出格式与错误诊断一致。
- 依赖生成对齐：通过 `bin/cheng --mode:deps-list` 产出与现有 Stage1 一致的 `.deps.list`（可配 `CHENG_DEPS_RESOLVE=1` 输出解析路径）。
- 自举确定性：`src/tooling/bootstrap.sh --fullspec` 与 `bootstrap_pure.sh --fullspec` 均通过，且生成物 hash 稳定。
- 性能基线：与旧 Stage0 在同样输入集下的时间/内存峰值可控，不产生新的 O(n^2) 增长。

实现拆分（模块映射）：
- Stage0 旧模块（lexer/parser/semantics/borrow_check/monomorphize/frontend_runner）已迁移到 `src/stage0c/*.c`。

## 编译期借用证明闭环（默认 move + var + share）

目标：在“默认 move + var 可变借用 + share(x) 共享”的内存管理模型下，提供可编译期证明的借用逃逸规则，Stage0/Stage1 行为与诊断对齐。

当前落地：
- Stage1：`var` 借用不可 return/share/赋给全局/传给非 var 形参；允许同一作用域内 reborrow，诊断 `E_VAR_BORROW_ESCAPE`。
- Stage1：借用活跃期内禁止对借用源读取/写入/再次借用，诊断 `E_VAR_BORROW_ORIGIN_USE/WRITE`。
- Stage0：对齐 `var` 借用逃逸检查与诊断（return/share/global assign/非 var 形参），用于 bootstrap 早期拦截。
- Stage0：对齐借用源读/写/再借用规则，避免 reborrow 期间的越界使用。
- 回归样例：`examples/diagnostics_borrow_escape_*.cheng` + `examples/diagnostics_borrow_origin_*.cheng` + `scripts/verify_var_borrow_diag.sh` 覆盖。

生产闭环验收：
- `scripts/verify_var_borrow_diag.sh` 全通过；`bootstrap.sh --fullspec` 与 `bootstrap_pure.sh --fullspec` 可稳定自举。
- Stage0/Stage1 在同一输入上诊断一致（错误位置/原因一致）。
- 借用白名单调用（如 void*/FFI 边界）不产生误报。

## 多线程内存安全闭环（Arc/Send/Sync）

目标：默认 ORC 维持非原子路径；跨线程显式 `share_mt/Arc`；编译期 `Send/Sync` 约束阻断 use-after-free 与数据竞争。

当前状态：
- 标准库已落地：`std/sync` 提供 `Arc/Mutex/RwLock/Atomic` 与 `share_mt`。
- 运行时已落地：原子 RC 路径与内存序（retain relaxed、release+acquire fence）。
- 编译器已落地：`Send/Sync` 推导 + `@thread_boundary` 边界校验；借用/`var`/非原子 RC 阻断跨线程。
- 回归用例已新增：`examples/diagnostics_send_sync_ok.cheng`/`examples/diagnostics_send_sync_fail.cheng` + `scripts/verify_send_sync_diag.sh`。

落地路径（生产级）：
1. 标准库：新增 `Arc/Mutex/RwLock/Atomic` 与 `share_mt`；`Arc` 走原子 retain/release；锁类型只暴露受控可变视图。
2. 运行时：原子 RC 路径与内存序（retain relaxed、release+acquire fence）；`CHENG_MM_DIAG` 增加原子 retain/release 计数。
3. 编译器：类型检查增加 `Send/Sync` 推导；线程边界 API 做静态校验；Borrowed/`var` 与非原子 RC 值禁止跨线程。
4. 回归用例：覆盖跨线程传参/捕获、`Arc` 共享、锁保护可变共享、错误用例（跨线程借用/裸指针/FFI 句柄）。

生产闭环验收：
- 线程边界的 `Send/Sync` 诊断在 Stage0/Stage1 一致；跨线程误用可稳定阻断。
- `scripts/verify_send_sync_diag.sh`、`bootstrap.sh --fullspec` 与 `bootstrap_pure.sh --fullspec` 通过。
- 性能基线可控：单线程路径无原子回退；跨线程 `Arc` 有明确成本与指标基线。

## 纯 C 工具链闭环（去旧依赖）

目标：Stage0/Stage1 + 直出 C 工具链全程由 C/Cheng 构建，bootstrap/seed/CI 不再依赖旧回退。

当前状态：
- Stage0c 已是默认入口（`bin/cheng`），`bootstrap.sh` 默认走 C stage0。
- 运行时头文件统一 `runtime/include`，直出 C 作为唯一中间层。
- `update_seed_stage0c.sh` 提供纯 C seed 更新链路。

闭环路径（生产级）：
1. Stage0c 直出 C（最小覆盖）：支持 Stage1 产出的语法子集，生成等价 C。（已落地）
2. `bootstrap.sh` / `bootstrap_pure.sh` 仅使用 `bin/cheng` + 系统 C 编译器。（已落地）
3. seed 更新与 determinism 校验全部走 C；CI 强制无旧依赖。（已落地）
4. 归并工具链文档与脚本，移除旧 Stage0 相关入口。（已落地）

验收：
- `bootstrap.sh --fullspec` 与 `bootstrap_pure.sh --fullspec` 在无旧环境下通过。
- C 生成物 hash 稳定（或接受的差异列表可解释）。
- 性能与内存不回退（基线同机对比）。

## Stage1 内部编译性能闭环（长期方案）

现状与问题：
- Stage0 -> bootstrap 内部编译的 `emit` 阶段存在明显热点，容易表现为“卡住”，需要系统性降耗与可观测闭环。
- 当前仍为单编译单元模型，模块级增量/并行闭环尚未落地。

目标与验收：
- `bootstrap.sh --fullspec` 在基线机器分钟级完成，内存稳定且无“卡住”现象。
- Stage1 internal compile 输出稳定（hash 一致），并在同等输入集下不明显慢于旧 Stage0。
- 模块级增量编译闭环：变更模块只重编，`chengc.sh --emit-c-modules --jobs` 并行调度可用，依赖失效可解释。

阶段方案：
1. 观测与热点定位
   - `CHENG_STAGE1_TRACE`/`CHENG_STAGE1_TRACE_EMIT` 输出分段与语句计数。
   - 记录 `inferExprType`/`typeHasManagedPayload` 计数，锁定重复遍历。
2. 热点缓存与结构优化（本轮落地）
   - AST 节点类型推断缓存（typeCache）与 managed payload 缓存。
   - type/procSig/typeDef/tuple/value 哈希映射替代线性扫描，避免 per-stmt getenv。
   - `CHENG_STAGE1_SKIP_OWNERSHIP=1` 可在 bootstrap 阶段跳过 ownership 分析，避免卡死。
   - procSig 签名匹配结果缓存，避免每次调用线性扫描重载链。
   - 符号 mangle 缓存，降低 emit 过程中重复字符串处理开销。
3. emit 路径优化
   - 减少重复 clone/lower、减少临时 AST 节点创建，降低 `emit` 负担。
   - 直出 C 的局部缓存优化（在不破坏确定性的前提下）。
4. 模块级增量与并行闭环
   - 构建 module graph，按模块生成 C 产物与缓存。
   - 以文件 hash/依赖链为失效基准，调度 `chengc.sh --emit-c-modules --jobs` 并行编译。
5. Stage1-only 工具链
   - 以 `stage1_runner` + `stage0c` 作为引导闭环，逐步移除 旧依赖。

## 历史记录（旧管线路线，已移除）

1. **Stage0 对接 旧管线**：保留现有宿主层词法/语法骨架，但输出旧管线可消费的 AST/IR 占位；实现与旧管线任务图的最小集成（可作为语言模式/插件）。
2. **词法/语法落地**：按 `doc/cheng-formal-spec.md` 完整实现，替换占位 AST，并补充 Stage0 回归测试。
3. **语义/借用对接 旧管线**：将语义检查、所有权/借用分析嵌入旧管线的语义/所有权 pass，利用 ORC 生成确定性释放点。
4. **后端降级**：实现 Cheng -> 旧管线中间层（旧 IR）降级 pass，确保生成物可被旧管线后端编译，支持并行/增量。
5. **标准库与自托管**：用 Cheng 重写 `src/stdlib/bootstrap/`，跑通 Cheng 自举（自托管前端→Stage1）并通过旧管线构建。
6. **工具链与 CI**: 提供 `chengc`/`chengsem` 等 CLI（基于旧管线/hastur），配置增量/并行 CI。

## 历史记录：执行清单（旧管线）

说明：以下条目为旧 IR 时代的历史记录，保留以便追溯，不再作为当前实现路径。

- [x] Stage0：旧 IR 降级路径已移除（历史记录保留）。
- [x] Stage1：具备 lexer/parser + 旧 IR codegen 的最小闭环（已移除），可自举生成 Stage1 runner。
- [x] 最小标准库：`src/stdlib/bootstrap/system.cheng`/`src/stdlib/bootstrap/os.cheng`/`src/stdlib/bootstrap/strings.cheng`/`src/stdlib/bootstrap/seqs.cheng`/`src/stdlib/bootstrap/tables.cheng`（str key 的 `Table*` API，含 `TableNext`）/`src/stdlib/bootstrap/streams.cheng`/`src/stdlib/bootstrap/core/option.cheng` 满足自举与示例运行。
- [x] IDE：实现 `walkDir`/`walkDirRec`/`execCmdEx` 并接入 Explorer/项目索引的真实工程扫描。
- [x] AST→旧 IR 覆盖补完（第一阶段，已移除）：补齐 `when/defer` 与 `for` 的 `continue` 语义，并加入 `examples/stage1_codegen_control.cheng` 覆盖样例。
- [x] AST→旧 IR 覆盖补完（第二阶段，已移除）：已覆盖 `@[]`/推导式/表达式级 `when/if/case`、命名实参重排/默认参数、`case` guard、`in/notin`(range + `seq[T]`/`str`(char/子串))、位运算/移位，并加入 `examples/stage1_codegen_fullspec.cheng`。
- [x] AST→旧 IR 覆盖补完（第三阶段，已移除）：`pattern` 进一步补齐（let/var/const tuple 解构；case wildcard 与 tuple selector 的 tuple pattern；for seq 单/双 pattern 降级）；并修复 lowering 注入顺序导致的自举破坏问题。
- [x] 语义/借用（占位闭环）：Stage0 已集成最小语义+借用占位检查（不阻断自举）；Stage1 保留 `defer` 作为语义边界，ORC 作为主路径。
- [x] 编译期借用证明 v1（Stage1）：`var` 借用禁止 return/share/global assign/传给非 var 形参；允许 reborrow，新增 `examples/diagnostics_borrow_escape_*` 覆盖。
- [x] 编译期借用证明 v1（Stage0）：对齐 `var` 借用逃逸检查与诊断，Stage0/Stage1 行为一致。
- [x] 借用诊断脚本：`scripts/verify_var_borrow_diag.sh` 覆盖 `diagnostics_borrow_escape_*` 用例。
- [x] 旧 IR 工具链（已移除）。
- [x] 纯 C seed 更新：`src/tooling/update_seed_stage0c.sh` 用 C stage0 更新 `stage1_runner.seed.c`。
- [x] 轻量 ownership move 预分析：在 monomorphize 后新增编译期预扫描，对同一语句列表内“RHS ident 无后续使用”的 `let/var/const` 与赋值标记 move，codegen 读取标记以跳过 retain 并移除跟踪，为后续 ORC 完整所有权分析铺路。
- [x] Ownership v1（Stage1）：新增 `exprClass/escapeClass/mustRetain/moveHint` side-table，借用白名单集中到 ownership pass（`get/getPtr/getPointer/TableGet*/SeqGet*/StringView/...`）；ORC 默认严格模式（可用 `CHENG_MM_STRICT=0` 关闭），禁止回退启发式。
- [x] ORC codegen v1：在 `CHENG_MM=orc` 下用 ownership 标注驱动 retain/release/move/escape，赋值覆盖与 return/`?`/expr-stmt/global assign 走统一 ORC 规则。
- [x] ORC 逃逸补齐：全局容器写入（`TablePut/add/addPtr/setStringAt/insert`）走 `share/retain` 保证容器持有引用；复杂 RHS 先落临时，避免多次求值。
- [x] moveHint 防误判：作用域内存在任意 `defer` 引用时不标记 last-use move；循环体内仅对非 loop‑carried 标识符启用 move（loop‑carried 含循环条件/迭代表达式使用以及迭代内先用后定义）；嵌套语句块 moveHint 会考虑外层后续使用（如 if/loop 后续仍用则不标记）。
- [x] 运行时 RC 对齐：`memRelease` 触发 refcount==0 立即释放并从链表摘除，避免二次释放。
- [x] ORC 回归用例：新增 `examples/test_orc_closedloop.cheng` 覆盖 overwrite/return-move/return-borrow/`?`/expr-stmt/global-escape/alias-overwrite/Err 分支未初始化赋值/`defer`+return/global 容器写入逃逸/loop overwrite/if use-after/defer outer use。
- [x] 内存诊断：`CHENG_MM_DIAG=1` 输出 retain/release 日志；提供 `memRetainCount/memReleaseCount`。
- [x] 旧管线集成（骨架，已移除）：历史记录保留，当前只保留直出 C 任务图与 `stage1_runner` 主路径。
- [x] Cheng Codex：补齐 `codex-cheng` CLI（exec/app-server/apply/config/login/logout）与事件输出，占位工具升级为可运行实现。
- [x] IDE：语法高亮模块化（`cheng-gui/services/syntax.cheng`），为 LSP 共享词法做准备。
- [x] IDE：诊断面板与最小语法检查（括号/字符串未闭合）+ 行标记。
- [x] IDE：Explorer 树（按目录分组生成目录/文件项）。
- [x] IDE：脏文件关闭提示（确认关闭/保存）。
- [x] IDE：自动缩进与行注释切换（Enter 继承缩进，`Cmd/Ctrl+/`）。
- [x] IDE：面包屑路径、缩进参考线、当前行高亮与自动保存（`CHENG_IDE_AUTOSAVE`）。
- [x] IDE：代码折叠（缩进块折叠 + 行号栏 Alt/Option+点击切换）。
- [x] IDE：迷你地图（右侧概览栏点击定位）。
- [x] IDE：分屏（上下）。
- [x] IDE：悬浮提示（诊断/局部符号）。
- [x] IDE：签名提示（当前文件/单行）。
- [x] IDE：语义补全（命名参数提示，补全 `param =`，当前文件/工作区定义）。
- [x] IDE：工程符号索引缓存（基于文件时间戳/大小复用 `projectSymbols`）。
- [x] IDE：语法高亮渲染缓存（行级 token 缓存，减少重复扫描）。
- [x] IDE：Outline/面包屑缓存（文件变更后刷新，避免每帧扫描）。
- [x] IDE：折叠可见行缓存（滚动/定位复用可见行索引）。
- [x] IDE：Quick Fix 新增 import 排序（文件级）。
- [x] IDE：Quick Fix 新增缩进对齐/闭合括号对齐（`action`）。
- [x] IDE：Quick Fix 新增连续空行折叠（文件级）。
- [x] IDE：左侧列表 hover 高亮（Explorer/Search/SCM/Run）。
- [x] IDE：右侧/底部列表 hover 高亮（Outline/Problems/VCS/Tasks/Output）。
- [x] IDE：右侧/底部 Tabs hover 高亮。
- [x] IDE：build/run/test 任务集成（终端命令，`execCmdEx`）。
- [x] IDE：语言服务诊断（终端 `diag` 触发编译器诊断）。
- [x] IDE：诊断自动触发（编辑后空闲运行；`CHENG_IDE_DIAG_AUTO`）。
- [x] IDE：引用/重命名/格式化（终端 `refs`/`rename`/`fmt`）。
- [x] IDE：任务日志面板（右侧 TASKS）。
- [x] IDE：项目级搜索（`Cmd/Ctrl+Shift+F` 或终端 `search`）。
- [x] IDE：打包脚本（`src/tooling/package_ide.sh`）。
- [x] IDE：终端新增 `package` 命令（`CHENG_IDE_PACKAGE_CMD`）。
- [x] IDE：符号跳转增强（文件/工作区；`Cmd/Ctrl+Shift+O`、`Cmd/Ctrl+T`）。
- [x] IDE：命令面板（`Cmd/Ctrl+Shift+P`）。
- [x] IDE：快速打开增强（模糊匹配 + `path:line:col`）。
- [x] IDE：VCS 状态面板与 `diff` 命令（`Cmd/Ctrl+Shift+G`）。
- [x] IDE：Git Blame（`blame` 命令，支持当前行/指定行）。
- [x] IDE：Git 历史（`history`/`log` 命令，支持当前文件/指定行）。
- [x] IDE：PTY 终端交互增强（ANSI 控制序列过滤、`\r`/`\b` 处理）。
- [x] IDE：PTY 终端逐字节输入（字符/方向键/回车/退格直通）。
- [x] IDE：多终端标签（`term` 命令新建/切换/关闭会话）。
- [x] IDE：多终端标签 UI（底部点击切换）。
- [x] IDE：多终端标签持久化（会话标签/活动会话）。
- [x] IDE：主题切换（dark/light；命令面板 `theme`）。
- [x] IDE：工程文件管理（new/mkdir/rm/mv，支持目录移动/删除）。
- [x] IDE：工作区配置与过滤（workspace_config.txt）。
- [x] IDE：多根工作区（`CHENG_IDE_ROOTS`）。
- [x] IDE：多根工作区 VCS/任务工作目录隔离。
- [x] IDE：工作区设置/布局持久化（主题/自动保存/分屏）。
- [x] IDE：输入法组合与退格/文本输入稳定性修复。
- [x] IDE：全选快捷键（`Cmd/Ctrl+A`）。
- [x] IDE：面板分隔条拖拽调整（Explorer/右侧/终端，布局持久化）。
- [x] Cheng Mobile：移动运行时与 App 生命周期接口（`cheng/runtime/mobile*.cheng`），导出脚本与示例。
- [x] Cheng Mobile：Host ABI 桥接头与 stub（`~/.cheng-packages/cheng-mobile/bridge/cheng_mobile_bridge.h`）。
- [x] Cheng Mobile：Android/iOS 模板说明（`~/.cheng-packages/cheng-mobile/android/README.md`、`~/.cheng-packages/cheng-mobile/ios/README.md`）。
- [x] Cheng Mobile：Host core 队列与 Android/iOS skeleton（`~/.cheng-packages/cheng-mobile/bridge/cheng_mobile_host_core.c`、`~/.cheng-packages/cheng-mobile/*/cheng_mobile_host_*`）。
- [x] Cheng Mobile：Host 示例与导出桥接（`examples/mobile_host_hello.cheng`，`build_mobile_export.sh --with-bridge`）。
- [x] Cheng Mobile：Host 事件注入辅助（`~/.cheng-packages/cheng-mobile/bridge/cheng_mobile_host_api.*`）。
- [x] Cheng Mobile：Android JNI / iOS glue 事件桥接（`~/.cheng-packages/cheng-mobile/android/cheng_mobile_android_jni.c`、`~/.cheng-packages/cheng-mobile/ios/cheng_mobile_ios_glue.*`）。
- [x] Cheng Mobile：Android Kotlin 最小接入与 iOS UIKit ViewController（`~/.cheng-packages/cheng-mobile/android/Cheng*.kt`、`~/.cheng-packages/cheng-mobile/ios/ChengViewController.*`）。
- [x] Cheng Mobile：Android/iOS 最小像素呈现（Surface/Layer blit）。
- [x] Cheng Mobile：Android/iOS 工程模板（`~/.cheng-packages/cheng-mobile/android/project_template`、`~/.cheng-packages/cheng-mobile/ios/project_template`）。
- [x] Cheng Mobile：Host 示例新增像素缓冲呈现（`examples/mobile_host_hello.cheng`）。
- [x] Cheng Mobile：Android Gradle Wrapper + iOS XcodeGen 生成脚本（`~/.cheng-packages/cheng-mobile/android/project_template/gradlew`、`~/.cheng-packages/cheng-mobile/ios/project_template/project.yml`）。
- [x] Cheng Mobile：Android/iOS 文本输入桥接（IME/UIKeyInput）。

## 执行清单（下一阶段）

- [x] IDE：选区 + 内置剪贴板与 Undo/Redo
- [x] IDE：鼠标拖选 + 系统剪贴板（基础）
- [x] IDE：多光标/块选（Alt/Option+点击/拖选）
- [x] IDE：自动配对与语义缩进规则
- [x] IDE：语义语言服务（诊断/补全/跳转）
- [x] IDE：工作区持久化
- [x] IDE：Git diff/暂存/提交视图
- [x] IDE：Git 分支/冲突处理
- [x] IDE：PTY 终端基础接入
- [x] IDE：调试器基础接入
- [x] IDE：调试器后端接入与单步/Watch
- [x] IDE：编辑/高亮/跳转/补全增强（关键字/字面量/匹配/块式索引）
- [x] IDE：键盘多光标（Cmd/Ctrl+Alt+↑/↓）
- [x] IDE：选区多行缩进/反缩进（Tab/Shift+Tab）
- [x] IDE：块选扩展（Alt/Option+Shift+↑/↓）
- [x] IDE：跨行选择合并（行号栏拖选）
- [x] IDE：行操作快捷键（移动/复制/删除/整行选择）
- [x] IDE：选区扩展/收缩（Alt/Option+Shift+→/←）
- [x] IDE：轻量格式化器增强（缩进/注释感知）
- [x] IDE：语义缩进与自动配对扩展（`except/finally` 对齐、括号换行缩进）
- [x] IDE：多行表达式续行缩进（运算符/逗号/点号）
- [x] IDE：闭合括号自动对齐（行首 `)`/`]`/`}`）
- [x] IDE：括号续行对齐（对齐到开括号列）
- [x] IDE：代码折叠（缩进块折叠 + 行号栏 Alt/Option+点击切换）
- [x] IDE：迷你地图（右侧概览栏点击定位）
- [x] IDE：分屏（上下）
- [x] IDE：悬浮提示（诊断/局部符号）
- [x] IDE：签名提示（当前文件/单行）
- [x] IDE：调试器后端联调与断点/变量刷新（debug backend 脚本接入）

## 已解决问题

- **Stage1 parser 单行 if 死循环**：修复 `if cond: ...; continue` 被错误降级为“无条件 continue”的问题（`parseTypeDecl/parseVarLet`）。
- **Stage1 parser 的 `elif` 链断裂**：`parseIf` 在 then/elif/else 之间 `skipNewlines`，允许夹杂空行/注释行而不中断分支解析。
- **Stage1 parser 缺少 `case/of/else`**：补齐 `parseCase`，使 `case n.kind:` 这类模式可被解析并进入 codegen。
- **Stage1 lexer 无法识别字符字面量**：用数值 `39`/`92` 替代 `'\''`/`'\\'`（stage0 对部分转义字面量处理不完整）。
- **Stage1 codegen 产物无法链接**：`@importc` 对 `mul_0/div_0/...` 这类 `*_0` 内部符号仅生成 `imp`，不生成 wrapper，避免链接期未定义符号。
- **读取源码 O(n^2) 内存爆炸**：`src/stdlib/bootstrap/os.cheng:readAll` 改为 chunk 读取 + 动态扩容，stage0/stage1 自举稳定。
- **Stage1 自举链路误报未定义标识符**：语义检查补齐顶层全局收集与 enum 字段可见性（如 `fmRead`），避免 self-host 被误报中断。

## 编译与运行（完全自举）

```bash
# 一键完全自举（Stage0→Stage1，内部生成 bootstrap runner，并验收确定性；可选 fullspec）
src/tooling/bootstrap.sh --fullspec

# 纯 Cheng 自举（不调用旧 Stage0；以 stage1_runner 为种子；若缺少则从 seed C 构建）
src/tooling/bootstrap_pure.sh --fullspec

# 生成 stage1_runner C 并编译
bin/cheng --mode:c --file:src/stage1/frontend_stage1.cheng --out:/tmp/stage1_runner.c
cc -Iruntime/include -Isrc/stdlib/bootstrap /tmp/stage1_runner.c src/stdlib/bootstrap/system_helpers.c -o stage1_runner

# 运行 stage1_runner，生成 bootstrap frontend C
./stage1_runner --mode:c --file:src/stage1/frontend_bootstrap.cheng --out:/tmp/frontend_bootstrap.c

# （可选）编译并运行 fullspec 覆盖样例生成物
./stage1_runner --mode:c --file:examples/stage1_codegen_fullspec.cheng --out:/tmp/stage1_codegen_fullspec.c
cc -Iruntime/include -Isrc/stdlib/bootstrap /tmp/stage1_codegen_fullspec.c src/stdlib/bootstrap/system_helpers.c -o stage1_codegen_fullspec
./stage1_codegen_fullspec

# 编译 stage1_runner 并验证输出稳定
./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:/tmp/stage1_runner.c
shasum -a 256 /tmp/stage1_runner.c
./stage1_runner
shasum -a 256 /tmp/stage1_runner.c

# Stage1 编译器 CLI：直接把 .cheng 编译为 C
./stage1_runner --mode:c --file:examples/stage1_codegen_hello.cheng --out:/tmp/hello_cli.c
```

期望输出：两次 SHA256 相同；可选步骤中 `./stage1_codegen_fullspec` 输出 `fullspec ok`。

## 验收记录

说明：旧 IR 时代的验收记录已归档（不再作为当前路径）。
- [x] 2026-01-04：构建管线初步接入包管理器：`chengc.sh` 支持 `--manifest/--lock/--registry/--package/--channel`，自动 resolve/verify lock 并生成 `chengcache/<name>.buildmeta.toml`（含 lock/meta 路径与摘要）。
- [x] 2026-01-04：注册中心与构建管线接入：`chengc.sh` 读取 pkgmeta 并写入 `[snapshot]`（`cid/author_id/pub_key/signature/epoch`），便于产物携带快照签名信息。
- [x] 2026-01-04：运行时计量与抽查闭环：`cheng_storage meter/audit` 写入 `ledger.jsonl`（compute/audit 事件），`settle` 可汇总收益（链上 RWAD 结算待接入）。
- [x] 2026-01-04：补齐构建期校验脚本：`src/tooling/verify_buildmeta.sh` 校验 buildmeta/lock/pkgmeta/snapshot，支持可选 ledger 校验。
- [x] 2026-01-22：`chengc.sh` 新增 `--emit-asm` 汇编输出管线，支持 `CHENG_ASM_CC/CHENG_ASM_FLAGS/CHENG_ASM_TARGET`，默认输出 `chengcache/<name>.s` 并跳过链接。
- [x] 2026-01-22：stage0c core 新增 `--mode:asm` 直出汇编（v0：仅常量返回），支持 `x86_64/aarch64/riscv64`；ABI 覆盖 Darwin/ELF/COFF（macOS/iOS、Linux/Android/鸿蒙、Windows）。
- [x] 2026-01-22：新增 `src/tooling/verify_asm_backend.sh`，并接入 `bootstrap_stage0c_core.sh --asm/--fullspec` 形成验证闭环。

## 去中心化计算与存储（规划）

- [ ] RWAD 链上结算：所有租约/计量/回执与分润使用 RWAD，并产生链上凭证。
- [x] 方案文档：新增 `doc/cheng-decentralized-compute-storage.md`，明确可选模式、租约计费、分润与路线图。
- [x] Cheng 侧模块落地：新增 `cheng/decentralized/*`（CID/租约/账本/本地存储）。
- [x] CLI 起步：新增 `src/tooling/cheng_storage.cheng`（init/put/get/lease）。
- [x] CLI 补充：新增 `cheng_storage put-text/cat`（走 io_backend，可配租约，支持 stdin + cat raw）。
- [x] CLI 对齐：`cheng_storage put` 走 io_backend（支持租约写入落账本与强制租约策略）。
- [x] Demo 脚本：新增 `src/tooling/demo_io_lease.sh`（租约 -> 写入 -> 读取 -> 结算；支持 local/p2p、租约强制与失败路径验证；包含 cat raw 与 settle toml/yaml 演示；支持租约复用/重生成）。
- [x] 验收脚本：新增 `src/tooling/verify_demo_io_lease.sh`（校验 payout 字段与存储结算输出）。
- [x] libp2p 源码同步：将 `cheng-libp2p/cheng/libp2p` 统一同步至 `cheng/libp2p`。
- [x] 运行时模式入口：新增 `cheng/decentralized/runtime.cheng`，支持 `--mode:local|p2p` 入口。
- [x] libp2p 运行时接入：`cheng/decentralized/p2p.cheng` 打通 bitswap 请求/serve，支持 `--mode:p2p` 读写与预热。
- [x] 通道化依赖与签名产物：注册中心映射 `edge/stable/lts` 与快照校验，减少版本碎片。
- [x] 计量与结算：存储租约分润 + 计算计量分润按 epoch 汇总（链上 RWAD 结算待接入）。
- [x] 仲裁与反作弊：最小抽查/回执机制，保障可验证性。
- [x] 租约 token 签名/校验：新增 `cheng/decentralized/lease_token.cheng`，CLI 支持 `leasegen` 与 `put --lease`。
- [x] 写入强制租约（可配置）：`cheng_storage put` 在 `--mode:p2p` 下默认要求 `--lease`，提供 `--no-lease` 开关。
- [x] IO backend 文本读写：`io_backend.readText/writeText` 与 `readTextAuto/writeTextAuto`（按环境变量初始化默认 backend）。
- [x] IO backend 租约写入：`storeBytesWithLease/putFileWithLease` 与 `*Auto`（写入前验证租约 token + ledger 记录）。
- [x] IO backend 租约强制开关：`CHENG_IO_REQUIRE_LEASE=1`（p2p 模式拒绝无租约写入）。
- [x] IO shim：应用侧 `readFileAuto/writeFileAuto`、`write*WithLease` 与 `ioLastError`（无 Result 样板）。
- [x] 包管理器最小闭环：manifest/lock 支持 TOML/YAML（可兼容 JSON），registry resolve + lock/verify CLI（`src/tooling/cheng_pkg.cheng`）。
- [x] 编译产物元数据雏形：`cheng_pkg meta` 结合 lock + registry 输出元数据（TOML/YAML/JSON）。

### 去中心化计算与存储（下一阶段）
- [ ] RWAD 链上结算：结算模块、批量提交与链上凭证索引。
- [x] 包管理器接入：与编译管线/依赖图深度集成（lock 产物写入构建元数据）。
- [x] 注册中心与构建管线接入：编译产物写入包作者节点 ID、快照 CID 与签名产物。
- [x] 运行时计量接入：为 IO/计算调用打点并写入账本（最小 SDK/宏）。
- [x] 存储证明/抽查机制 MVP：最小抽检与惩罚回执闭环。

### 去中心化计算与存储（统筹执行计划・TOML）
```toml
version = "2025-01-15"
owner = "cheng-core"
mode = "opt-in"
principles = ["local-default", "verifiable-artifacts", "single-version", "read-free"]
scope = ["pkg", "registry", "storage", "compute", "rewards", "compiler", "libp2p"]

[[phase]]
id = "P0"
name = "spec-freeze"
status = "done"
exit = ["cid-v1", "lease-token-v1", "metering-v1", "registry-record-v1", "lock-toml-v1"]

[[phase]]
id = "P1"
name = "storage-mvp"
status = "done"
exit = ["local-store", "p2p-bitswap", "lease-verify", "ledger-jsonl"]

[[phase]]
id = "P2"
name = "compute-mvp"
status = "done"
exit = ["exec-request", "receipt-v1", "metering-sdk", "epoch-settle"]

[[phase]]
id = "P3"
name = "compiler-integration"
status = "done"
exit = ["artifact-meta", "p2p-io-backend", "mode-flag", "buildmeta-toml"]

[[phase]]
id = "P4"
name = "trust-audit"
status = "done"
exit = ["sampling-audit", "fraud-report", "rate-limit", "reputation"]

[[phase]]
id = "P5"
name = "storage-proof"
status = "done"
exit = ["storage-proof", "proof-ledger", "proof-cli"]

[[phase]]
id = "P6"
name = "provider-reputation"
status = "done"
exit = ["provider-reputation", "provider-repute-cli"]

[[phase]]
id = "P7"
name = "rwad-onchain-settlement"
status = "planned"
exit = ["rwad-settlement-module", "batch-settle", "onchain-receipt-index", "rwad-bridge-hook"]

[[task]]
id = "compute-exec-protocol"
phase = "P2"
area = "compute"
priority = "p0"
status = "done"
deps = ["storage-mvp"]
deliverables = ["exec.request.jsonl", "exec.receipt.jsonl"]

[[task]]
id = "metering-sdk"
phase = "P2"
area = "sdk"
priority = "p0"
status = "done"
deliverables = ["metering_sdk.cheng", "withMetering template", "io-meter-hook(os+file_bytes)", "macro-instrumentation"]

[[task]]
id = "settlement-pipeline"
phase = "P2"
area = "rewards"
priority = "p1"
status = "done"
deliverables = ["epoch-summary", "payout-preview(toml/yaml)", "audit-penalties", "reconcile-check", "receipt-status-anomalies", "epoch-mismatch-reconcile", "reconcile-csv", "trust-summary"]

[[task]]
id = "rwad-settlement-module"
phase = "P7"
area = "rewards"
priority = "p0"
status = "planned"
deliverables = ["rwad-settlement-module", "batch-settle-tx", "event-index"]

[[task]]
id = "ledger-to-chain-batcher"
phase = "P7"
area = "bridge"
priority = "p0"
status = "planned"
deliverables = ["ledger-batch", "tx-hash-map", "reconcile-report"]

[[task]]
id = "artifact-metadata"
phase = "P3"
area = "compiler"
priority = "p0"
status = "done"
deliverables = ["artifact.meta.toml", "signature-binding"]

[[task]]
id = "p2p-io-backend"
phase = "P3"
area = "runtime"
priority = "p0"
status = "done"
deliverables = ["io.backend.p2p", "mode-switch"]

[[task]]
id = "io-backend-text"
phase = "P3"
area = "runtime"
priority = "p2"
status = "done"
deliverables = ["io_backend.readText/writeText", "io_backend.readTextAuto/writeTextAuto"]

[[task]]
id = "io-backend-lease"
phase = "P3"
area = "runtime"
priority = "p1"
status = "done"
deliverables = ["io_backend.storeBytesWithLease", "io_backend.putFileWithLease", "io_backend.*Auto"]

[[task]]
id = "io-backend-lease-enforce"
phase = "P3"
area = "runtime"
priority = "p2"
status = "done"
deliverables = ["CHENG_IO_REQUIRE_LEASE", "p2p write require lease"]

[[task]]
id = "io-shim-auto"
phase = "P3"
area = "runtime"
priority = "p3"
status = "done"
deliverables = ["io_shim.readFileAuto", "io_shim.writeFileAuto", "io_shim.write*WithLease", "io_shim.ioLastError"]

[[task]]
id = "io-demo-script"
phase = "P3"
area = "tooling"
priority = "p3"
status = "done"
deliverables = ["src/tooling/demo_io_lease.sh"]

[[task]]
id = "io-demo-verify"
phase = "P3"
area = "tooling"
priority = "p3"
status = "done"
deliverables = ["src/tooling/verify_demo_io_lease.sh"]

[[task]]
id = "single-version-enforce"
phase = "P3"
area = "pkg"
priority = "p1"
status = "done"
deliverables = ["resolve-rule", "conflict-report"]

[[task]]
id = "trust-audit"
phase = "P4"
area = "trust"
priority = "p1"
status = "done"
deliverables = ["sampling-audit", "fraud-report", "rate-limit", "reputation"]

[[task]]
id = "storage-proof"
phase = "P5"
area = "storage"
priority = "p1"
status = "done"
deliverables = ["storage_proof.cheng", "ledger.storage_proof", "cheng_storage proof"]

[[task]]
id = "provider-reputation"
phase = "P6"
area = "storage"
priority = "p1"
status = "done"
deliverables = ["provider_reputation.cheng", "cheng_storage store-repute"]
```

## 决策记录

## ORC 闭环验收清单（Stage1）

- 刷新自举产物（避免旧 `stage1_runner`）：
  - `CHENG_MM=orc src/tooling/bootstrap.sh`
- ORC 默认回归（期望 refcount 断言通过）：
  - `CHENG_MM=orc src/tooling/chengc.sh examples/test_orc_closedloop.cheng --name:test_orc_closedloop`
  - `./test_orc_closedloop`
- Off 模式回归（不插桩，允许跳过 refcount 断言）：
  - `CHENG_MM=off src/tooling/chengc.sh examples/test_orc_closedloop.cheng --name:test_orc_closedloop_off`
  - `./test_orc_closedloop_off`
- 关键观测点：overwrite/return-move/return-borrow/`?`/expr-stmt/global-escape/`defer`+return/global 容器写入/loop overwrite 的 `memRefCount` 变化符合预期；Err 分支不误释放旧值。

- libp2p 不进入语言“核心语法/类型系统”，作为 **标准库/运行时可选模块** 提供。
- Cheng 语言相关实现统一放置于 `cheng/` 目录内，避免跨仓库散落。
- manifest/lock 默认采用 TOML（密度更高），可选 YAML；registry/ledger 仍使用 JSONL 便于追加与调试。

## 约定

- 所有公共接口、诊断文本使用中文。
- 当前优先保证 Stage0/Stage1 自举链路可运行；与旧管线任务图/增量/并行的深度集成后续迭代。
