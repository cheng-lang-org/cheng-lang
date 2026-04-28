# AI 原生 GUI 执行语义

## 结论

Cheng GUI 应用的目标不是“让 AI 看屏幕再猜操作”，而是让人类可视化和 AI 执行共享同一套 typed GUI 语义。

人类看到的是视觉投影，AI 使用的是动作投影，测试读取的是观测投影。三者必须来自同一个结构化 GUI IR 和同一条事件分发路径。

## 核心定义

AI 原生 GUI 有四个结构化对象：

- `GuiStateGraph`：应用状态、路由、焦点、权限、异步任务和资源句柄。
- `GuiSurfaceTree`：可见界面的语义树，包含稳定 id、角色、文本、布局、样式 token、可执行动作、可访问性标签和隐私等级。
- `GuiEvent`：输入事件和系统事件，包含来源、目标、参数、前置条件和幂等键。
- `GuiReceipt`：事件执行后的状态变更、语义 diff、视觉 diff、耗时、内存和副作用记录。

这些对象是 typed IR，不是拼接字符串。字符串只允许作为文本内容、诊断报告或最终 artifact 序列化结果。

## 等价执行语义

AI 和人类必须走同一条执行路径：

- 人类点击按钮：平台壳把触摸、鼠标或键盘事件转成 `GuiEvent`。
- AI 调用按钮：agent runtime 根据 `GuiSurfaceTree` 选择稳定 semantic id，生成同型 `GuiEvent`。
- 测试回放按钮：test runner 读取 trace，生成同型 `GuiEvent`。

三条路径进入同一个 event dispatcher，得到同一个 `GuiReceipt`。禁止给 AI 开隐藏捷径，禁止让测试绕过真实状态机。

## 为什么不能用坐标和 OCR 做主链

坐标、截图和 OCR 只能用于验证，不能作为主执行语义。

- 坐标会被窗口尺寸、字体、缩放、平台控件差异破坏。
- OCR 只能恢复部分文字，恢复不了可执行能力、权限、焦点、异步状态和副作用。
- 屏幕像素不是应用语义，不能证明事件会落到哪个状态转移。

正确路径是：AI 读取 `GuiSurfaceTree`，选择 semantic id，发 typed event，再用 semantic diff 和视觉 frame 双重校验。

## 数据模型草案

```cheng
type
    GuiNode =
        id: str
        role: str
        label: str
        textValue: str
        enabled: bool
        focused: bool
        bounds: RectI32
        actions: GuiAction[]
        children: GuiNode[]
        privacyLevel: int32

    GuiAction =
        kind: str
        targetId: str
        inputKind: str
        precondition: str
        effectClass: str

    GuiEvent =
        kind: str
        targetId: str
        payload: GuiEventPayload
        idempotencyKey: str

    GuiReceipt =
        accepted: bool
        stateHashBefore: str
        stateHashAfter: str
        semanticDiff: GuiSemanticDiff
        frameManifest: GuiFrameManifest
        sideEffects: GuiSideEffect[]
```

真实实现可以调整字段名，但不能把这些结构退化成手写 JSON 字符串。JSON、trace、manifest 都应该由 typed writer 从结构化对象写出。

## 字符串边界

热路径禁止字符串拼接：

- layout、事件选择、状态 diff、命中测试和动作派发不能依赖拼接字符串。
- `Fmt`、`Lines` 只用于报告、日志、测试输出和最终人读文本。
- JSON artifact 必须由结构化 writer 写出，不能在 controller 里到处 `out = out + ...`。
- 视觉渲染消费样式 token、布局树和资源表，不消费字符串模板。

这和对象文件后端同一个原则：typed IR 是主链，文本只是边界。

## 平台集成

Android 和鸿蒙的生产交付物是 `.so`，平台壳只负责生命周期、事件入口、surface 提交和权限桥接。

- Android：`AInputQueue` / IME / lifecycle 事件进入 Cheng event dispatcher，渲染输出 `surface_frame_rgba_v1` 或平台 native surface。
- OHOS：Ark/Native bridge 只绑定 `.so` 导出的稳定 ABI，事件和生命周期必须是事件驱动。
- iOS：Cheng 产物进入 Mach-O app slice、static framework 或平台允许的 native binary 形态，UIKit/SwiftUI 壳只负责系统生命周期和事件转发。
- Desktop：窗口系统事件进入同一 dispatcher，computer use 不走坐标主链，只把坐标作为视觉校验辅助。
- Browser：浏览器宿主可以作为真值机或兼容壳，但 Cheng GUI 主链不能依赖 JS/DOM 回退。

所有平台禁止轮询主循环模拟输入。输入、生命周期、权限、网络状态和后台恢复都必须走事件。

## r2c 关系

`r2c` 的 React 迁移不应该直接生成“长得像 UI 的代码”，而应该先收成 React 语义图，再降到 Cheng GUI 语义：

- React component / hook / effect / context 进入 RSG。
- RSG 降到 `GuiStateGraph`、`GuiSurfaceTree` 和 `GuiEvent`。
- native GUI bundle 写出 semantic surface、frame manifest、route replay manifest 和 host capability manifest。
- `surface_frame_rgba_v1` 继续作为跨平台视觉帧合同，但它不是唯一真值。

这样 AI 执行、native GUI、移动端 shell 和 render-compare 都消费同一份语义源。

## Computer Use 合同

100% computer use 不是“AI 能点到屏幕上每个像素”，而是每个用户可达操作都有可机器执行的语义入口。

完成标准：

- 每个交互节点都有稳定 semantic id、role、label、状态、bounds 和 actions。
- 每个 action 都能生成 typed `GuiEvent`，并返回 `GuiReceipt`。
- 每次渲染都有 `GuiSurfaceTree`、frame manifest 和 state hash。
- AI action replay 与人类输入 replay 产生同一个状态转移和同一类副作用。
- 权限、文件、网络、支付、删除这类副作用必须显式标注 effect class，不能隐藏在点击回调里。
- 视觉对拍失败和语义 diff 失败都必须 hard-fail，不能降级成截图存在就通过。

## 隐私和安全

语义树不能无条件暴露所有内容给 AI。

- 密码、token、私钥、支付信息和隐私文本必须打隐私标签。
- AI runtime 只能看到授权范围内的 label、role、action 和脱敏文本。
- 高风险 action 必须要求明确确认事件，不能由普通 click 自动触发。
- `GuiReceipt` 必须记录副作用类别和授权来源。

## 验证门禁

最小门禁应覆盖：

- semantic tree smoke：稳定 id、role、action、bounds、privacy tag。
- event replay smoke：同一事件流在 AI、人类模拟、test runner 下得到同一 state hash。
- visual equivalence smoke：`GuiSurfaceTree` 对应的 frame manifest 和 RGBA 输出一致。
- mobile shell smoke：Android/OHOS/iOS 至少覆盖启动、事件分发、frame 提交和内存统计。
- leak smoke：长事件流后 state graph、resource handle 和 frame buffer 无泄漏增长。

未覆盖语义必须编译期或启动期失败，不能走 mock、JS/WebView fallback、OCR fallback 或坐标猜测。

## 落地顺序

1. 固化 `GuiSurfaceTree`、`GuiEvent`、`GuiReceipt` 的 Cheng typed 模块。
2. 让 `r2c native-gui` 输出 semantic surface 和 event replay manifest。
3. 把现有 `surface_frame_rgba_v1` 绑定到同一 `GuiReceipt`。
4. 接 Android/OHOS `.so` 壳和 iOS native 壳的事件入口。
5. 增加 computer-use runner：只消费 semantic id 和 typed action，坐标只用于视觉校验。
6. 清理 controller 内手写 JSON 拼接，改为 typed writer。

## 非目标

- 不造一个空泛的通用 GUI 框架目录。
- 不把浏览器 DOM 当 Cheng GUI 语义主链。
- 不用截图、OCR、坐标作为主执行入口。
- 不用字符串拼接作为 GUI IR。
- 不用轮询模拟移动端生命周期或输入。
- 不通过 mock 或 fallback 声称跨平台 GUI 完成。
