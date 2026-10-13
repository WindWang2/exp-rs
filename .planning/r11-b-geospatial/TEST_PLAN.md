# TEST PLAN & ORACLE SPECIFICATION — Direction B (Geospatial Data Fabric & Persistence)

## 1. 目标与验证哲学

根据 Karpathy AI Coding 核心原则 4（Goal-Driven Execution & Verification），每个工作包都必须有可重复、可自动裁决的 Oracle：
- 严格遵循 **RED (可复现错误) → 根因分析 → 最小外科修复 → GREEN × 2 (连续两次全绿) → 逆向破坏性/边界断言**。
- 测试必须全部在独立构建目录 `build-dev` 下执行，`-j1` 运行，避免测试间临时文件争用。

---

## 2. 工作包验证 Oracle 细则

### WP-B3: Fabric Catalog 截断契约与死循环防御 (Oracle-B3)
- **B3.1**: Local Backend 单页截断诚实性:
  创建拥有 3 个 STAC item 的本地目录，以 `maxItems = 1` 查询 `searchPage`：
  - 预期结果: `page.records.size() == 1`，`page.next.hasMore == true`，`page.truncatedByCap == true`。
  - 既有缺陷 (RED): `page.truncatedByCap` 为 `false`（因仅检查 `itemFiles.size() >= localMaxFiles`）。
- **B3.2**: Local Backend `searchAll` 终止与防死循环:
  对上述拥有 3 个 STAC item 的本地目录，以 `maxItems = 1` 查询 `searchAll`：
  - 预期结果: 耗时 < 100ms，返回 `records.size() == 1`，`truncatedByCap == true`，循环次数 <= 2，绝对不触发 `ResourceExhausted` (100,000次保护拦截)。
  - 既有缺陷 (RED): `searchPage` 在第二页返回 0 个 item 但 `hasMore == true`，`searchAll` 无法累积新 item 也无法退出，死循环 100,000 次抛出异常。
- **B3.3**: In-Memory Backend `maxItems` 一致性:
  对拥有 5 条内存记录的 CatalogService，以 `maxItems = 2` 查询 `searchPage`：
  - 预期结果: 返回 2 条记录，`page.truncatedByCap == true`，`page.next.hasMore == true`。

### WP-B1: 数据源资格矩阵与 Direct-Open 一致性 (Oracle-B1)
- **B1.1**: `ResourceUri::parse` 原生识别 `file://`:
  - 输入 `file:///path/to/raster.tif` → `kind == LocalFile`, `path == "/path/to/raster.tif"`
  - 输入 `file://localhost/data/test.tif` → `kind == LocalFile`, `path == "/data/test.tif"`
  - 输入 `file://server/share/test.tif` → `kind == LocalFile`, `path == "//server/share/test.tif"` (UNC 映射)
  - 输入 `file:///C:/data/test.tif` (带 Windows 盘符) → `kind == LocalFile`, `path == "C:/data/test.tif"`
  - 既有缺陷 (RED): `ResourceUri::parse` 报 `Invalid` ("not a URI, URL, VSI handle or existing local path")。
- **B1.2**: `RasterReader::open` 与 `inspectRaster` 直通支持:
  - 无论是操作系统原生路径还是 `file://` URI，`RasterReader::open(uri)` 和 `inspectRaster(uri)` 必须自动经由规范化映射打开数据集，与 `fileExists()` 行为严格一致，消除 OpenFailed。

### WP-B6: 发布与原子恢复 (Oracle-B6)
- **B6.1**: 崩溃与目标冲突恢复:
  - 主文件发布阶段故意注入异常（例如目标为不可覆盖的占位目录），验证所有已就绪的 sidecar 严格回滚为原本的旧内容，无残留临时文件与 `.bak` 遗留。
- **B6.2**: Stage Ledger 孤儿扫描与附加检查:
  - 模拟驱动不匹配、尺寸不匹配、阶段文件损坏，`attachExisting` 精确报告 `driver_mismatch`、`shape_mismatch`、`staged_unopenable`。

### WP-B2: 远程 COG 与离线网络边界 (Oracle-B2)
- **B2.1**: IPv4-mapped IPv6 SSRF 拦截:
  - 针对 `http://[::ffff:127.0.0.1]/data.tif`、`http://[::ffff:169.254.169.254]/meta` 等变种，请求过滤必须识别为私网/回环地址并拒绝。
- **B2.2**: 离线模式零出站:
  - `offline::setEnabled(true)` 时，任何远程调用直接抛出 `GeoError(ErrorCode::NetworkError)`，0 次真实 socket 出站。

---

## 3. 回归与自动化执行规范

1. 构建与测试指令：
   ```bash
   flock -w 600 /home/kevin/projects/rs-studio/.build_lease.lock cmake --build build-dev --target <target> --parallel 2
   ctest --test-dir build-dev -R <target_pattern> --output-on-failure -j1
   ```
2. 关键套件通过标准：100% 通过（0 失败，0 泄漏）。
