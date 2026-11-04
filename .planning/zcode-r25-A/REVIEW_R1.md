# R25-A REVIEW_R1 — 独立子代理评审（未参与实现的全新上下文）

- 评审者：general-purpose 子代理（agent 独立上下文，只读）
- 范围：`git diff origin/master...HEAD` 全量 + 周边代码
- 结论：**REQUEST-CHANGES**（P0×1 / P1×7 / P2×1 / P3×7）——已全部处置，见下表
- 评审同时确认：DEF-1 日长 oracle 数学正确（−33°→13、40°N→9、45.13°→17 样本，判别余量≥13 分钟）、OGR 句柄无泄漏、DEF-2 内核与漏斗 oracle 正确、DEF-5 inf 永不赢 best-match、FEAT-2 standard 与 master 位等价、注册块真 append-only。

## 发现与处置（全部闭环）

| # | 严重度 | 发现 | 处置 |
|---|---|---|---|
| 1 | P0 | `datasetMetaDouble` 声明无定义 → 全部测试链接失败（我的 python 补丁无声空操作） | ✅ 补定义 + `<optional>` include；根因：replace 无 assert。后续补丁全部带 assert |
| 2 | P1 | 投影 CRS 中心用 `abs(gt[5])`：北向上栅格中心跑到景外半幅，纬度差整景高 | ✅ 改带旋转项的符号化仿射中心；测试改 11×1001 行 30m（45.00 vs 45.26 判别） |
| 3 | P1 | F64 累积路径 `hasNodata &&` 前缀跳过 NaN 检测 → 无声明 NoData 的掩膜单元变 0.0（#783 回归） | ✅ 改为与内核完全相同的谓词；新增 NaN 墙 E2E |
| 4 | P1 | correction 结果缺 incidenceDeg/headingDeg（测试断言失败） | ✅ 补两字段（与 flatten 孪生一致） |
| 5 | P1 | 面积表测试 0.001° 当成 111km（实为 111m），界 1e10 应为 1.24e4 | ✅ 修注释与界 |
| 6 | P1 | perf resample 无波长元数据 → 双侧抛错 | ✅ 显式 nm sourceWavelengths 参数 |
| 7 | P1 | 光学链 fixture 无声明 NoData → apply_mask 拒绝 | ✅ withNoData(-9999) |
| 8 | P1 | DEF-6 测试同上 | ✅ withNoData(-9999) |
| 9 | P2 | LOOK_AZIMUTH 无条件戳记（默认 heading 推导的 90° 被洗成 provenance） | ✅ lookAzimuthResolved 门控（两算子） |
| 10 | P3 | SCL 直方图丢 12..15 类 | ✅ 0..15 |
| 11 | P3 | schema latitude 仍声明默认 40.0（UI 自动填充会压过 CRS 推导） | ✅ 改为无默认 raw 参数 |
| 12 | P3 | meanScore 全无限时 0.0 = 「完美匹配」假象 | ✅ null + infiniteScores 计数 |
| 13 | P3 | writeSarDatasetMetadata `headingDeg != 0` 守卫（既有死代码） | 留痕 CROSS_TRACK（pre-existing，零调用者） |
| 14 | P3 | 恒等 geotransform 通过门（无地理参考的 CRS 栅格静默得 lat=行数/2） | ✅ 精确恒等式守卫 fail-closed |
| 15 | P3 | 执行估测注释仍说 6 帧（accum 路径现 ~8 帧） | ✅ 更新为 8 帧 |
| 16 | P3 | 流向测试注释说西南角（实为 NW）+ DEF-4 显式参数路径无测试 | ✅ 修注释 + 新增 explicit wavelengths SECTION |

## 修复后验证
- 16 个改动文件 + 8 个测试文件全部 `-fsyntax-only` 通过（含 P0 链接缺失的定义补齐）
- 全量构建/ctest 待共享锁释放后执行（TEST_RESULTS.md 回填）
