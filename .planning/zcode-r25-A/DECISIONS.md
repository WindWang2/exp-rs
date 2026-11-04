# R25-A DECISIONS（无人值守显式记录）

- **D1 账本归属**：不写共享 `.goal-loop-ledger.md`（五轨争写面）；本轨账本 = `.planning/zcode-r25-A/LOOP_LEDGER.md`。
- **D2 构建纪律**：Linux/Unix Makefiles、Debug+ENABLE_TESTS=ON、`CMAKE_BUILD_PARALLEL_LEVEL=2`、共享锁 `flock $HOME/.cache/exp-rs-build.lock`（实测与 R25-C 轨互斥排队）、ctest -j1、QT_QPA_PLATFORM=offscreen。
- **D3 候选 Issue 全部 FIXED_IN_MASTER**：任务书候选（#1538/#1550/#1631/#1646/#1687）逐项代码复核为已修 → ≥4 新缺陷改为自证：R24-A CROSS_TRACK 三留痕候选 + 本轨 W3 深审新发现（波长单位×2、SID、apply_mask 元数据保真），共 6 缺陷 + 1 深化，超额不凑数。
- **D4 DEF-2 修复形态**：内核 uint64 精确计数（`flowAccumulationCounts` 新 API；float 重载保持 #783 哨兵契约）；算子 flow_accumulation 产品改 Float64（writeTileRaw typed-block 路径）并上报 maxAccumulation。≤2²⁴ 消费面数值不变（metamorphic 钉死）。
- **D5 DEF-5 语义边界**：仅「完全不相交支撑」返回 +inf；「部分重叠零带」保持既有 pin 契约（test_spectral_classification.cpp:712 显式钉住），不改已有产品语义。
- **D6 FEAT-1 元数据回退不戳记默认值**：默认 30°/0° 猜测不写入输出元数据（防止把猜测洗成 provenance）；参数/元数据来源的几何才再声明。correction 与 flatten 同一纪律。
- **D7 sidecar 纪律**：不动 metadata()（byte-for-byte drift 门 + 57 目录大小 pin），文档走 schema() 与结果 JSON；避免重新生成 sidecar 的整链成本。
- **D8 W3 子代理结论必须主代理实读复核**：Explore 报告的 P1/P2 逐条实读源码确认后才立项（DEF-4/DEF-5 已复核；报告的 P3 logRatio 无生产调用者→不计入缺陷，登记审计留痕）。
- **D9 tests/CMakeLists.txt**：仅末尾追加 `# BEGIN/END R25-A` 风格独立块（实际为逐 test 注释块），不动他人注册。
