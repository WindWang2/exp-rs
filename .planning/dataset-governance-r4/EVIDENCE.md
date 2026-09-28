# EVIDENCE — Track 13 R4 Dataset Governance Hardening（实测证据链，终稿）

环境：worktree `/home/kevin/projects/rs-studio/exp-rs-dataset-governance-r4`，build-gcc15（全新构建目录，gcc-15 栈，Debug，ENABLE_TESTS=ON，ninja -j2 / ctest -j1）。
证据口径注记（评审 P2-5 后修正）：**独立权威**指与被测实现无关的真值来源（hashlib、公开参考向量）；**drift pin**指从在树实现一次性生成、人工评审钉死的常数（防漂移探针，非独立 oracle）——两者分开陈述。

## 1. Oracle 双轮全绿（最终树）

命令：`ctest -R "dataset|fingerprint|split|leak|ontology" -j1`（50 个用例 = ctest 按用例级发现）
- Round 3（终树）：**50/50 Passed，exit 0**
- Round 4（终树）：**50/50 Passed，exit 0**
关键摘录（完整输出本地留档 logs/ctest_final_r3.log / r4.log，按仓约定 raw logs 不入库）：
```
100% tests passed, 0 tests failed out of 50   (round 3)
100% tests passed, 0 tests failed out of 50   (round 4)
```
首轮曾出现 `test_io_subdataset_inventory_NOT_BUILT (Not Run)`——正则误匹配的 io 域目标未构建所致，构建该目标后消失；该目标非本域代码，未改动。

## 2. 扰动矩阵：12 组字节级变体（全部 PASS）

载体 `test_dataset_fingerprint_determinism.cpp`（终树 68 断言 ×2 全绿）：
组 1-8：LF/CRLF/CR、尾随换行×2、UTF-8 BOM、BOM+CRLF、缩进、pretty/compact——字节拼写对；
组 9：**字节层**键序反转（手写平文档两序）；
组 10-12：**字节层**数字拼写（`"count":1` vs `1.0` vs `1e0`；`0` vs `-0.0`；`0.1` vs `1e-1`，对序列化文档做字节替换）。
附加行：`\uXXXX` 逃逸拼写（字节层）≡ UTF-8 字面；canonical 幂等；负例内容行（串内 `a/b` vs `a\b` 指纹必须不同）；binary/NUL/截断守卫（坏文档永不产生完好载荷指纹）。
评审修正记录：初版 9/10/12 行曾经 QJsonValue 构造（两侧字节相同 = 同义反复），已按 P1-3 改为字节级——现 12 组全部为真实字节变体。

**注入自证**（本地 logs/injection_self_proof_red.log，关键摘录）：
```
FAILED: …/test_dataset_fingerprint_determinism.cpp:240: CHECK( a.toHex() == reference )
  group: LF vs CRLF line endings
assertions: 18 | 17 passed | 1 failed      ← 注入 CRLF 截断回归（#1336 类）后
```
还原后双跑 68/68 复绿。矩阵对目标回归类（ingest 字节敏感化）有效；字节级行（1-12）按构造携带真实字节差。

**实测说明**：BOM 容忍来自 `QString::fromUtf8` 解码层（剥前导 BOM）；QJsonObject 键天然有序。canonical 单点无需源码改动——矩阵固化的是两层平台行为，比装饰性归一诚实。

## 3. 真值来源（区分独立权威与 drift pin）

独立权威：
- 指纹 KAV：手算 canonical 字节（UTF-8 字面、码元序键、最短往返小数）+ **独立 hashlib** SHA-256 = `ff5f7f56…36a2cb`，实现逐字节复现。
- SplitMix64：公开参考向量（seed 0：`0xe220a8397b1dcdaf, 0x6e789e6aa1b965f4, …`）。
半独立（按头文件钉死的算法文本离线重算，捕获"代码≠文档"分歧）：Pcg32 序列（注意 seeder 的**输出**即 state）、hashSeed/seedFor 向量。
Drift pin（在树实现一次性生成、人工钉死，防漂移非独立 oracle）：
- Golden v1 manifest 指纹 `a23dfda4…f7e9e`；
- Split 指纹 `88c811e3…937f`（跨进程常数：指纹契约排除墙钟字段）。

## 4. 确定性用例（ctest 逐一可数）

正则内新增 28 例（50 例中）：fingerprint determinism ×6、split reproducibility ×8（含跨进程 tripwire）、leakage coverage ×4（13 检查矩阵/排列不变性+证据定位/单侧对/fold honest-gaps）、version migration ×4、D19 feature-join 定序 ×1、governance consistency ×2（挂 `data` 前缀，另行验证）。
正则外（另行双跑验证）：sample/annotation fail-fast ×3（test_sample_label_annotation，127 断言×2 绿）。
回归面全绿：test_dataset_core 278、test_d19_dataset_foundry 1729、test_mlops9_split 285、test_split_leakage 683、test_stratified_split 125、test_spatial_block_leakage 218、test_contract_fuzz_ipc 3959、test_sample_fixtures 26132、test_governance_store 218、test_dataset_quality_scale 733、test_dataset_e2e_examples 57。

## 5. 修复清单（10 项）

| 缺陷 | 位置 | 提交 |
|---|---|---|
| D1 seed_hex 缺失静默落 0 | split.cpp | bdb8237c |
| C1a evidence role 跟随枚举序 | leakage_audit.cpp | 91f80127 |
| C1b pre_post 对偶重复上报 | leakage_audit.cpp | 91f80127 |
| A1 diff 向量 QHash 序 | dataset_version.cpp | 1c4f2800 |
| D-载荷 非有限坐标/负像素/空引用/非法 WKT + 无定位 | sample.cpp | a576125b |
| D-注记 未知类码静默入库 | dataset_store_samples.cpp | a576125b |
| A2 缺失必需列报告按 QSet 序 | feature_table.cpp | 0dbfb0d3 |
| L1 master 测试闭包断链（agent_loop 符号） | tests/CMakeLists.txt | 9c78f383 |
| R1-3 评审修复：CMake 护栏 / 单侧对丢报 / 矩阵字节化 | tests/CMakeLists.txt, leakage_audit.cpp, tests | c6cd8659, a27f8601, 0e0678e9 |
| R2 评审修复：wrong-type seed / split tripwire / gate 次序 | split.cpp, tests, dataset_store_samples.cpp | 8f2a41e0(d1b9bee7), cc6e1a7f 等 |

## 6. 资源红线遵守记录

全程 `ninja -j2`、`CTEST_PARALLEL_LEVEL=1`；机器共 3 条并行轨道各 -j2（16 核/62Gi），RSS 峰值 ~40Gi/62Gi（64%），未触发 -j1 降级条件。subagents 全程 2 个（只读 22 头审计 + 只读对抗评审），无递归派生。未等待线上 CI；仓库根 master 只读，全部工作在隔离 worktree。

## 7. Token 记账

见 `.planning/dataset-governance-r4/.goal-loop-ledger.md` 累计列（评审后终行）。实际消耗远低于 280M 写作预算：3.3 的"2 亿下限"按逐文件全读+逐用例全周期高价测算；本轨道以只读子代理压缩 22 头审计读入、首轮构建一次成型、无返工烧耗。抽查锚点：22 头逐行素材（API_AUDIT.md）、四大链路行号、全部 KAV 独立可复算、注入自证可复现。

---

## Round 2 (2026-09-28): post-merge verification on master a726d17a62

Scope: Round 1's deliverable is MERGED (PR #1350, merge point 42a8d0fe46). This round
re-verifies the Oracle against the master that has since absorbed four more track
merges (#1351–#1354), rather than re-delivering Round 1.

### R2.1 Master P0 found & fixed: configure was broken for all of master

- Symptom (fresh configure, build-gcc15-r2, gcc-15 stack): `CMake Error at
  tests/CMakeLists.txt:14728: Parse error. Expected a command name, got unquoted
  argument with text "=======".` — configure exit 1.
- Root cause: 7bb6398c05 ("Merge remote-tracking branch 'origin/master' into
  tmp-merge-1345") resolved the tests/CMakeLists.txt union keeping both sides but
  left one orphaned `=======` separator at the parity-r4 / verify-chain block
  boundary. Every checkout of master failed to configure the test tree since.
- Whole-tree sweeps: exact-line `^={7}$` over *.cpp/*.h/*.hpp/*.txt/*.cmake/*.sh/*.py
  → exactly this one occurrence; `^<{7} `/`^>{7} ` over ALL tracked files → none.
  (Precedent: #1294 fixed the #1293 merge's leftover markers; this one re-appeared
  from a later union merge.)
- Fix: delete the separator line (both union sides are complete); atomic commit
  1a4289a108 on hardening/r4-dataset-governance-r2. Configure after fix: exit 0
  ("Generating done", 43.6 s).

### R2.2 Fresh build (verification of record)

- build-gcc15-r2, /usr/bin/g++-15, Debug, ENABLE_TESTS=ON, ninja -j2, no pipe
  masking (exit code checked directly). Log: build-gcc15-r2/build_r2.log (local).
- Result: OK. Oracle closure built in two phases (14 domain targets, then the
  94 remaining regex-touching targets) plus test_mlops9_split /
  test_dataset_e2e_examples (domain completeness; their case names do not
  match the regex). gcc-15 ICE (segfault, qgis_gui template-heavy TUs) hit 3
  build attempts; resolved by ninja-level retries / raised stack limit
  (ulimit -s 262144) — the repo's raise-compiler-stack.sh launcher is the
  systematic fix and should be wired as compiler launcher upstream.
- RSS guard: machine never below ~28 GiB available of 62 GiB (< 50% used by
  this build); the -j1 degradation threshold was never approached.

### R2.3 Independent KAT re-verification (outside the C++ build)

- Fingerprint KAT: python hashlib SHA-256 over the exact hand-canonicalized bytes
  pinned in test_dataset_fingerprint_determinism.cpp → recomputed
  ff5f7f566493f670c7615839ce9cf14c1fe85443834bf2f9f309a80f1236a2cb == pinned. MATCH.
- Generator/seed KATs: all 22 pinned vectors of test_split_reproducibility.cpp
  (SplitMix64 seed 0 / 0xDEADBEEF; Pcg32 seed 0 / 42; hashSeed("split"/"patch");
  seedFor over 3 combos) recomputed in python from the algorithm text alone →
  22/22 MATCH. The "independent authority" claims of EVIDENCE §3 are re-backed by
  fresh out-of-tree evidence: the pins are reproducible without the repo build.

### R2.4 Oracle counts refreshed on current master

- Perturbation matrix rows in test_dataset_fingerprint_determinism.cpp: 12 (≥12).
- API_AUDIT.md: 22 data rows, disposition column non-empty on every row.
- DECISIONS.md: 7 numbered rules (≥5).
- Round-1 commit count 15e5c66b5..22c98137aa: 19 (≥16).

### R2.5 Oracle ctest double-runs (final state)

Round-1-comparable domain subset (14 domain binaries):
- Phase-1 runs (logs/ctest_r2_run{1,2}.log): 52 discovered = 50 real + 2
  `_NOT_BUILT` readiness sentinels; all 50 real tests passed, both runs.
  The sentinels matched the regex only through their NAMES
  (test_mlops9_split_NOT_BUILT contains "split"); the binaries were then
  built (readiness-gate mechanism from the perf-memory track) — their
  registered case names do not match the regex, so the surface settled at
  179.

Full regex surface (final, after building every regex-touching binary):
- logs/ctest_r2_final_run{1,2}.log: **179 tests, 178 passed, 1 failed,
  both runs identical** (exit 8 = the single failure).
- The single failure is FOREIGN to this track's ownership and pre-existing
  on master for any fresh tree: test_scientific_state_gdal "bare dataset
  projects honest unknowns and the FSM default" — the test writes its
  fixture GeoTIFF into CMAKE_SOURCE_DIR/build-rs14-passport without ever
  creating the directory (writeBareGeoTiff calls GDALCreate directly), so
  `REQUIRE(dataset)` gets nullptr on a fresh checkout. Proven: with the
  directory present the case passes (11 assertions). RS14 scientific-state
  ownership, file outside this track's whitelist — documented, not fixed
  here (same disposition as round-1's rs:temporal_sar_fusion sidecar note).

### R2.6 Latent domain defect found & fixed during verification

- test_dataset_e2e_examples "Example D" failed: `addAnnotation` refused the
  annotation because Example D built its LabelSchema in memory only and
  never `saveLabelSchema`-ed it — the Track 13 R4 ingest gate (PR #1350)
  correctly resolves the referenced schema inside the store. The binary was
  one of master's long-standing NOT_BUILT set, so the stale red was
  invisible until built. Fix 490415c1b0 (whitelisted test file): register
  the schema first; binary now 4/4 cases, 58 assertions green.
