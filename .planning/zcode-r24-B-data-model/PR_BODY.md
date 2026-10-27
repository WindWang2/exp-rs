# r24-B: 数据资产 / EO 模型推理 / 实验复现 — 稳定性收敛（P1×3 + P2×7 + #1581 lineage 重挂闭环，双轮独立评审）

**Baseline** `5fe642ec6339807823aa56612ba94668592a738d`（= origin/master，含 #1716/#1717）
**Head** `c7d6b7598…`（7 个实现/测试 commit + 1 个规划文档 commit）
**平台**：Windows 本机实跑（MSVC 2022 / Ninja -j2 / RelWithDebInfo / vcpkg + Qt 6.8.0）。Linux/macOS：NOT_EXECUTED。

## 侦察结论（不重做已合并工作）

对 23 个 R24-B 所有权区域开放 Issue 逐条实码验证：**全部 FIXED_IN_MASTER**（sweep commits `987161c25/9b42bc93f/d9f1b886f/831f8d600/c8ba51990/8e0430262`；R22 #1710/#1709 修复在位）。证据：`.planning/zcode-r24-B-data-model/ISSUE_TRIAGE.md`。本轨全部改动来自：(a) 4 个 fresh-context 对抗审查代理找出的**未开 Issue 的 LIVE 缺陷**；(b) 代码注释自认的残余缺口（#1581 lineage 孤岛）；(c) 覆盖空缺。

## 修复清单（每项均有 RED→GREEN 证据）

| # | 级别 | 缺陷 | 修复 |
|---|---|---|---|
| 1 | **P1** | 检测 lane 把声明式 NoData 哨兵（如 -9999）原样喂模型——raster/scene lane 均先转 NaN，detection lane 漏了该步（`detection_tile_engine.cpp` 注释自认但未实现）；带 scale/mean-std 归一化时成为巨幅离群值，静默劣化 | 逐带收集 `bandNoDataValue` → 哨兵→NaN→0，与 raster lane 逐位一致。RED：基座代码 fed mean=-4899.5；GREEN：100.0（8 断言×2 用例） |
| 2 | **P1** | tile 引擎（单/多输入 lane）末 tile 全 NoData 时，有效尾批被 `continue` 绕过 batch-full 检查且无循环后 flush——最后一条有效条带静默丢失（Labels 产品错标 0 类 / 羽化下变 NoData） | 两 lane 循环后无条件 flush（仅在 bug 场景可达）。RED：禁用后 2/819 断言失败（x64..95 全 0 类）；GREEN 819/819 |
| 3 | **P1** | DatasetStore 六个写路径（addSamples/removeSample/addAnnotation/addSourceAssetsToDraft/stageVersion/commitVersion）的可变性门在 BEGIN IMMEDIATE **之外**——跨连接 check-then-act：样本冻结进已提交版本 / 陈旧清单覆盖并发资产编辑（staged 门被绕过）/ addSourceAssetsToDraft 0 行更新静默返回成功 | 全部移入事务内（#811 facets 范本），UPDATE 加 `staged=1`/`changes()==1` 守卫。RED：racer 基座 0/2（both-succeed+stale-commit 出现）；GREEN 2/2（40 轮双连接 barrier racer，两种合法排序均出现） |
| 4 | **P2** | `#1581` 残余：reproduction bundle lineage 切片丢边方向 → 导入 run 成谱系孤岛（修复时自认的 limitation） | 导出 from_*/to_* 方向保持（兼容 other_*）；导入校验（词表/字符集/自环/唯一 run 端点/外 run 拒绝/256 上限）→ run-id 改写重挂 → 实验侧+可选 DatasetStore 数据集侧；legacy 3 键维持命名缺口。RED：基座导出 2/4 失败；GREEN 4/4（含敌意项跳过与 de-islanding 遍历） |
| 5 | P2 | eo_preflight 波长窗在 band_roles 缺席时静默不执行（模型自身的灵敏度声明消失） | 命名 advisory；角色超出显式选带不再检查未喂 band |
| 6 | P2 | `WAVELENGTH_UNITS` 被忽略（0.665µm 当 0.665nm 拒绝合法运行） | 单位归一（nm/µm/mm/cm/m），未知单位=缺失（不猜测） |
| 7 | P2 | 发布栅栏只覆盖 swap 窗口：固定 stage 名跨进程/跨线程互毁；栅栏本身仅进程内 | 唯一 stage 名 `<final>.<pid>x<n>.tmp~` + 年龄门控 sweep（1 天，含隐藏族与 .morph~）；**QLockFile 跨进程栅栏**（本地活 pid 永不抢占，异机 6h 兜底）；栅栏拒绝时履行 stageForCleanup 契约 |
| 8 | P2 | leakage_audit `bucketOf` qint64 转换 UB（极端坐标入垃圾桶，空间检查静默漏比对） | #875 式护栏 + `unbucketed_count` 量化（-1=未运行；summary/toJson/fromJson 往返） |
| 9 | P2 | `open()` 把 schema 门读取失败当新库 → INSERT OR REPLACE 倒写（可能更新的）版本戳 | 门读取三态，读失败拒绝打开 |
| 10 | P2 | MSVC `bad_alloc::what()`="bad allocation" 不匹配 OOM 针词 → Windows 上主机侧 OOM 走 Unknown，OOM 阶梯失效 | 补针词 |
| 11 | P3 | mirrorAllAssets ghost-remove `(void)` 丢结果 / addSamples 回滚后取错误文本 / commit step-失败与 0 行混同 / 陈旧注释等 | 计数入失败通道、先取后回滚、消息拆分 |

## 新增测试（8 套件 18 用例，`tests/test_r24_b_*.cpp`，注册块 `# BEGIN R24-B` append-only）

uri_percent（'%' 正例，GAP-G1）· leakage_bucket（UB 护栏+损坏库拒绝）· lineage_reattach（方向/改写/legacy/敌意/数据集侧/de-islanding）· dataset_txn_gate（双连接 racer×80 轮）· detection_nodata（哨兵 parity+对照）· eo_preflight（advisory+单位归一）· publish_fence（唯一名+年龄门控 sweep+跨进程锁+拒绝清理）· metamorphic（tile size/羽化/重跑不变性 683 断言 + 尾批回归）。

**metamorphic oracle（O-B3）**：逐像元 argmax 分类器下，tileSize {16,40,33} × {无羽化, halo 6-8 羽化} 六变体输出 byte-identical（96×64，尾块/跨界类边界）。O-B2 敌对面：非法 URL 解析拒绝、损坏 GeoTIFF 库拒绝、超维度坐标 UB 护栏、栅栏竞争 typed AlreadyRunning、敌意 lineage 项命名跳过、schema 篡改拒绝。

## 实跑证据（命令+退出码）

- `ctest --test-dir build-r24_b -R r24_b_* -j1`：**18/18 通过 × 2 轮**（0 失败）。
- RED×5 项：外科回退各自修复→失败→恢复→通过（详 `TEST_RESULTS.md` 表格与数字）。
- 构建闭包：8 目标 + 依赖 3515 编译单元 BUILD_EXIT=0。
- 合并注册门：`python scripts/check_merge_registration_integrity.py --require-refs --base origin/master --head HEAD` → **1049 targets（base 1041 + 8 新增），lost 0，markers 0，duplicates 0，exit 0**。
- `git diff --check`：干净。
- 性能（5 次实测，`PERF_BASELINE.md`）：四套件 wall 方差 ≤7%，**零加速宣称**；base 对照与 peakRSS 如实标 NOT_EXECUTED/NOT_CAPTURED。

## 双轮独立评审

- **R1**（fresh 上下文，全量 diff + 21 TU 语法级编译）：P0×1（finalRunId 空——重挂功能 DoA）+ P1×1（#2 尾批）+ P2×4，全部修复；`REVIEW_R1.md`。
- **R2**（换上下文，R1 修复 8/8 CONFIRMED + merge-loss/跨平台/调用链/测试健壮性专项）：P1 测试缺陷×1（损坏页打偏）+ P2×1 + P3×4，全部修复或记录；`REVIEW_R2.md`。
- 评审已知残留（记录不修，理由在评审文档）：QLockFile 非竞争失败的消息精度（现网不可达）、重挂远端点不做存在性检查（loose-refs 设计）、事务内 CPU 工作加宽写锁（busy_timeout 兜底）。

## 跨轨说明

开放 Issue 清单 ~90 条中 R17 系列全部已被既往 PR 修复（Issue 状态未同步）——`CROSS_TRACK_FINDINGS.md` X1，供其他轨避做重复工作。`tests/CMakeLists.txt` 仅末尾追加带标记块。

## 风险

未跑平台：Linux/macOS（评审确认代码面跨平台 clean：QDir::Hidden/QLockFile/setFileTime/portable::pid/UTF-8 µm）。`test_dataset_core` 等含 UTF-8 测试名的套件在 GBK 代码页宿主经 ctest -R 匹配会 mojibake（宿主代码页问题，与 diff 无关；exe 直跑通过）。
