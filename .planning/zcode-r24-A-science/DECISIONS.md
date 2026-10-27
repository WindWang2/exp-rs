# R24-A Decisions（假设与默认选择的显式记录）

无人值守模式：以下选择按仓库惯例与最小改动原则执行，不再询问。

- **D1 goal-loop 账本重定向**：不写全仓共享 `.goal-loop-ledger.md`（跨轨三人同写冲突面），本轨账本 = `.planning/zcode-r24-A-science/LOOP_LEDGER.md`。
- **D2 构建配置**：沿用前轨可复现配置（MSVC+Ninja+Debug+ENABLE_TESTS=ON，vcpkg_installed 从同 md5 manifest 的主仓 build-dev 复制 3.0GB 到本轨 `build-r24_a/`，VCPKG_MANIFEST_INSTALL=OFF 只读消费）。bison/flex 用 `C:/deps/winflexbison/win_{bison,flex}.exe`（与 r23-D build 同源）。构建并行度 2（空闲 RAM ~10.6GB < 12GB 阈值）。
- **D3 #1687 修复形态**（WP-A1 核心）：
  - 新增哨兵感知重载 `bothNormalizable(a,b,noDataA,noDataB)` / `normalizedDifference(a,b,out,count,noDataA,noDataB)`：无效 = `sicnu::rs::isNoDataValue`（非有限恒无效 + 声明哨兵精确相等），其余一律走 safeDiv——**移除负反射率等值对（a==b<0）的 NaN 化**：数学上 (a-b)/(a+b)=0/负=0（有定义），大气校正后轻微负反射率是合法输入。
  - 生产接线点（持有波段哨兵的算子）迁移到新重载；旧 4 参签名保留（供无哨兵上下文/测试），魔数 `-999` 行为仅保留在旧签名并在头文件标注 deprecated 语义与迁移指引。
  - 旧数据不破坏论证：NDVI 族定义域（反射率 0..1 或 -1..1 校正域）内不存在 `<=-999` 的合法值；旧路径对 `-9999` 哨兵的 NaN 行为在新路径由"声明哨兵精确相等"等价覆盖（默认 sentinel=NaN 时非有限门同效）。回归测试钉死两类旧 fixture 行为。
  - `computeStatsFromAccumulators` validCount 契约：补头文件注释——acc.count 是"喂进累加器的样本数"（喂入方负责预过滤非有限值），与 computeStats 的逐元素重过滤语义并列声明，消除 #1687 第二项的文档债。
- **D4 help 目录 drift 测试归属**：`src/processing/algorithm_help_catalog.cpp` 的键-注册一致性测试归 WP-A7（算法元数据面，非 GUI 控件）；修复键名本身已 FIXED_IN_MASTER，本轨只补防腐烂测试。
- **D5 Explore 子代理审计**：3 个只读审计代理（时序/SAR-地形/光谱-分类），结论必须由主代理实读源码复核后才立项；子代理不写代码。
- **D6 性能口径（WP-A6）**：不用绝对墙钟 CI 门；记录 5 次中位数吞吐（像素/s）+ 峰值 RSS，正确性 oracle 守恒，稳定 >5% 退化即回退。
- **D7 E2E（WP-A8）**：在既有 Catch2 架构内扩充（临时目录、离线合成小 fixture），不新建框架；至少覆盖光学指数链、时序链、DEM 链三条真实 GDAL 产线。
- **D8 测试命名/注册**：源文件 `tests/test_r24_a_*.cpp`；`tests/CMakeLists.txt` 只 append 独立 `# BEGIN R24-A` 块。
- **D9 worktree 路径含 8 位数字**（20261027）：已知 CT-R21-A-1 环境性失败面（temporal_cube sceneInstant 扫 \d{8}）；涉 temporal E2E 测试时把该环境因素显式记录，不作为通过/失败判据，并向 CROSS_TRACK 追加本轨复现证据。
