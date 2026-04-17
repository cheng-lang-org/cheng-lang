# Cheng v3 重写 codex-rs 第一阶段方案

  ## Summary

  目标是在 /Users/lbcheng/cheng-lang/v3/codex 内，用 Cheng v3 重建一套可日常使用的 Codex 首发产品，第一阶段同时覆盖 core + exec + cli
  + tui 四层，不做“只搭骨架”或“只做单入口”的缩水版本。
  实施顺序采用“四层并推，但内核合同先冻结”的方式：先定义统一协议和状态机，再并行填充 exec/cli/tui，避免后期返工。
  完成定义是两套入口都能日常使用：exec 可稳定跑完整非交互任务链，tui 可稳定承担主交互工作流。

  ## Key Changes

  - 在 v3/codex 下建立独立 Cheng 包，不混进现有 v3/src/tooling 和 v3/src/project 主线；目录固定拆成：
    core/、exec/、cli/、tui/、protocol/、runtime/、tests/、docs/。
  - 先做一份最小但闭合的 Cheng 版 Codex 统一合同，作为四层共享真值：
    会话模型、消息项模型、工具调用模型、命令执行模型、审批模型、配置模型、插件/技能注入模型、UI 事件模型、持久化 rollout 模型。
  - core 负责纯业务语义，不直接打印，不直接绑终端，不直接绑具体 UI。
    包含：
    线程/会话状态机、turn 驱动、工具分发、shell 执行编排、配置装载、技能注入、插件发现、消息历史、rollout 持久化、审批决策。
  - exec 做非交互主链，首批必须打通：
    prompt 输入、stdin 拼接、会话启动、工具调用、命令执行、最终输出、退出码。
    第一阶段只保留生产主链，不做 Rust 版那些实验性或隐藏命令的全量复刻。
  - cli 做统一多命令入口，第一阶段固定收口到可日常用的子命令集合：
    默认交互启动、exec、resume、login/logout、mcp、plugin、sandbox、apply。
    其余命令一律延后，避免把第一阶段拖成无限工程。
  - tui 直接做全屏终端 UI，但只围绕主工作流建最小闭环：
    会话列表/当前线程、消息流渲染、输入框、审批面板、工具运行日志区、退出恢复提示。
    不复制 Rust 版全部细枝末节 UI；先保证稳定事件驱动，不允许轮询式假 UI。
  - 复用现有 v3 已有能力，不重复造轮子：
    命令行解析走 std/cmdline；
    本地进程/文件/路径能力尽量复用 v3/src/tooling、v3/src/std、v3/src/runtime 现有实现；
    已有 native-gui/TUI 相关经验只抽协议和事件模型，不直接耦死到 r2c 专用逻辑。
  - 第一阶段明确不做的东西：
    云端任务、远程 app-server、完整平台沙箱后端、所有实验命令、所有历史兼容别名。
    这些只保留接口占位，不进入首发验收。

  ## Public Interfaces

  - 新增 Cheng 包入口：
    v3/codex/cheng-package.toml
  - 新增统一协议模块：
    v3/codex/protocol/*
    对外固定暴露：
    SessionId、ThreadId、TurnInput、TurnEvent、ToolCall、ExecRequest、ExecResult、ApprovalRequest、ApprovalDecision、PluginSpec、
    SkillSpec、ConfigSnapshot。
  - 新增四个产品入口：
    v3/codex/exec/main.cheng
    v3/codex/cli/main.cheng
    v3/codex/tui/main.cheng
    v3/codex/core/*.cheng
  - cli 对外命令面第一阶段固定，不允许边写边漂：
    默认启动进入 TUI；
    codex exec ... 非交互执行；
    codex resume 恢复最近或指定会话；
    codex mcp ...、codex plugin ... 做最小管理面；
    codex sandbox ... 接 Cheng 版 sandbox 编排入口；
    codex apply 接补丁应用主链。
  - 持久化文件格式第一阶段直接定文本或 JSON 单一真值，不做双格式兼容层。
    旧 Rust rollout 数据不要求直接兼容读取；如果要迁移，单独做一次性导入工具，不把兼容逻辑塞进主运行时。

  ## Implementation Plan

  - 第 1 步：产出 Cheng 版 codex 全局合同文档和模块图，冻结四层边界、事件流、数据流、持久化格式、命令面。
  - 第 2 步：先落 protocol + core 最小闭环。
    必须先跑通“单线程 turn + shell tool + 审批 + rollout 持久化”的纯核心测试。
  - 第 3 步：并行接 exec 与 cli。
    exec 先做稳定自动化入口；
    cli 同步做子命令路由和配置注入，但所有业务都只调 core。
  - 第 4 步：并行建设 tui 事件环。
    先做输入、消息流、审批弹层、日志区四件套，再补恢复入口和会话列表。
  - 第 5 步：接插件/技能/MCP 最小主线。
    只做启动发现、装载、注入、最小调用，不做市场和远程复杂能力。
  - 第 6 步：统一验收。
    用同一批 smoke 覆盖 exec 和 tui 两入口，确保二者共享同一核心语义，不出现双实现分叉。

  ## Test Plan

  - 协议层：
    会话模型、工具调用模型、审批模型、持久化模型的序列化/反序列化和 round-trip。
  - core 层：
    单轮对话、连续多轮、工具成功、工具失败、审批拒绝、命令超时、会话恢复、插件/技能装载失败。
  - exec 层：
    codex exec PROMPT、stdin + prompt、退出码、最终输出、无交互完成。
  - cli 层：
    子命令分发、默认进入 TUI、配置覆盖、resume/login/plugin/mcp/sandbox/apply 主路径。
  - tui 层：
    全屏启动、输入提交、消息追加、审批交互、日志滚动、退出恢复提示。
  - 端到端：
      1. 新建会话，在 TUI 中发起任务并执行 shell tool。
      2. 退出后通过 resume 恢复同一线程继续执行。
      3. 通过 exec 跑同类任务，确认和 TUI 共享同一核心行为。
      4. 启用一个本地 skill/plugin，验证注入和调用主线。
  - 验收标准：
    exec 和 tui 都能连续承担真实日常工作流；
    所有入口只走 Cheng v3 主链；
    不依赖额外脚本，不引入 mock，不靠降级兜底。

  ## Assumptions

  - 第一阶段按“可日常使用”定义推进，但不追求 Rust workspace 全量 crate 对位复刻。
  - Rust 版 codex-rs 中实验命令、云端协同、远程 app-server、完整平台沙箱后端不进入第一阶段。
  - v3 现有 runtime、tooling、命令执行和文件系统能力足够承载首发版本；若发现硬缺口，优先补 Cheng v3 底层能力，不允许塞临时脚本绕过。
  - v3/codex 作为独立包推进，避免污染当前 v3/src 既有编译器和链路主线。