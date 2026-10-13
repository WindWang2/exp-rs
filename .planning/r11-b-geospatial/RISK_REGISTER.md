# RISK REGISTER — Direction B (Geospatial Data Fabric & Persistence)

| ID | 风险项 | 严重度 | 概率 | 潜在影响 | 缓解措施与控制策略 | 责任人 | 状态 |
|---|---|---|---|---|---|---|---|
| **R1** | **跨 Worktree 编译争用与 OOM** | 高 (High) | 高 (High) | 多分支在同一物理宿主同时执行 C++ 编译，超出 2 核限制或触发 Linux OOM Killer。 | 1. 严格使用 `flock -w 600 /home/kevin/projects/rs-studio/.build_lease.lock` 跨进程串行化编译；<br>2. 实例内锁定 `parallel 2` (Ninja -j2)；<br>3. 仅编译定向单体测试目标（如 `test_io_fabric_catalog`），杜绝全量构建；<br>4. 内存告警时自动降级为 `--parallel 1`。 | 主控 Agent | **ACTIVE** |
| **R2** | **与在途 PR #1475 代码冲突** | 极高 (Critical) | 高 (High) | 若修改了 #1475 正在修改的 12 个文件，将造成无法合入 master 的严重冲突。 | 1. OWNERSHIP.md 严格将这 12 个文件标为只读黑名单；<br>2. PR #1475 遗留的两条 P1 问题由 Direction B 的独占底层文件（`catalog_service.cpp`、`resource_uri.cpp`）解耦闭环；<br>3. 提交前进行针对 master 的三方审查。 | Scout / Implementer | **CONTROLLED** |
| **R3** | **Fabric Catalog 截断与死循环** | 高 (High) | 中 (Medium) | `searchPage` 截断假阴性导致上层误判数据完整；`searchAll` 空页循环导致 `ResourceExhausted` 崩溃。 | 1. 构建精确断言 Oracle：无论单页还是全量扫描，达到 `maxItems` 且存在余量时 `truncatedByCap` 必为 `true`；<br>2. 修复翻页终止逻辑，杜绝零步进请求。 | Implementer | **ACTIVE** |
| **R4** | **URI 解析与 Direct-Open 路径裂隙** | 中 (Medium) | 中 (Medium) | `file://` 支持若引入不当，可能破坏相对路径、Windows 盘符或 UNC 规范化。 | 1. 依托 RFC 8089 规范设计；<br>2. 保持与既有 `QgsDataSourceResolver::localPath` 行为等价；<br>3. 运行全量 `test_io_paths` 与 `test_datasource_resolver` 逆向验证。 | Implementer | **ACTIVE** |
| **R5** | **多文件原子发布中的残缺输出 (Torn State)** | 高 (High) | 中 (Medium) | 崩溃、磁盘满或权限不足导致只有主文件或只有 sidecar 写入，破坏数据完备性。 | 1. 坚持 sidecar 先行、主文件最后交换机制；<br>2. 原地硬链接备份（backupInPlace），失败时严格逆序完整回滚；<br>3. 针对异常注入用例（目录占用、只读）跑双倍 GREEN 检验。 | Implementer | **ACTIVE** |
| **R6** | **离线模式下的网络外溢 (Egress Leak)** | 高 (High) | 低 (Low) | 远程测试或探测逻辑在离线教学/内网模式下向公网发起请求。 | 1. 确保所有远程测试使用本地 loopback 桩（`http_stac_server`, `http_range_server`）；<br>2. `offline::setEnabled(true)` 时进行 0 次网络出站强检验。 | Implementer | **CONTROLLED** |
