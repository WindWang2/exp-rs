# GOAL — Direction B: 存量空间数据链路、块处理与可恢复持久化

- **项目**: `WindWang2/exp-rs`
- **Worktree**: `/home/kevin/projects/rs-studio/exp-rs-r11-b-geospatial`
- **分支**: `hardening/r11-b-geospatial-data`
- **基线 HEAD (master)**: `8a08642760e2e8ffd2c4b965fc4683c01d3e4758`
- **使命**:
  围绕已有 GDAL/QGIS 地理空间数据源、COG/栅格 I/O、Fabric Catalog/Cube、块处理执行设施以及数据集与实验持久化机制，进行可靠性、边界收敛和可恢复性加固。
  坚持“不误读、不静默截断、不丢数据、可恢复、可度量”原则。
  **绝对不增加第二套 DataManager、TileCache、Catalog 或调度器**，依托现有单权威架构收敛缺陷。

---

## 1. 核心验收 Oracle (Objective Exit Criteria)

1. **缺陷闭环纪律**:
   每个选定工作包（WP）均遵循：**故障注入/边缘用例 RED → 根本原因分析 → 最小外科手术式修复 → GREEN × 2 → 逆向/故障注入验证**。
2. **截断与终止契约 (WP-B3)**:
   - `CatalogService` 在无论 `mLocal`、`mMemory` 还是 `mRemote` 下，只要返回集因 `query.maxItems` 达到上限而停止，`truncatedByCap` 必须如实报告 `true`，杜绝假阴性。
   - `searchAll` 针对 `mLocal` 和 `mMemory` 彻底消除空页死循环与 `ResourceExhausted` 异常。
3. **数据源直通与归一化一致性 (WP-B1)**:
   - `ResourceUri::parse` 原生支持 `file://`、`file://localhost`、UNC `file://host/share` 以及 UTF-8/URL 编码路径。
   - 栅格只读打开（`RasterReader::open` / `inspectRaster`）保证与存在性检查（`fileExists`）具有完全一致的归一化路径，彻底消除“存在性检查通过但 direct GDALOpen 喂原始 URL 失败”的历史风险。
4. **发布与原子恢复 (WP-B6)**:
   - 模拟进程崩溃、目录冲突、只读占用、磁盘写满场景，发布失败时目标保持原子状态（旧版本完整保留或新版本发布成功，禁止残缺状态）。
   - `stage_ledger` 记录在 attach 与 recover 过程中能够正确识别并清理孤儿临时文件与悬挂账本。
5. **安全与离线边界 (WP-B2)**:
   - 远程 COG 与 HTTP 请求在 `offline::isEnabled()` 时确保 0 次外部网络 egress。
   - IPv4-mapped IPv6 私有地址严格拦截，杜绝 SSRF 绕过。
6. **资源与生命周期 (WP-B4 / WP-B5)**:
   - GDALDataset 句柄与 band/window 在异常路径下 100% 通过 RAII 释放，无内存泄漏与句柄泄漏。
7. **独立审查门禁**:
   - 独立 Reviewer 对完整 `git diff origin/master...HEAD` 执行两轮逆向代码审查。
   - **P0 = 0，P1 = 0** 为生成最终可交付 PR 的硬性前提。

---

## 2. 严禁事项与边界隔离

- 严格遵守整机 `-j2` 编译限制（`CMAKE_BUILD_PARALLEL_LEVEL=2`，Ninja `-j2`，`CTEST_PARALLEL_LEVEL=1`），编译操作必须在 `flock /home/kevin/projects/rs-studio/.build_lease.lock` 保护下串行执行。
- 绝不改动 PR #1475 已经修改的 12 个文件（见 OWNERSHIP.md），不抢修其 PR 分支。
- 绝不跨界修改 Track A (`src/analysis/**`)、Track C (`src/processing/framework/task_center.*`)、Track D (`src/app/**`) 的代码。
- 绝不 force-push master，不更改线上 CI 配置文件。

---

## 3. 工作包路线 (Work Package Sequence)

- **Epoch 0**: 基线环境就绪、Ninja 构建验证、规划矩阵生成。
- **Epoch 1 (WP-B3)**: Fabric Catalog / Query 截断契约与分页死循环根治。
- **Epoch 2 (WP-B1)**: 数据源资格矩阵、`ResourceUri` 对 `file://` 原生支持与 Direct-Open 路径统一。
- **Epoch 3 (WP-B6)**: 发布与恢复原子性、Stage Ledger 故障恢复与多成员还原。
- **Epoch 4 (WP-B2)**: 远程 COG 边界、IPv4-mapped IPv6 过滤与离线零出站验证。
- **Epoch 5 (WP-B9)**: 实验/复现包归档安全性与跨平台路径可移植性契约。
- **Epoch 6**: 双轮独立 Review 逆向门禁核验与修复。
- **Epoch 7**: Master 重放验证、最终审计与 PR 交付。
