# DECISIONS — r33-t04-execution-backend

自主技术决定记录（unattended 模式，按 .agents/AGENTS.md 约定写入此处）。

| # | 决定 | 理由 | 备选与放弃原因 |
|---|---|---|---|
| D1 | 基线取 `origin/master` `1dfb04382e`（非研究快照 `5ff15400`） | 快照后 #2134-#2137/#2139 已全部合并，0 个开放 PR；排他表运行时重取后全部清空 | — |
| D2 | WP01 用 O(n) min-scan 而非新数据结构 | 并发 job 数量小（map 通常 <10），最小可解释改动；保持既有 wall-clock/cooperative 契约 | multimap<deadline>：引入第二索引与维护成本，无收益 |
| D3 | WP02 用"绑定后补发带身份快照"而非改 submit 签名 | enqueueTask 是 public virtual，改签名破坏覆写者；queueTaskUpdatedLocked+flushPendingSignals 复用既有信号通道且 FIFO 保序 | 事务化提交入口：侵入大 |
| D4 | WP17 键改长度前缀而非换哈希/分隔符 | 单向、无歧义、旧 entry 自然 miss（内存缓存无需迁移）；数值/布尔字段本就无分隔符歧义 | 转义分隔符：双向复杂 |
| D5 | WP12 复用既有中文字面量保持 i18n allow=21 | #2108 明确不抬 cap；语义区分由 code 字段（passport.ok_type/ok_missing）承载 | 新增字面量：22>21 破坏 gate（已实测） |
| D6 | WP16 改公共 API 签名（MetricResult::fromJson→Result、equivalentRuns→Result<QStringList>） | 调用方全集可控（1 处生产 + 2 测试 / 2 生产 + 2 测试）；Result<T> 是仓内既有惯用法 | out-param：不惯用 |
| D7 | WP07 放宽 evidence 为 object-or-array 而非包装数组 | 已持久化文档兼容（旧文档本就无法 parse，但序列化格式不变即 digest 不变）；引擎三类证据两类发数组一类发对象，统一闭集 | 强制对象：需改引擎发射端并换 schema 版本 |
| D8 | WP04 progress value 只强制 finite 不强制 [0,1] | 参考worker原样转发 operator 进度；host 未定义值域，admission 拒绝 1.5 可能误杀合法发射者。范围钳制属消费方（记入 review 线索） | — |
| D9 | WP04+05 用 catch(...) 网兜 + 既有 switch 统一 retire | 恰好一次回收由 switch 单点保证；网兜只负责把逃逸异常转为 retire+rethrow | 分散 try/catch：回收点翻倍 |
| D10 | capability_router 词典移入 .inc 数据包含文件 | 中文是匹配词汇（数据面）非 UI 文案；编译期逐字节不变、零运行时风险；与 .ts/.ui 同类的数据分类。audit 仅为 C++ 字符串卫生 | 抬 allow cap（禁止）；运行时 JSON 资源（Qt-free lib 引入部署依赖+缺文件退化风险） |
| D11 | lab_session_state 用共享前缀常量合并 21→15 | 全为诊断文案，字段名 ASCII 携带区分；不损失信息 | 抬 cap（禁止） |
| D12 | hard_en=1（data_manager_panel.cpp，#2132 引入）不修 | 属 T06 UI 面板写集；记录 HANDOFF 交接 | 越界修改违反排他规则 |
| D13 | WP03 sidecar 命名收敛到 ArtifactGC::sidecarPathForMain 单一权威 | 规则三处（GC/commit/serve）一处漂移即事故；复用而非复制 | 各处内联 lambda：复制规则 |
| D14 | WP09 用既有 verifyPublishedOutput 钩子绑定终态 | resumable_tile_run R8 已内建"marker 降级→重建"语义；接线即修复，幂等重试免费获得 | 新增 journal 字段/双 marker：改持久格式 |
| D15 | WP20 修复注入子进程环境（identity+隔离配置）而非写测试全局 | #2113 要求不写用户配置、不读凭据；GIT_CONFIG_GLOBAL=devnull 使有/无用户身份行为一致 | — |
| D16 | WP11 深度内容证明（stat hint→内容 hash）仅记 handoff | 需消费 T03 身份接口（AssetRevision/content digest），跨轨新接口按协议先 handoff；本轨已把 sidecar 集合完整性（WP03）与 claim 数据契约（WP10）闭合 | 自造第二身份权威：违反"不新增权威" |
| D17 | WP19 制图 adapter 仅静态复核暂不改 | 六 adapter schema 未声明 outputs 的修复需真实 registry→Dispatcher 四件套 E2E（T06 fixture 交接在案）；本轮优先已验证批次 | — |

## i18n gate 决定汇总

- capability_router.cpp: 66→0（≤4 ✅，词典→.inc 数据文件 D10）
- lab_feedback_projection.cpp: 14→13（=13 ✅，共享前缀常量）
- lab_session_state.cpp: 21→15（=15 ✅，六处合并 D11）
- lab_readiness.cpp: 21→21（=21 ✅，复用字面量 D5）
- 总量 nonui_zh: 1088→1022（≤1059 ✅）
- 残余 hard_en=1 于 src/app/panels/data_manager_panel.cpp（#2132 引入，T06 写集）→ HANDOFF
