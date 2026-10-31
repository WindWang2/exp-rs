# FINAL_REPORT — R26-science（2026-10-31）

## 完成状态：DONE_TESTED（PR 已创建，未自动合并）

- **PR: https://github.com/WindWang2/exp-rs/pull/1731**（OPEN, MERGEABLE）
- base master@2610cb4741 → head agent/r26-science-20261030T151203Z@fc9ba91a36
- 2 commits：9fdf8d8aaa（12 项修复） + fc9ba91a36（R1/R2 处置）

## 工作包总账（12 + E2E + PERF）

| WP | 状态 | 证据 |
|----|------|------|
| S1 percentClip 下溢越界读 | DONE_TESTED | RED=SIGABRT/libstdc++ 断言；GREEN×2 + 复审后×2 |
| S2 训练 cap 在 NoData 前 | DONE_TESTED | RED 计数 5/5 vs 10/8；GREEN×2 |
| S3 isotonic 重复 knot sidecar 报废 | DONE_TESTED | RED knot [1,1,2]；GREEN×2 |
| S4 MK var(S) 时间并列项 | DONE_TESTED（修订：tau-a 契约保留） | RED varS 66/18 vs 48/18；Gilbert 基准位保持 |
| S5 kFold QHash 序破坏种子契约 | DONE_TESTED | RED 与参考模拟不一致 |
| S6 remap 缺失伪满分 | DONE_TESTED | RED 1.0 vs 0 |
| S7 majority key=0 平票 | DONE_TESTED | RED 5 vs 0 |
| S8 polygonize 4/8 连通 | DONE_TESTED | RED 2 要素 vs 1；自接触行为实证固化 |
| S9 logRatio 非有限→0 | DONE_TESTED | RED 0.0 vs NaN |
| S10 changedArea ft² 当 m² | DONE_TESTED | RED 4.0 vs 0.371612 m² |
| S11 spatial CV CV_64F | UNREPRODUCED（探针实证，测试保留为契约守卫） | OpenCV 5 copyTo 就地转换 |
| S12 scaler fromJson 校验 | DONE_TESTED | RED 接受 vs fail-closed |
| E2E 注册表链路 | DONE_TESTED | 152 断言（registry→算子→GDAL 回读） |
| PERF 基线 | DONE（3 规格×5 进程，无提速主张） | PERF_BASELINE.md |

## 硬门自检
- [x] RED→GREEN×2（11 RED 证据 + 两轮全绿 + 复审后两轮全绿）
- [x] 关键构建+定向 ctest 两轮绿（16 套件 ×2 轮）
- [x] 注册完整性 1064 目标（added 1+1 / lost 0+0）
- [x] R1 approve-with-fixes → P1/P2 全修；R2 approve → 6×P2 修、3×P2 记录
- [x] P0/P1 = 0（R1/R2 终态）
- [x] gh pr create 成功返回 URL
- [x] git diff --check 干净
- [x] 不自动合并（PR 保持 OPEN）

## 未运行矩阵（NOT_EXECUTED，已在 PR 描述声明）
GPU/ONNX/OpenVINO/TensorRT/OTB 算子路径；Windows/MSVC；Release 性能对照；
在线 CI。

## 遗留（已记录，未阻塞）
- S2 capped 候选级内存画像（彻底方案建议后续 perf 轨）
- S4 双轴并列 var(S) 两矩近似（头文件已文档化）
- 工程/局部 CRS changedArea 单位缺口（既有范围，未扩围）
- CROSS_TRACK：registry 包装算子同进程第二次运行 rs:change_detection(makeMask) 失败
  （复现步骤在 PERF_BASELINE.md，根因疑在 runtime 包装层，不在本轨写区）
