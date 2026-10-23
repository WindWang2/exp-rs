# R20-02 ORACLES — 失败预言与断言明细

## O1 推理几何/语义（F1/F2/F4，tests/test_model_runtime.cpp）
- **F1** `tile engine completes a non-divisible raster with batched edge tiles`
  - RED 机制：master 的 flushBatch 在混批时抛 InvalidInputData（100×100、tile 64、batch 4 → 首 4 tile 窗口 64×64/36×64/64×36/36×36 混批）→ 未捕获异常 → 用例失败。
  - GREEN 断言：tilesProcessed==4、outWidth/Height==100、全幅 10000 像素逐点 == 输入 ramp（margin 1e-3）——防"绕过失败但错位/丢边"的弱修复。
- **F2** `uncertainty band reports NoData as NaN, never zero confidence`
  - RED 机制：类别带 1 在双带全 sentinel 像素恢复 NaN，但 unc 带计算得 0.0 → mismatchedConfidence==2048 ≠ 0。
  - GREEN 断言：outBands==3；NaN 像素数 == 2×32×32（两象限，防全图 NaN 的假绿）；valid==2×32×32；mismatchedConfidence==0。checkerboard 相位确定性已核实（builder (x/32+y/32)%2）。
- **F4** `declared output.uncertainty refuses when no head has two channels`
  - RED 机制：master 静默发布单带产品 → REQUIRE_THROWS_AS 失败。
  - GREEN 断言：RSOperatorError；输出路径不存在（refuse-before-staging）。

## O2 manifest fail-closed（F3，tests/test_model_catalog_v2.cpp）
- 并入既有 #646 表驱动用例（"manifest fields that the runtime does not execute are rejected"）：
  - `bad-interpolation`（"bicubic"）与 `bad-interpolation-case`（"Nearest"）→ InvalidManifest + reason 含 "interpolation"
  - `bad-resize-token`（"to-input" 拼写错）→ InvalidManifest + reason 含 "resize"
  - 既有 good-tiling 继续非 Invalid（防过度拒绝）。

## O3 快照原子性（F5，tests/test_workspace_services.cpp）
- 触发：gov.db rename 走 + 原路径 mkdir（打开的 SQLite 句柄随 inode 继续服务 checkpoint；QFile::copy 无法读目录；root 免疫）。
- RED 机制：master 拷贝失败不检查 → report.ok==true → CHECK_FALSE 失败。
- GREEN 断言：ok==false；error 含 "governance"；snap-*.snapshot 目录被清理（无半快照）。

## O4 内容地址自愈（F6，tests/test_fault_injection.cpp）
- RED 机制：对象截断为 "TORN" 后再 put → master 走 mismatch 分支返回 nullopt → REQUIRE( second ) 失败。
- GREEN 断言：second 有效、digest/poolPath 与 first 相同、size 恢复为 payload 尺寸、字节逐位等于 payload（防"返回成功但对象仍坏"）。

## 通用边界（适用项）
- 空输入：F4 用最小 64×64 单带；F1 含 0-halo 与非 0 商两种几何。
- 错误类型：F3/F4 均 typed（InvalidManifest / RSOperatorError），非崩溃。
- 回退与旧版兼容：good-tiling 阳性钉住词表校验不过度拒绝；既有 27 个随库 manifest 的模型库扫描测试（test_model_library_manifests）作为全量回归。

## 重复/恒真防护
- F1/F2 GREEN 均带逐像素数值断言（非仅"不抛"）。
- F2 含 NaN 计数上/下界（防全 NaN 或零 NaN 的恒真）。
- F6 含字节级 readAll 比对（防仅返回结构无内容）。
