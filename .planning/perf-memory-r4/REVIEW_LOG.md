# REVIEW_LOG — Track 18 perf-memory R4

## 独立对抗性 Review（1 个 subagent 槽位，只读，全程留证）

- 范围：`15e5c66b5..HEAD` 全部 diff（评审时 HEAD=e8b5a25c3f，10 提交，21 文件）。
- 轴：Standards（C++20/内存安全/融合等价性/CMake）+ Spec（白名单、豁免真实性、
  下限数字三方对账：台账 vs 编译/测试日志 vs 账本轮次）。
- 结论：**PASS-with-notes**。

### 逐位等价核验通过的关键项（评审员独立推导）

- rs_change_streaming 融合：对 `hasNd&&finite(nd)`、`nd=±inf`、`nd=NaN`、`!hasNd`
  四类输入逐一证明与原两分支逐位等价（#444 float-space 比较、#720 非有限全扫语义保持）。
- rs_qa_mask OR+count 融合、rs_spectral_index 归一化融合：次序/放置逐样本不变。
- rs_recode flatMap 首中即 QMap::value 语义；narrowing exact 由 pass-1 全图范围先定 gdt。
- gdal 预填跳除：全部 55 个调用点的返回值都被检查，无未初始化消费。
- 守护界值出处逐条真实（ADR 0073:16 / USER_GUIDE.md:1227 / ADR 0089 +
  rs_change_primitives.cpp:233 逐字吻合）；豁免引用与 #1336/#1337/#1338 diff 精确吻合
  （5 命令 ID 54 处命中、atomic 4 例 RED 基线同数、33 码=#1337 文案）；白名单零越界；
  obs 基线 6 件与 baseline md 逐数一致；failure_11 新增红如实记录未粉饰。

### 发现与处置

| 级别 | 发现 | 处置 |
|---|---|---|
| P1 | `$<LINK_LIBRARY:WHOLE_ARCHIVE,…>` 需 CMake ≥3.24，与顶层 `cmake_minimum_required(3.20)` 冲突，3.20-3.23 下 configure 硬错误 | **已修**：改为经典 `-Wl,--whole-archive/-Wl,--no-whole-archive` 显式三段写法（3.13+ 可用），并以 `if(UNIX AND NOT APPLE)` 限定 GNU/ELF 场景（同时化解 P2-GNU-only 面）；提交 follow-up |
| P2 | 链接修复为 GNU ld/Linux 专属，win32 lane 未修 | **已修**：同上平台限定；本体即 #1335 在途过渡（注释+提交信息均已声明合并后整段删除） |
| P2 | READINESS_CLOSURE.md 被 gitignore 却被提交信息引用；A13/A15-A19 行遗留 `_待跑_` 与 D 节"零悬置"矛盾 | **已修**：陈旧行全部补齐为终态（本文件可审计）；PR 正文将自带处置摘要，不依赖 gitignored 工件 |
| P2 | 提交声称守记录 Kahan 输出和，但 tiled_inference 守护用 per-tile 覆盖 checksum、已提交 JSON 无 output_sum、EVIDENCE §6 留空 | **已修**：tiled_inference 补 rasterPixelSum；三个守护 JSON 用最终双跑重新生成（含 output_sum）；EVIDENCE §6 由 Oracle 两轮实测填充 |
| P3 | EVIDENCE §5 的 change 67/67 与已提交 JSON 68 不一致 | **已修**：统一以最终双跑重生成的 JSON 数值为准 |
| P3 | 8 处优化中 4 处仅机制论证、计时在噪声内 | **如实保留**：EVIDENCE §2 已逐条标注"噪声内/机制级"；不夸大 |
| P3 | majority hoist 在 height==0 退化输入下也会分配（有界，实害趋零） | **接受**：保持现状，记录在案 |

### 复核（修复后）

- CMake 改动后全测试面重新链接（ninja 无失败），Oracle 两轮以修复后二进制执行。
- 守护最终双跑：9/9 绿；output_sum 两轮逐位一致；RSS 全部在界内（数值见 EVIDENCE §5）。
