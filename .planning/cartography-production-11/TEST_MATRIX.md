# TEST_MATRIX — cartography-production-11

框架：Catch2；目标：`test_cartography_production_11`（新，D-012）+ 回归 `test_mapspec`。运行：`QT_QPA_PLATFORM=offscreen ctest -R <target> -j1`（build-dev）。

| # | 能力 | 独立 oracle | 命令（ctest -R …） | exit | 证据 |
|---|---|---|---|---|---|
| T1 | atlas 导出页数/文件名/extent | 测试自建内存 layer（N=3 已知要素），自算 filename_expression 预期 + 重算 sha256 | test_cartography_production_11 | 待填 | 待填 |
| T2 | atlas manifest 与磁盘一致 | 逐页 reopen 文件 hash 比对 manifest | 同上 | 待填 | 待填 |
| T3 | produce 全链（spec→compose→preflight→repair→export→manifest） | 产物存在 + manifest digest 独立重算 | 同上 | 待填 | 待填 |
| T4 | produce 取消无半成品 | 取消后目录枚举为空（无 tmp/无产物/无 manifest） | 同上 | 待填 | 待填 |
| T5 | 导出失败（非法 dpi/只读目录）回滚 | 目录枚举 + 返回 typed 错误 | 同上 | 待填 | 待填 |
| T6 | 三 surface byte-equal（tool/operator/pipeline） | 三次产物 sha256 相等（png） | 同上 | 待填 | 待填 |
| T7 | 模板 v1→v2 迁移 known-answer | 输入 v1 fixture→期望 v2 字段逐项断言；幂等（再迁移 no-op） | 同上 | 待填 | 待填 |
| T8 | 新 preflight 规则 known-answer/negative | 构造越界/合规 spec 各一 | 同上 | 待填 | 待填 |
| T9 | repair 有界（新规则触发） | 迭代数 ≤ 钳制且台账完整 | 同上 | 待填 | 待填 |
| T10 | series 生成（vector/time/region 三源） | 页数/extent/变量 known-answer；上限钳制 negative | 同上 | 待填 | 待填 |
| T11 | annotation 锚定 + provenance | 锚点坐标→页内 rect 换算 known-answer；manifest binding 段 | 同上 | 待填 | 待填 |
| T12 | 模板 corpus 全链 smoke | 全部 shipped templates 迭代；失败清单诚实 | 同上 | 待填 | 待填 |
| T13 | 字体策略/metadata（pdf 矢量+title） | 导出成功 + manifest 环境段字段存在；字体替换诊断 | 同上 | 待填 | 待填 |
| T14 | Unicode 路径导出 | 中文+空格 tmp 目录产物完整 | 同上 | 待填 | 待填 |
| R1 | 回归 test_mapspec 全套 | 既有断言不回归 | test_mapspec | 待填 | 待填 |
