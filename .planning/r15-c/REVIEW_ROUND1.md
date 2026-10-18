# R15-C Review Round 1 — 独立审查记录

- **Reviewer**：未参与本轨任何生产代码/测试编写的独立 subagent（只读；未修改/构建/git 写操作）。
- **对象**：`git diff 46791bc65c6f552ebbcf723bb91892736d56f319..HEAD`（7 commits，13 src + 6 新测试 + 3 修改测试）。
- **结论**：**BLOCK**（1 个开放 P1 + 1 个 P2）。

## 发现与处置

| # | 级别 | 发现 | 处置 |
|---|---|---|---|
| R1-1 | **P1** | `model_execution_service.cpp` VRAM→ResourceBudgetExceeded 投影无效：字符串匹配（"vram budget" 小写连续）永远匹配不到 readiness 层的真实拼写（"model requires N MiB VRAM but the host budget is M MiB"）；且无测试覆盖 | **已修复**：匹配真实连续 token "VRAM but"；新增用例驱动真实超预算模型经 runModelInference 断言 ResourceBudgetExceeded。RED 已验证（stash 该 TU 后断言失败，exit 42）；GREEN 已验证 |
| R1-2 | **P2** | `model_publish.cpp` DetectionPublishGuard fold 可撕裂重发布的 shapefile：`removeBackupFamily` 旧顺序先删主备份 → disarm 部分删除窗口内崩溃时"无主备份+终名是新产物"，fold 会把旧产品的 companions（如陈旧 .prj）钉到新产品上 | **已修复**：两个 guard 的 removeBackupFamily 改为"先 sidecars/companions、后主文件"（park 阶梯的镜像）→ mid-disarm 崩溃必留主备份 → fold 被阻断。新增回归用例手工构建 mid-disarm 状态并钉住新产品完整性 |
| R1-3 | P3 | tile 引擎注释 "bit-identically" 夸大（默认门下 tilesSkippedNoData/forward 计算确实变化，输出栅格不变） | **已修复**：注释改为精确范围声明 |
| R1-4 | P3 | OpenVINO deviceName 对 GPU plugin device 0 返回原始 token "GPU"（守卫 `> 0`） | **已修复**：`>= 0` → "cuda:0" |
| R1-5 | P3 | 分类器 "must be a 4-d"/"must be 4-d" pattern 在树中无产生者（死 pattern） | **已修复**：删除；NCHW 拒绝限定为 fed-input 拼写（"must be" + "nchw"），rank-4 输出拒绝保持独立分类（新增回归 pin） |
| R1-6 | P3 | 字符串子串分类本质脆弱（任何措辞改写都会静默回退） | 已记录：匹配 token 由测试钉住；类型化 reason 留作后续改进（需改 evaluateRuntimeReadiness 签名，超出本轨最小修复范围） |

## Reviewer 的 12 项质询结论（摘要）

1. publish fold：mid-park（F1）+ Window C（F2）对其窗口正确；DetectionPublishGuard 的 partial-disarm 撕裂（R1-2）已修；Window C 安全（调用方要么总发 prov，要么根本不用 sidecar）。
2. chunked_run ReduceConcurrency：正确（`<` 是守卫非假设；Resumable 传 1 忽略返回值；拒绝时 AdmissionRefused 经 bridge 重抛，不会带着超预算形状运行）。
3. VramLedger：成功路径无溢出（检查后 projected ≤ capacityMb，int）；部分释放一致；生产调用方都传精确预留量 → 行为不变；0-MiB 幻影 holder 修复正确。
4. classifyInferenceError：安全（"input binding" 不会误分类 TRT cudaMalloc OOM——更早的 cudamalloc 模式先命中）；顺序无冲突。
5. runMultiInput 预检：不拒绝合法 session（http/onnxruntime/python 都声明 seam；opencv_dnn 声明且用支持多输入的基类 inferNamed；OV/TRT 两者皆未声明且未 override inferNamed——正确地单输入）。src/ 下无未声明的多输入 provider。
6. C02 square-head：无误拒（yolo-ship C=6≠409600；规则只在 C==W*H 精确相等时触发）。
7. ensemble NoData：Byte(255)/UInt16(65535) 域均正确；有效 agreement 量化上限 writeNoData-1，不吞有效像元；weightSum=0 → NaN → !isfinite 守卫 → NoData。
8. tile coverage 账目自洽（无重复计数；batch 级跳过成为冗余残余，无害）。
9. conf/nms 哨兵与范围处理正确。
10. dedup 键：llround(double) 消除 int 回绕，正常范围语义不变；Windows 上测试非 vacuous。
11. 跨轨所有权：CLEAN（未触及禁止文件；无重复编写已合并修复）。
12. tests/CMakeLists vs #1513：中部插入 vs 尾部追加，无文本冲突（结构性结论；#1513 远程 draft 内容 NOT VERIFIED）。

## Reviewer 明确标注的 NOT VERIFIED

- 未构建/运行任何测试（只读审查）。
- test_device_planner 挂起问题（无 GPU/NVML）。
- #1513 实际内容。
- 5 个新测试文件的逐条断言非虚性（除 C08 dedup 与 C14 override 外）。
- PERFORMANCE.md 数字。

第 1 轮后本轨动作：修复 R1-1（P1，含 RED/GREEN 证据）、R1-2（P2，含回归用例）、R1-3/4/5（P3）；第 2 轮复审中。
