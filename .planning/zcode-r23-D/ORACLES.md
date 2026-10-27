# ORACLES — R23-D 每个 WP 的可验证完成条件

## WP-D1 CT-6 闭环
- RED：master 源码 + 新测试、但无 ndvi_landsat.rules.json 时，case 1 失败（Unknown lab "ndvi_landsat"，typed usage 错误）——证明 repo 无 foundry 场景权威；case 3 的「foundry 工件在 ndvi_basics 下 fail」是 CT-6 原始事实的永久钉子。
- GREEN×2：三 case 全绿（foundry 流程 PASS + digest 双跑一致；错 CRS → fail + "CRS EPSG:4326 does not match expected EPSG:32648" + cappedByBlocking；双权威互斥守卫）。
- 主机日志与 digest 存档：TEST_RESULTS.md 记录两轮 ctest 输出与 grade digest 值。

## WP-D2 session_store 深度炸弹
- RED：master 上 loadAny 对 200 层嵌套会话文件抛 Json::RuntimeError（Catch2 捕获 → 用例失败）。
- GREEN×2：typed `lab.session.corrupt` + message 含 "stackLimit"；语法坏文件同码（对照组）。

## WP-D4 expected_numeric 有限性
- RED：master 接受 ±inf 区间（result.ok == true → 断言 firstDiagCode 失败）。
- GREEN×2：`lab.runtime.field` typed 拒绝 ×3 形态（双侧/仅 max/仅 min）；有限反序边界仍走既有 min<=max 拒绝；合法文档仍 ok（对照）。

## WP-D5 回归保护矩阵
- 证据：#1610-1613（test_faultlab/test_faultlab_params_r8/test_r16_a_faultlab/test_r21_e_faultlab_verifier）、#1598/#1599/#1601（test_verifier_*_14 车道 + test_r18_e_verifier_rules）、#1602（test_verify_adapters）、#1593（test_lab_data_pack）、#1678（test_lab_runtime/session_state）、#1648（test_recipe_registry、test_lab_source、test_lab_document）、#1653（verifier 车道）、#1694（test_r22_teaching_batch_regrade）在本 worktree 全绿（命令+退出码存档 TEST_RESULTS.md）。
- 不重修已修问题；若任一红 → 升级为 LIVE 缺陷走 WP 流程。

## 交付级 Oracle（对应协议 O0-O8）
- 见 FINAL_REPORT.md 逐项打钩；PR 创建后 gh pr view 验证 OPEN/非 draft/base=master/head SHA。
