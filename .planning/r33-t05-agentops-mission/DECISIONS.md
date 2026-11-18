# R33-T05 DECISIONS — 常规技术决定（unattended 模式，不逐步请示）

| # | 决定 | 理由 | 备选与否决 |
|---|---|---|---|
| D1 | WP02 修复取向：append 在"全可见"状态下走 beginInsertRows(first=旧总数, last=新总数-1)；在"已分页"状态下不改变 mVisible（新行落到可见前缀之外，canFetchMore/fetchMore 揭示），且不发射任何行通知 | 唯一同时满足：Qt 行数契约（rowCount 变化=通知范围）、"隐藏行不能无通知变可见"、不以"显示全部历史"绕过分页（WP02 oracle 明文禁止全量揭示）；fetchMore 保证最后一条可达。原注释"新任务立即显示"仅在全可见（常规活跃会话）时保持成立 | 备选"append 时揭示全部隐藏行（带通知）"被否决：违反 oracle"不以显示全部历史绕过" |
| D2 | WP05：fromJson 采用持久化 `next_seq`（存在且合法时），并在 previousSeq==INT64_MAX（+1 溢出）时 typed 拒绝；append() 在 mNextSeq 将溢出时返回 false 且不写入 | 保存侧始终写 next_seq；逐出（含 maxEntries=0 的空 entries 极端）时派生值会回退 → seq 复用 → 违反 append-only 审计。拒绝极值文档=WP05 oracle"拒绝不可安全追加的文档" | 备选"仅派生 last+1 并在极值时钳制"被否决：钳制制造重复 seq，且 UB 仍在 |
| D3 | WP06：compactProjection 保留 terminal 条目的 string payload（typed stop reason），其余条目照旧压缩；不新增 schema 版本 | 单一 schema 兼容读取是 oracle 明文；terminal stop reason 是最小必要 evidence（WP06 列表第一项）；terminal payload 是 string，字节数远小于普通 payload，保留对预算无影响 | 备选"为压缩文档引入 v1.1 schema"被否决：违反"保持既有单一 schema 的兼容读取" |
| D4 | WP08 读取预算=32 MiB（`platform::sidecar` `kDefaultMaxSidecarBytes` 同值），stat-before-read + chunked 读（照抄 session_journal 的成熟模式），超限 typed 失败不覆盖旧状态 | 写权威从不超过该值；预算可解释（与写侧同一权威上限）；journal 侧同模式已合并评审过 | 备选"自定义更小值（如 8MiB）"被否决：嵌入式 timeline 的 100k 任务合法文档可超 8MiB，会造成合法项目打不开 |
| D5 | WP09：readContextSidecarAt 返回验证所用的同一 QByteArray，rotateLastGood 复用该字节写 last-good（删除二次打开+二次 readAll） | 消除 TOCTOU：验证的字节==保存的字节（oracle 原文）；顺带把 last-good 复制路径并入有界读（与 WP08 同一修复面） | 备选"打开后 flock"被否决：跨平台文件锁复杂且 Qt 无标准 API；同字节绑定更简单且可测 |
| D6 | WP11：fault 子树=string 且非 secret 形状且 ≤maxChars 时精确保留（评分合同）；object/array/异常 string 递归走正常脱敏+预算 | evaluator 只认 `payload["fault"].isString()==faultKind`（3 个已知短常量）；object fault（faultlab 风格 family/seed）内部仍需脱敏秘密键；oracle"输出不含 synthetic secret" | 备选"全部 verbatim"（现状）被否决：即本缺陷；备选"全部按普通值处理"被否决：极小 maxChars 配置会截断 17 字符的 transient_failure 破坏评分 |
| D7 | WP13：verificationReport 非空但 schema 未知 → 按"已提供但不可用"fail-closed（unifiedPresent=true, passed=false, verdict=FAIL + reason），confidence 0.0；extras.claims 非空时为每条 claim 标 `source:"external"`（缺省平台 claim 标 `source:"platform"`） | WP13 oracle"malformed/未知 report 不能得到平台高置信成功"+"外部自述 claims 与平台可验证 claims 明确绑定"；空对象仍是"未提供"（保持既有 legacy 测试合同） | 备选"未知 schema 继续当缺席"（现状）被否决：即本缺陷 |
| D8 | 构建入口：只构建受影响目标（见 PLAN），不全量构建；UBSan 腿用 sanitizer-debug preset 的既有目标（若时间不允许则记录 NOT_EXECUTED） | -j4 全机上限下的最小验证面 | — |
| D9 | #2078/#2079（operations_coordinator/decision_record）不纳入本轨首批：前者文件为 #2134 刚合并领域且边界明示排除，后者同；留待复核是否已被 #2134 修复后记 ALREADY_DONE 或跨轨 HANDOFF | 写集纪律 | — |

| # | 决定 | 理由 | 备选与否决 |
|---|---|---|---|
| D10 | 新测试不无条件依赖 QAbstractItemModelTester：手写 RowContractWatcher（逐信号验证 insert 范围==rowCount 增量、无 remove、dataChanged 合法域、reset 配对）常开；Qt tester 仅在 QT_VERSION<6.12.0 时以 Fatal 附加 | 实验证据：Qt 6.12.0（qt6-base 6.12.0-5.1，本机即项目所用系统 Qt）tester 在 rowsInserted 派发内同步探测 fetchMore，changeInFlight 状态机拒绝"fetchMore 同步插行"模型的嵌套 beginInsertRows（最小契约正确平面分页探针复现；纯满载平面模型通过）。这是 tester 侧限制而非模型契约违规：真实视图从事件循环滚动驱动 fetchMore。模型保持同步 fetchMore（与 QSqlQueryModel 一致） | 备选"模型改为延迟 fetchMore"被否决：破坏视图同步契约且可能引起视图轮询；备选"tester Warning 模式常开"被否决：该版本下必然刷假阳性 FAIL 噪音 |
| D11 | 独立小闭包编译验证（g++ 直连系统 Qt/jsoncpp + 既有 build 树的 Catch2 静态库）作为重型构建被锁时的轻量验证通道；不替代 CTest 全量 | 主机锁被 R33-T06 长构建占用（协议允许等待/协调但不杀进程）；单文件语法检查与 4-8 个小文件的编译链接不属于"重型 configure/build/link"，不触碰任何 build 目录与主机锁 | — |
| D12 | test_r30_t05_journal_read_bound 的 fixture `next_seq:1`+entry `seq:1` 修正为 `next_seq:2`（等长替换，字节算术不变） | 该形状本就不是真实 save 的产物（toJson 恒写 last+1）；新校验"next_seq 必须严格大于最后 entry"是 WP05 oracle 的组成部分；fixture 自称 "minimal legal journal document"，修正使其名副其实 | 备选"放宽校验允许 next_seq==lastSeq"被否决：该形状正是拼接/回退文档的指纹，放宽即放弃缺陷检查 |

## PR #2143 correction (supersedes D10's version gate and 22d427dd's removal)

D13: Restore QAbstractItemModelTester in Fatal mode on all supported Qt versions;
use its public `setUseFetchMore(false)` and drive paging explicitly under the
full official checks plus RowContractWatcher. Qt 6.8.0 source ordering and a
standalone Qt-only probe establish the completion-signal automatic-fetch
reentrancy issue; a deliberately wrong row-count delta still Fatal-aborts with
manual population. See `tests/probes/r33_t05_modeltester/README.md` for pinned
source lines, reproduction, actual results and validation limits. Deleting the
tester was unnecessary and weakened coverage. No product-code workaround or
blanket Qt-version exclusion is justified by this finding. Historical 6.12
claims above were not revalidated in this correction.
