# HANDOFF — r33-t04-execution-backend

## → T06（桌面与制图）

1. **i18n gate 残余 `hard_en=1`**：`src/app/panels/data_manager_panel.cpp:972` —— #2132（已合并）
   引入的双语分页标签中一处裸 ASCII 字面量命中用户可见 API。本轨修齐三个后端 cap
   与总量后，gate 仅剩此一条。T06 用
   `python3 scripts/audit_i18n_strings.py --files tests/fixtures/i18n_audit_files.txt --gate tests/fixtures/i18n_string_baseline.json --selftest`
   复验。**不得抬 baseline**。
   - owner: T06；最小输入：该文件 972-977 行的 `state`/`label` 构造。

## → T03（数据代际与 dataset）

1. **执行缓存 stat-hint → 内容证明**（WP11 深度部分）：
   `src/data/execution_fingerprint.cpp` 的 `declaredOutputStillValid` 以
   size+mtime 为廉价 proxy；同大小同 mtime 重写（含刻意伪造）无法检出。
   本轨整文件拥有该文件但内容证明应消费 T03 身份接口
   （AssetRevision/content digest），按协议不新增第二权威。
   - 期望契约：T03 提供按路径取内容摘要的既有或新增接口；
     T04 在 `lookupExecution` 校验时以 stat hint 短路、疑点时内容复验。
   - 兼容：v3 fingerprint 版本分离保留。
2. **ArtifactObjectPool 跨轨扩展**（WP03 关联）：pool 对象携带 sidecar 集合
   的表示若需扩展（当前 pool 不带 sidecar，serve 清除目的地陈旧 sidecar），
   归 T03；T04 只消费。

## → T05（AgentOps 与 Mission）

1. **capability_router 词典文件**：`src/science_context/capability_lexicon_*.inc`
   现为纯数据包含文件（中文=匹配词汇）。若 T05 需要新增意图词，直接编辑 .inc
   与 capability_router.cpp 中的表同步，勿再向 .cpp 添加中文字面量（i18n cap=4）。

## → T01（科学数值）

1. **WP09/WP10 科学阈值/NoData oracle**：change mask 恢复链与 spectral claim
   的数值验证（阈值策略、NoData 语义）由 T01 提供独立 oracle；本轨
   test_r33_t04_change_mask_recovery / test_r33_t04_spectral_claim_contract
   使用几何/元数据/像素计数断言。

## → T06（实验教学 UI）

1. **WP12 readiness 投影消费**：`passport.ok_missing`/`passport.ok_type` 两个
   新 item code（severity=unknown）需 UI 侧文案映射；wire 值
   `unknown` 与既有 `passport.missing` 同级处理，勿当 pass。
2. **WP14 反馈投影**：`grader.overall` 行在评分不可用时现携带
   reasonZh（错误详情）；UI 直接显示该行 reasonZh 即可，无新字段。

## WP19 制图 adapter（未完成，本轨记录）

六个 cartography adapter schema 未声明 `outputs`（落入兼容 Raster
producesFile fallback）。修复需真实 registry→TaskCenter/Dispatcher 报告四件套
E2E + T06 可见 UI 验收交接；本轮未实现（NOT_EXECUTED，诚实记录于 PR 正文）。
文件：`src/agent/cartography/cartography_operators.cpp`（T04 整文件例外）。
