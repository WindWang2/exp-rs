# PR_BODY 草稿 — hardening/r9-science-data-integrity

## Summary
方向 A（遥感算法/科学数据/实验复现性收敛）第九轮加固：收口 6 条已合并 PR（#1469/#1472/#1473）遗留的**未解决 inline review threads**（均经主 Agent 源码复核 + 独立 reviewer 二次核实），以及开放 Issue 复核后确认 STILL_OPEN 的 3 个子项（#1456/#1450/#1457 尾巴）。不重复 #1469-#1474 已修内容（去重矩阵见 .planning/r9-science-data-integrity/PR_ISSUE_REVIEW_MATRIX.md）。

## 修复清单（Bug→RootCause→Repro→Fix→Regression）

1. **radiometric POSIX 冒号路径误判 URI**（#1469 review @ radiometric_state.cpp；关联 #1467/#1450）
   - RootCause: layerLocalFilePath 把「非盘符冒号」一律当 provider URI；POSIX 合法路径 `/data/acq:2026.tif` 跳过辐射元数据读写，reopen 后回落 DigitalNumber，可放行已定标数据二次定标。
   - Fix: 提炼 `local_source::isPlainLocalPath`（header-inline、QtCore-only，保持 eo_preflight 无 qgis_core 链接边）；冒号在首个分隔符**之后** ⇒ 路径内合法冒号；歧义形态 fail-closed。
   - Repro: 独立复现器（MSVC /std:c++20）master 逻辑 5 例失败、新规则 17/17（.planning/.../red_repro_w1_colon_path.cpp，exit 0）；in-tree 回归 17 向量 + Q_OS_UNIX 端到端冒号文件段。
2. **model manifest 拒收 SAR 定标态**（#1472 review；#1450）
   - Fix: kStates + 错误文案 + 头文件文档加 sigma0/beta0（生产拼写派生，有意排除 derived-product 拼写）。
   - Repro: validateManifestJson("sigma0"/"beta0" enforced) 在 master 返回非空 issues（red），修复后空。
3. **file://localhost 伪 SMB 映射**（#1472 review @ qgsdatasourceresolver；伴随测试平台感知化）
   - RootCause: Qt 6.8 `toLocalFile` 把一切非空 host（含 localhost）折成 `//host/path`，localPath 的早退分支放行 → 本地存在性检查失败。
   - Fix: localhost 先剥离 host 再走 toLocalFile（两平台统一；无 path 回退原文）。reviewer 用 pin 定 Qt 6.8.0+MSVC 探针实测证伪「Windows 特判 localhost」假设后采用本方案。
4. **temporal 通用参数与共享 fileExists 不吃 file://**（#1472 review）
   - Fix: temporal_workspace 通用候选循环镜像 scenes 循环经 localPath()；rs_json_params::fileExists 路由 QgsDataSourceResolver::localPath()（跨界最小：B 侧共享文件 1 处，仅放宽接受 file://，无契约语义变化，供 B 串行集成）。
5. **VerifyFailed 后吞掉 assetChanged**（#1473 review @ data_manager；#1457）
   - RootCause: durable_sidecar VerifyFailed = 新字节已 rename 上目标但读回失败；布尔判断当普通失败处理，不发 assetChanged → 显示停留旧 VRT 而磁盘已变。
   - Fix: status 区分——VerifyFailed 仍 emit assetChanged + `relocate.dependent_vrt_verify_failed` Error 诊断；pre-publish 失败维持原 Warning+不发信号。SICNU_FAULT_POINT 测试缝（沿 dataset_store 先例）。
6. **removeSample prepare 错误被 ROLLBACK 重置**（#1473 review @ dataset_store_samples）
   - Fix: 两个 prepare-failure 分支 capture-before-rollback（step 分支既有契约）。Repro: DROP TABLE 双 SECTION 注入，master 上诊断读 "not an error"（red）。
7. **fabric planner 截断信号覆盖**（#1456 STILL_OPEN）
   - Fix: `truncated = walk截断 || selector截断`；maxItems≤sceneBudget 两形态恢复诚实报告（master red：false）。
8. **suitability digest 绕过单权威**（#1450 STILL_OPEN）
   - Fix: 两处 contentDigest 改经 canonicalizeJsonRfc8785（execution/dataset fingerprint 同一权威）；旧 Qt-Compact digest 本就无法经 canonicalizer 复核（fail-closed 迁移语义，PR 标注）。
9. **stepFailed 收口尾巴**（#1448/#1457 STILL_OPEN）
   - facets distribution/crossCounts（Result 通道）→ typed 失败；facetNames/splitManifestsForVersion/leakageReportsForSplit（通道-less）→ R4 先例 loud-subset qWarning；workspace_catalog：Stmt 补 stepFailed/error()，open() schema 探测 step 失败 **fail-closed**（原会落进 createSchema 旧 schema 覆写），page()/count()/byId/byPath/readAsset 水化补警告。

## Review
- 第一轮：subagent ②（未参与实现，含独立 Qt 6.8.0 探针实测）：P0×2（均已修：W1 测试坏转义、W3 Windows 分支假设证伪重写）、P1×1（resources/icons 不入提交，staging 显式排除）、P2×7（已修 4 项，3 项记录）。
- 第二轮 delta review：（待填）

## Evidence
（待填：编译命令/退出码、定向 CTest 套件×2 遍、通过数）

## Cross-boundary
- B 侧共享文件最小变更：model_catalog.cpp/.h（kStates 2 token）、rs_json_params.cpp（fileExists 路由）。
- 相邻问题记录不修：rs_classification_pipeline.cpp:165（A 相邻，同型 VerifyFailed 布尔吞掉，需改 bool→状态返回契约）；agent_loop/session_journal.cpp:62、agent/harness/evidence.cpp:60（B）；asset_state_provider 无锁读写（science_context，C 协调归属）；SHA-256 fork 合并与 spatial_split 分层债（结构性）。

## Issues 对照
- 关联：#1469 #1472 #1473（review threads 收口）、#1467 #1456 #1457 #1450 #1448 #1449 #1447 #1461 #1458 #1464（子项核对矩阵；多数 ALREADY_FIXED_IN_MASTER 不重做）
- 明确不重做：见 PR_ISSUE_REVIEW_MATRIX.md 全表

## Notes
- base/head SHA、双次验证数据、未验证范围（POSIX lane 冒号文件端到端、localhost POSIX 分支 → NOT_VERIFIED(Linux)，以平台感知断言+代码证明替代）
- 未等待线上 CI
