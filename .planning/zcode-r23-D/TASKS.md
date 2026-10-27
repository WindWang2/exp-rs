# TASKS — R23-D Work Packages

状态机：DISCOVERED → RED_PROVEN → IMPLEMENTED → GREEN1 → GREEN2 → REVIEWED → DONE
不适用/延后必须写明 DEFERRED 原因。P0 优先，其次 P1，再次 P2/P3。

## WP-D1（P1）CT-6 收口：foundry 场景校准规则 + 离线 1-to-grade 闭环回归
- 问题：repo 学生流程（run_lab.cmd 文档路径：generate samples → lab1 pipeline → lab --lab ndvi_basics --grade）在 master 上必然 45/100（R21-E CT-6 实证，本轮现场复核仍 LIVE：规则/生成器均未变）。
- 方案（不放宽任何规则）：按 bundle overlay 的既定校准方法，在 data/labs/grading/ 新增 foundry 场景权威规则文件（新 lab id，不动被 test_lab_grading 语料钉死的 fixture 权威 ndvi_basics.rules.json）；新增本轨专属回归测试：生成样本→按官方 pipeline 定义运行→评分（新规则）→预期 ≥ passing_score PASS；CRS/网格故意错误对照必须 FAIL 且给 typed 定位（grid.compatible）；离线零网络、种子固定可重复。run_lab.cmd 的 `--lab` 参数一行改动记 CROSS_TRACK（E 轨）。
- Oracle：新测试在 master 红（当前规则对 foundry 工件 45/100）→ 实现后双绿；负控（错误 CRS）红；主机日志+digest 存档。

## WP-D2（P2）session_store 深度炸弹 typed 拒绝
- 问题：D-F1。jsoncpp 1.9.6 stackLimit 超限抛 Json::RuntimeError，readJsonFile 无 catch，异常逃逸 LabSessionStore 公共 API；注释与实现矛盾。
- 方案：try/catch (Json::Exception) → 归一化为 lab.session.corrupt typed 失败；对齐 lab_pack/grader_json/recipes 的既有纪律。
- Oracle：RED=master 上 >64 层嵌套会话文件导致异常逃逸（测试断言 typed 失败）；GREEN×2；合法深度文件不受影响。

## WP-D3 ~~（P3）recipes 无界读封顶~~ → 撤销（FIXED_OPEN）
- #1648 已由 a1ffd5c66（2026-10-19）修复（stat-before-read + 16MiB，四处现场证实）。按协议严禁重新实现已修问题；本 WP 转为回归保护（见 WP-D5）。

## WP-D4（P3）spec_runtime expected_numeric 有限性门
- 问题：D-F2。1e999→±inf 边界被接受（含 -inf/+inf 真空区间）。
- 方案：isfinite 门 + typed lab.runtime.field 拒绝（对齐 grader_types.cpp isfinite 纪律）。
- Oracle：RED=master 接受 ±inf 边界；GREEN×2；合法有限边界回归通过。

## WP-D5（P2）#1610-1613/#1678/#1602/#1593 回归保护矩阵（防 merge-loss）
- 目标：既有修复在 master 仍绿的机器证明 + 本轨新增 guard 测试集中跑（不重复实现）。
- Oracle：列出各回归测试名 + ctest 结果；若有红 → 升级为 LIVE 缺陷处理。

## WP-D6 ~~#1653 verification 技术债~~ → 撤销（FIXED_OPEN）
- 51eb7d1a6（2026-10-19）已去重 beginResult（三文件单引用 + include checks_numeric.h 现场证实）。转为回归保护。

## DEFERRED 候选（须写原因）
- D01 全量契约普查、D03 grader 数学重审、D05-D07 verification 深审、D09 pack 兼容、D10 render、D11 性能矩阵：Explore 普查显示该基线已系统性修复（0 TODO；NaN/深度/路径/确定性注释可考），且已有大覆盖测试。除非基线 ctest 出红或 R1/R2 发现新证据，否则不凭空造缺陷（协议 §2 禁止无根据臆造）。基线测试全绿证据将记录于 TEST_RESULTS.md 作为这些方向的"已保护"证明。
