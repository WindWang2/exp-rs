# TEST_MATRIX — cartography-production-11

框架：Catch2；目标：`test_cartography_production_11`（D-012 独立目标）+ 回归 `test_mapspec`。
运行：`QT_QPA_PLATFORM=offscreen ctest -R test_cartography_production_11 -j1`（build-dev）；direct：`test_cartography_production_11.exe "[cp11]"`。

| # | 能力 | 独立 oracle | 用例 tag | exit | 证据 |
|---|---|---|---|---|---|
| T1 | manifest digest canonical + 漂移敏感 + 环境段豁免 | 测试改 dpi → digest 必变；改环境 → digest 必不变 | [cp11][manifest] | 待填 | 待填 |
| T2 | manifest 校验/篡改检测/零页拒写 | 结构负例 + write 拒绝 | [cp11][manifest] | 待填 | 待填 |
| T3 | manifest sidecar 原子往返 + 磁盘篡改失效 | 读回 digest_ok + 手改文件失效 | [cp11][manifest] | 待填 | 待填 |
| T4 | produce 类型化拒绝（INVALID_PARAMETER/VALIDATION_FAILED） | error_code 断言 | [cp11][produce] | 待填 | 待填 |
| T5 | produce 单页交付 artifact+manifest；磁盘重算 sha256 一致；确定性重放 byte-equal | 独立 QCryptographicHash 重算 | [cp11][produce] | 待填 | 待填 |
| T6 | produce 取消 → 目录零残留 | 首报告即取消 → entryList 空 | [cp11][produce] | 待填 | 待填 |
| T7 | require_preflight_pass 非修复性缺陷拒绝；宽松模式照发 | PREFLIGHT_NOT_PASSED + 目录空 | [cp11][produce] | 待填 | 待填 |
| T8 | atlas 逐要素交付：页数/排序/文件名/extent/manifest 与磁盘互证 | 内存 coverage 图层 + 每页重算 hash + rank 升序断言 | [cp11][atlas] | 待填 | 待填 |
| T9 | atlas 缺 coverage → 类型化拒绝零残留 | EXPORT_FAILED + 目录空 | [cp11][atlas] | 待填 | 待填 |
| T10 | atlas 页边界取消回滚 | 第 2 次 export 报告取消 → 目录空 | [cp11][atlas] | 待填 | 待填 |
| T11 | series：多页物化/变量/extent/页码/克隆 id/索引页/v6 校验净 | known-answer 字段断言 + validateMapSpec 空 | [cp11][series] | 待填 | 待填 |
| T12 | series 边界：>10 页拒绝；未知 {{token}} 记 problems | problems 非空断言 | [cp11][series] | 待填 | 待填 |
| T13 | 模板治理：v1→v2 迁移 known-answer + 幂等 + 前缀映射 | 逐字段断言 | [cp11][governance] | 待填 | 待填 |
| T14 | required_furniture preflight + repair 收敛 | MAP_REQUIRED_FURNITURE_MISSING 出现后修复消失 | [cp11][governance] | 待填 | 待填 |
| T15 | 新规则 known-answer/负例（alignment/print-font/whitespace）+ catalog 发布 | 触发/不触发两向断言 | [cp11][preflight11] | 待填 | 待填 |
| T16 | 三 surface byte-equal（engine/tool/operator+TaskCenter） | 三次产物 sha256 相等；manifest 页 hash == engineSha | [cp11][e2e] | 待填 | 待填 |
| T17 | 模板 corpus 全链 smoke（instantiate→produce→manifest 互证） | failures 列表空；degradations 诚实汇报 | [cp11][corpus] | 待填 | 待填 |
| R1 | 回归：test_mapspec 全套（206+ cases） | 既有断言不回归 | test_mapspec | 待填 | 待填 |
