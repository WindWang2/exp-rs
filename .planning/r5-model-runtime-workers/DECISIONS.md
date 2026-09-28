# DECISIONS — R5 Track 07: model runtime / Python worker hardening

## D-1 master 构建断裂的一行修复（白名单例外）
`origin/master`（a726d17a6）的 `tests/CMakeLists.txt:14728` 残留一行未消解的合并
冲突标记 `=======`（blame：7bb6398c0，#1345 合并链引入，存活至 master tip），
导致该基线上任何 configure 直接 parse error。本分支首个 commit 删除该行——
这是解锁本 Track（及任何并行 Track）的最小补丁，不重组两侧内容。PR 中单列
commit 并注明。

## D-2 真实进程恢复的测试载具：C++ fake worker，而非 Python
池的真实恢复路径需要 worker 进程真正连接 QLocalServer、真正收发 newline-JSON、
真正死亡。/bin/sh 无法可靠连 unix socket，Python 依赖会拖慢并复杂化 CI ——
新增 `tests/fixtures/fake_python_worker.cpp`（QtCore+Network，~150 行），
协议面与 worker_daemon.py 相同；pool 经 PythonWorkerProcess 以 pythonPath=
fixture 二进制启动。死亡触发方式：SIGKILL（测试直接杀 pid）、exit(code)
（干净退出）、socket abort（EOF 存活）——全部真实边界。时钟/backoff 不注入、
不 mock：测试轮询 fixture 日志文件与进程表实现确定性同步（无 sleep 调参）。

## D-3 干净退出 + in-flight = 通道故障（恢复），干净退出 + 无 in-flight = 退役
历史白名单（exit 0/1/9/15/137 非 crash）是"进程层"分类；池只看进程层会让
"正常退出却带着未答请求"的调用方永久挂起。决策：恢复条件 = in-flight 请求
存在（server.inFlightCount()>0），而不是退出分类；crash 分类仅影响
workerCrashed 信号携带的 reason 文本。socket EOF（进程存活、连接断开）同理。

## D-4 重放发送失败的静默丢弃必须闭合（-1 分支）
sendRequest() 在无 client 时返回 -1 且不注册回调 —— 重放路径原样丢弃恢复
请求（调用方挂死）。新 free function `replayRecoveredRequest()`（pool 头文件
暴露供 oracle）统一"发送或答错"语义：预算耗尽或发送被拒 → typed error。
该分支的两个伴生场景（resize 删除节点、重启后永不连接的 watchdog）有确定性
用例；-1 分支本身由该 free function 的单元 oracle 直接钉死。

## D-5 崩溃预算语义：耗尽即退役 + 服务挣回（dead-worker recycling）
保留"预算耗尽不再重启"的防崩溃循环语义，但：(a) 失败消息按原因分化
（budget/restart-failed/removed/shutdown/timeout），不再一律谎报 "retries
exhausted"；(b) releaseWorker 时若 worker 存活则预算 +1（封顶 5）——长寿命
池不会因很久以前的瞬时崩溃永久损失节点；(c) restartCount 保持单调累计，
poolHealth().totalRestarts 仍是诚实指标，预算另立 crashBudgetLeft。
回退公式随剩余预算走（挣回后恢复快速重启）。

## D-6 fence 键的平台真身份：绝对化 + 深层存在祖先的 symlink 解析 + 探针式大小写折叠
`QDir::cleanPath` 只做词法清理。新 `canonicalPublishFenceKey()`：
(1) absoluteFilePath + cleanPath；(2) 沿路径向上找最深"存在"祖先取
canonicalFilePath（symlink 收敛），拼回词法 tail（final 文件通常尚不存在）；
(3) 大小写折叠只在**探测证实**不敏感时执行——POSIX 用 stat(st_dev,st_ino)
比较翻转拼写，Windows 卷默认折叠；case-sensitive FS 上绝不合并不案。
悬垂 symlink 的 final（重命名语义下是"替换链接"）不与目标并案——rename
不跟随 final 符号链接，二者本就是不同产品。方向性：宁可保守并案（误报
AlreadyRunning）也不放行互相踩踏。

## D-7 DetectionPublishGuard 补上同型 fence（PublishFenceLease 统一）
ProductPublishGuard 的 fence（#1353）防的腐败窗口对 DetectionPublishGuard
逐字成立（同 adoption/park 机制、无 fence），但后者完全裸奔。统一为
`PublishFenceLease`（header 内小 RAII 类，声明为首成员）：ctor throw 路径
由"已构造成员被销毁"语义覆盖（取代 #1353 的 ctor 局部 slot），dtor 最后
释放。调用点核查：两种 guard 在生产与测试中从不嵌套同一路径（lane 互斥、
ensemble 并行用例均为同路径顺序复跑）。

## D-8 provider malformed 响应：fail-closed，不重放
readLine 新增 Malformed 结局（完整行到达但非 JSON 对象）。处理镜像 Oversized：
记 lastError（带 stderr 尾）、permanentlyDead、stopWorker、typed
"malformed response (output invalid)"。理由：newline 后的字节已丢失，流
位置永远无法与协议对齐，重放只会把错位数据喂给下一个请求；进程存活则更
不能谎报 "exited unexpectedly"（R4 的 catch-all 正是这么误诊的）。
分类：malformed → OutputInvalid → ComputationError（与 Oversized 先例一致）。

## D-9 handshake 失败归 ProviderCrash（词表收口）
"python worker handshake failed (expected event=ready)" 原落入 Unknown。
在 classifyInferenceError 的 ProviderCrash 词表加 "handshake failed"——
通道在任何模型工作之前失败，与 worker died 同族。不改 API、不加枚举值
（append-only 词表匹配）。

## D-10 chunk 契约 pin 修正方向：以实现 + 两个套件为准（overflow_error）
`test_chunk_contract_11` 期望 std::length_error，但 buildTileGrid（tile_spec.h
注释明示）与 test_chunk_graph 的两处 pin 都是 std::overflow_error，#1056
int64 改造后语义上 overflow 也更准。三方一致方向 = 修 pin（测试 bug），
不动 tile_spec.h。master 既有红 #1353 残留项就此闭环。

## D-11 不动项（边界诚实）
- worker_daemon.py 协议面：不改（fake worker 承担测试面）；
- acquireWorker 忙等 + processEvents（#527 已注释约束）：不动；
- gpu_plane ModelSessionPool / OOM ladder（#1353 已收口）：无新证据不动；
- MCP/Qt teardown、workflow checkpoint、plugin host：其他 Track。
