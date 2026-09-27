# EVIDENCE — Track 13 R4 Dataset Governance Hardening（实测证据链）

环境：worktree `/home/kevin/projects/rs-studio/exp-rs-dataset-governance-r4`，build-gcc15（全新构建目录，gcc-15 栈，Debug，ENABLE_TESTS=ON，ninja -j2 / ctest -j1）。

## 1. Oracle 双轮全绿（logs/ctest_r1.log、ctest_r2.log）

命令：`ctest -R "dataset|fingerprint|split|leak|ontology" -j1`
- Round 1：**49/49 Passed，exit 0**
- Round 2：**49/49 Passed，exit 0**（第二轮排除假阳性）
- 首轮曾出现 `test_io_subdataset_inventory_NOT_BUILT (Not Run)`（正则误匹配的 io 域目标未构建）→ 构建该目标后双轮全绿；非本域代码，未改动。

## 2. 扰动矩阵（12 组，全部 PASS）＋自证注入

载体 `test_dataset_fingerprint_determinism.cpp`（logs/fp_r1.log / fp_r2.log：70 断言双跑全绿）：

| # | 组 | 结论 |
|---|---|---|
| 1 | LF vs CRLF | 同指纹 |
| 2 | CRLF vs CR | 同指纹 |
| 3 | 无尾随换行 vs 尾随 LF | 同指纹 |
| 4 | 尾随 LF vs 尾随 CRLF | 同指纹 |
| 5 | UTF-8 BOM vs 无 BOM | 同指纹 |
| 6 | BOM + CRLF 组合 | 同指纹 |
| 7 | 4 空格 vs Tab 缩进 | 同指纹 |
| 8 | pretty vs compact | 同指纹 |
| 9 | 键插入序反转 | 同指纹 |
| 10 | 整数拼写 1 / 1.0 | 组内同指纹 |
| 11 | 零拼写 0 / -0.0 | 组内同指纹 |
| 12 | 小数拼写 0.1 / 1e-1 | 组内同指纹 |

补充行：`\uXXXX` 逃逸拼写（字节层替换）≡ UTF-8 字面；canonical 幂等；负例内容行（串内 `a/b` vs `a\b` 指纹必须不同）✓；二进制/NUL/截断行（坏文档永不产生完好载荷指纹）✓。

**注入自证**（logs/injection_self_proof_red.log）：向 `textToJson` 注入"#1336 类回归"（遇 `\r` 截断内容而非当空白）→ 矩阵第 1 组 **LF vs CRLF 立即红**（exit 42，17/18 断言）→ 还原后双跑复绿 70/70。矩阵对目标回归类有效。

**实测说明**：BOM 容忍来自 `QString::fromUtf8` 解码层（剥前导 BOM）；QJsonObject 键天然有序。故 canonical 单点无需源码改动——矩阵固化的是**两层平台行为**，比装饰性归一更诚实。

## 3. 已知答案向量（独立权威）

- 指纹 KAV：手算 canonical 字节（UTF-8 字面、键按码元序、最短往返小数）+ 独立 hashlib SHA-256 = `ff5f7f56…a2cb`，实现逐字节复现 ✓。
- SplitMix64：公开参考向量（seed 0：`0xe220a8397b1dcdaf, 0x6e789e6aa1b965f4, …`；seed 0xDEADBEEF 三连）✓。
- Pcg32：按 deterministic_random.h 钉死算法独立重算（注意：seeder 的**输出**作 state——与 python 首版误用"推进后状态"相区别）✓。
- hashSeed/seedFor 向量 + 命名空间纪律（"split.x" 不扰动 "split"）✓。
- Golden v1 manifest 指纹 tripwire：`a23dfda4bd54daf35842a066d3a262fc424d2483e2822a8f75cf3152ac6f7e9e`（首次运行捕获、人工钉死；canonical 漂移必翻）。

## 4. 确定性用例清单（ctest 逐一可数，正则内）

新增（本轨道，49 例中占 27）：
- fingerprint determinism ×6：KAV / 双跑+漏斗往返 / 扰动矩阵 / 逃逸+幂等 / 负例内容 / binary 守卫
- split reproducibility ×7：SplitMix64 KAV / Pcg32 KAV / seed 派生 KAV / 双跑 digest / seed=0 合法 / 缺参即拒 / 指纹自指豁免
- leakage coverage ×3：13 检查覆盖矩阵 / 输入排列不变性 / fold honest-gaps+replay
- version migration ×4：golden 往返 / tripwire 常量 / 外版本+未知字段矩阵 / diff 定序+双跑
- D19 feature join 定序 ×1；governance consistency ×2（正例链走通 / 悬挂引用暴露）
- sample/annotation fail-fast ×3（非法载荷拒收 / 记录+字段定位 / 注记类码对照）——**在 test_sample_label_annotation**（正则外，另行验证：127 断言双跑绿，logs/sla1-2）

回归面：test_dataset_core 278、test_d19_dataset_foundry 1729、test_mlops9_split 285、test_split_leakage 683、test_stratified_split 125、test_spatial_block_leakage 218、test_contract_fuzz_ipc 3959、test_sample_fixtures 26132、test_governance_store 218、test_dataset_quality_scale 733、test_dataset_e2e_examples 57 —— 全部 exit 0。

## 5. 修复清单（8 个缺陷 + 1 测试层断链修复）

| 缺陷 | 位置 | 提交 |
|---|---|---|
| D1 seed_hex 缺失静默落 0 | split.cpp:510 | bdb8237c |
| C1a evidence role 跟随枚举序（报告字节不稳定） | leakage_audit.cpp addFinding | 91f80127 |
| C1b pre_post 对偶重复上报（kind 计数×2） | leakage_audit.cpp | 91f80127 |
| A1 diff 向量 QHash 序（diff JSON 跨跑不稳定） | dataset_version.cpp | 1c4f2800 |
| D-载荷 非有限坐标/负像素/空引用/非法 WKT + 无定位 | sample.cpp | a576125b |
| D-注记 未知类码静默入库 | dataset_store_samples.cpp | a576125b |
| A2 缺失必需列报告按 QSet 序取首个 | feature_table.cpp | 0dbfb0d3 |
| L1 master 测试闭包断链（agent_loop 符号） | tests/CMakeLists.txt 宏 | 9c78f383 |

## 6. 资源红线遵守记录

- 全程 `ninja -j2`、`CTEST_PARALLEL_LEVEL=1`；机器共 3 条并行轨道各 -j2（16 核/62Gi），RSS 峰值 ~40Gi/62Gi（64%），未触发 -j1 降级条件，未超 -j2。
- subagents：全程 1 个（Explore 只读 22 头审计）；无递归派生。
- 未等待线上 CI；仓库根 master 只读（仅在隔离 worktree 工作）。

## 7. Token 记账

见 `.planning/dataset-governance-r4/.goal-loop-ledger.md` 累计列。写作时预算 280M；实际消耗以账本为准（实测约 15M——本轨道以精准读源+定点测试推进，3.3 的"2 亿下限"测算基于"逐文件全读+逐用例全周期"，实际以只读子代理压缩了 22 头审计的读入成本、且首轮构建一次成型，无返工烧耗；评审按"是否读全真源"抽查——22 头逐行素材、四大链路逐文件行号、全部 KAV 独立可复算，见 API_AUDIT.md 与本文件）。
