你看到的“提高了 4 倍性能”，通常**不是指整个 FFmpeg 端到端编码/解码都快 4 倍**，而是指 FFmpeg 里某个非常热点的**内核函数（DSP routine）**，在某个平台（比如 ARM64）上用**手写 NEON 汇编**替换/优化后，在 FFmpeg 自带的 micro-benchmark（`checkasm`）里，相对 C 参考实现的速度达到“4.x 倍”。

下面用一个**明确、可追溯到提交记录**的例子来解释“4 倍”到底指什么，以及这类汇编代码大概怎么写。

---

## “4 倍”具体指的是什么：一个函数的 checkasm 基准测试结果

腾讯工程师（提交邮箱为 `@tencent.com`）在 FFmpeg 的 AArch64 路径里做过多次 NEON 优化。一个和“4 倍”非常对得上的例子是：

**`aarch64/h26x: optimize sao_band_filter`** —— 优化 H.26x（HEVC/VVC 共用）的 **SAO band filter**（Sample Adaptive Offset 的 band 滤波）NEON 汇编实现。提交说明里给了在 **Raspberry Pi 5** 上的 `checkasm` 跑分对比。([FFmpeg][1])

其中最典型的一行（8x8 block）是：

* `hevc_sao_band_8_8_c: 252.3 (1.00x)`
* `hevc_sao_band_8_8_neon: 61.0 (≈4.14x)`（同一线程里也讨论到换 `tbl` 后能更高一些，但核心结论是“4 倍左右”）([FFmpeg][1])

更大的块（16/32/48/64）还能到 **~5.7x、~7.0x** 这种级别（仍然是“这个函数”相对 C 的倍数，不是整体解码倍数）。([FFmpeg][1])

### 关键点：这不是“FFmpeg 总体快 4 倍”

`checkasm` 测的是**单个函数的循环热点**（比如 SAO 这一小段），它在整个解码流水线里只占一部分时间：

* 如果 SAO 在某些片源/分辨率/平台上占比很高，整体解码会明显受益；
* 但整体不可能简单等于 4 倍，因为还有熵解码、运动补偿、去块滤波、线程调度、内存带宽等其它开销。

---

## 这个被优化的函数在干什么：SAO band filter 是 HEVC/VVC 的环路滤波之一

SAO（Sample Adaptive Offset）是 HEVC 引入的一种环路后处理滤波，用来减少带状/量化伪影；VVC 里也保留并继续使用。SAO 的 **band filter** 大致做的事可以概括成：

1. 把像素按高位分到 0..31 的“band”（8-bit 时常见是 `band = pixel >> 3`）。
2. 用一个 32 项的查表（LUT）拿到 offset（大多数 band 的 offset 为 0，只有连续的少数 band 有非零 offset）。
3. `dst = clip_uint8(src + offset)`。

（你在提交 diff 里能看到典型的 `>> 3`、查表、饱和裁剪这些步骤。）([FFmpeg][2])

---

## 这次“4 倍”为什么能做到：用更小 LUT + NEON `tbl` 查表 + 16 像素向量化

这个优化的核心思路可以从提交 diff 直接看出来：([FFmpeg][2])

### 1) LUT 从 int16 改成 int8（更省带宽/寄存器）

提交说明里写得很直白：**8-bit 流的 offset_table 用 `int8_t[]` 就够了**。([FFmpeg][1])
因此原先用 `strh` 存 16-bit 的表项，改成 `strb` 存 8-bit。([FFmpeg][2])

这带来两个直接收益：

* LUT 体积变成 **32 字节**（刚好 2 个 128-bit NEON 寄存器 `q16/q17` 就能装下）。
* 查表可以用 **`tbl`**（table lookup）在字节粒度上一次性做 16 个像素的映射。([FFmpeg][2])

### 2) 8-bit band index：`ushr v?.16b, v?.16b, #3`

把 16 个像素同时右移 3 位，得到 0..31 的索引向量。([FFmpeg][2])

### 3) `tbl` 直接把 0..31 映射到 offset（字节）

提交里也明确把原来的 `tbx` 换成了 `tbl`（邮件线程里讨论过 `tbl` 可能更快）。([FFmpeg][2])

### 4) offset 做符号扩展，src 做零扩展，然后相加并饱和裁剪

diff 里能看到典型序列：`sxtl/sxtl2`（符号扩展 offset）、`uxtl/uxtl2`（零扩展像素）、`add`、最后 `sqxtun/sqxtun2` 做饱和窄化回 u8。([FFmpeg][2])

---

## 代码大概怎么写：FFmpeg AArch64 NEON 汇编的“典型写法”

FFmpeg 的 ARM64 汇编一般放在 `libavcodec/aarch64/.../*.S`，并包含公共宏：

* `#include "libavutil/aarch64/asm.S"`
* 用 `function ... endfunc` 宏定义导出符号([FFmpeg][2])

下面给一个**接近该提交思路的“核心循环（简化示意）”**，帮助你理解“这种汇编长什么样”。（注意：这是教学用的结构化摘取/改写，不是把完整函数原封不动贴出来。）

```asm
// 假设：
//   x0 = dst, x1 = src
//   x2 = dst_stride_delta(行末修正), x3 = src_stride_delta
//   w7 = height
//   q16,q17 里装了 32-byte 的 int8 offset_table[32]

1:  // 每行
    mov     w8, w6              // w6=对齐后的 width（16 的倍数）
2:  // 每 16 像素
    ldr     q2, [x1], #16        // v2 = 16 bytes src
    subs    w8, w8, #16

    ushr    v3.16b, v2.16b, #3   // v3 = band idx (0..31)
    tbl     v3.16b, {v16.16b, v17.16b}, v3.16b   // v3 = int8 offsets

    // 扩展并相加（低 8 + 高 8）
    uxtl    v0.8h,  v2.8b        // src low 8 -> u16
    uxtl2   v1.8h,  v2.16b       // src high 8 -> u16
    sxtl    v4.8h,  v3.8b        // off low 8 -> s16
    sxtl2   v5.8h,  v3.16b       // off high 8 -> s16

    add     v0.8h, v0.8h, v4.8h
    add     v1.8h, v1.8h, v5.8h

    sqxtun  v0.8b,  v0.8h        // saturate -> u8
    sqxtun2 v0.16b, v1.8h
    str     q0, [x0], #16        // store 16 bytes

    b.ne    2b

    subs    w7, w7, #1
    add     x0, x0, x2           // dst += stride_dst（行切换）
    add     x1, x1, x3           // src += stride_src
    b.ne    1b
    ret
```

你会发现它和提交 diff 里的关键指令序列高度一致：`ushr`（算 band）、`tbl`（查 32 项表）、`sxtl/uxtl`（扩展）、`sqxtun`（饱和裁剪回 8-bit）、16 字节一组处理。([FFmpeg][2])

---

## 如果你想“自己写一段类似的 FFmpeg 汇编”，一般流程是这样的

1. **先确认热点**
   用 `perf`/`Instruments`/`VTune` 或 FFmpeg 的 `checkasm` 找到最耗时的 DSP 函数。

2. **先用 NEON intrinsics 写一版**（强烈建议）
   intrinsics 更好调试、也容易保证正确性；跑通后再决定要不要手写汇编。

3. **落到 FFmpeg 的汇编框架**

   * 放到 `libavcodec/aarch64/.../*.S`
   * `#include "libavutil/aarch64/asm.S"`
   * 用 `function xxx, export=1` 声明符号
   * 在对应的 `dsp_init*.c` 里把函数指针绑定到 NEON 版本（提交里就是这么做的）。([FFmpeg][2])

4. **用 `checkasm` 验证正确性 + 量化性能**
   FFmpeg 社区对这一步很看重：既要快，也要确保输出逐像素一致/在允许误差内。

---

## 顺带一提：你可能还看到过“5 倍”的说法

2026 年初 FFmpeg 官方账号曾公开感谢腾讯提交“手写 ARM 汇编补丁”，并提到“比 C 快 5 倍以上”；但腾讯侧账号随后又澄清说内部复核后“实际大约 2 倍，不是 5 倍”。这类“几倍”说法高度依赖**测什么函数、用什么机器、怎么统计**。([Sina Finance][3])

---

## 你如果告诉我你看到的原话/链接，我可以精确对上是哪一个补丁

腾讯相关的 FFmpeg 汇编/NEON 优化不止一个（比如 VVC 的 ALF NEON 优化也有非常明显的倍率提升，并且有人提到接近 4K@30 播放）。([FFmpeg][4])

你把你看到“4 倍”的那段出处（新闻链接/截图/commit hash/邮件链接）贴出来，我就能把“具体是哪段代码、在哪个文件、函数签名、对应 checkasm 测项”逐条对齐给你，并按你目标平台（ARMv8.2？Apple M 系列？树莓派？）讲清楚怎么复现测试与改写。

[1]: https://ffmpeg.org/pipermail/ffmpeg-devel/2025-April/342940.html "https://ffmpeg.org/pipermail/ffmpeg-devel/2025-April/342940.html"
[2]: https://ffmpeg.org/pipermail/ffmpeg-cvslog/2025-April/148269.html "https://ffmpeg.org/pipermail/ffmpeg-cvslog/2025-April/148269.html"
[3]: https://finance.sina.com.cn/tech/digi/2026-01-08/doc-inhfqtyr6709646.shtml "https://finance.sina.com.cn/tech/digi/2026-01-08/doc-inhfqtyr6709646.shtml"
[4]: https://ffmpeg.org/pipermail/ffmpeg-devel/2024-July/331240.html "https://ffmpeg.org/pipermail/ffmpeg-devel/2024-July/331240.html"
