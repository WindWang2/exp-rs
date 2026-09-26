# DECISIONS — Track 16 验证链收口（链路规则，随执行定稿）

> 收口时每条规则必须与代码实际行为一致（WP-G 抽 2 条对照断言）。

## D1 digest 产出点登记规则
链上每一处"canonical 文本 → sha256/fingerprint"产出点必须登记于 BASELINE.md §7（机械枚举 rg 全集），
并满足其一：(a) canonical 写出器规范上 locale 无关（grader std::to_chars 路线）；(b) 产出点入口单点
钉 ClassicNumericLocale（verify canonicalJsonText 路线，#1318 先例）；(c) 书面豁免（漂移方向只会
分裂缓存键、永不引起跨 locale 碰撞或陈旧命中 —— science_context assetDigestOf 豁免理由）。
禁止在调用方散落 setlocale。

## D2 失效联动合同
preflight 与 science_context 共享同一权威投影（StateAssetFactsProvider 同 resolver 形状 +
CapabilityFactsLookup revision）；任何失效接缝（invalidateAsset/invalidateAllAssets/notifyProjectSwitch/
setCapabilityFacts/refreshRecipes）之后，同输请求不得复用失效前事实产物：bundle 层 cache 必须清空，
preflight 层必须以当前权威回答（引擎无内部缓存，陈旧风险在权威投影侧）。失效后放行必须携带
失效后证据（revision 推进 / cacheHit=false / typed unknown），禁止静默回退陈旧事实。

## D3 两级 outcome 合成规则
（WP-C 定稿）provider 未挂载 = Indeterminate(verify:i_provider_missing)；不可判读（非有限数、
不可读、词汇外）= Indeterminate；可检出违约 = Fail(verify:e_*)；坏 spec = 单条合成 spec.valid Fail。
多检查报告的顶层 outcome 取"最坏者"：Fail > Indeterminate > Pass；合成规则必须可预测且每条有
头注释合同出处。

## D4 "不可评"判据（grader/suitability）
（WP-E/F 定稿）证据不完备（超时标记、必需 evidence 文档缺失、部分提交）、事实冲突/过期/部分可用 →
typed 拒绝（GraderError / suitability typed failure 词表既有码），绝不是低分或静默择一。
"分数只在证据完备时给出"。

## D5 豁免清单
（WP-G 定稿）—— 每条：对象、豁免理由、漂移方向分析。首条：science_context assetDigestOf
（cache-key 用途；locale 漂移 → 字节变化 → 键变化 → miss 重算，安全方向；无跨机合同）。
preflight/sha256.h 第三套 SHA-256：与 verify/grader 同款自包含实现的叶子隔离先例（Qt-free 叶子
不跨引用），登记为既接受重复（引擎叶子边界），非缺陷。

## D6 ctest 口径（Oracle 修正）
字面 Oracle `verify|grader|preflight|suitab|science_context` 漏 verifier/verification 家族
（不含 "verify" 子串）；本轨绑定口径 `verif|grader|preflight|suitab|science_context|evidence`
（超集），两轮全绿同时覆盖字面口径。
