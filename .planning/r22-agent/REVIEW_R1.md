# REVIEW_R1 — r22-agent (R22-04) adversarial security review

- 审查人：R1（独立安全审查，与开发过程分离）
- 对象：`git diff origin/master(911e0b17a3)..HEAD -- src/ tests/`（7 提交，#1704 #1690 #1691 #1692 #1705 #1693）
- 方法：逐文件读 diff + 完整源文件 + 全部调用方；对关键语义（`istream::getline` 边界）用本机 g++ 实测反例；独立重跑测试二进制（containment 64 断言、env_keys 46 断言实测通过；job_logs/ipc/ghost_algo 与 `.planning` 双跑记录一致）；对每项修复构造失败反例攻击。
- 结论先行：**APPROVE**（无未闭环 P0/P1 实错；2 项 P2 实错为后续必须跟进的策略缺口，不影响本次合并的正确性与既有行为）。

严重度定义：P0=可直达的本地文件逃逸/RCE 级实错；P1=高危实错（数据泄露/挂死）；P2=真实但条件依赖的策略缺口/缺陷（必须修，可跟进）；P3=意见/加固建议（可不修）。

---

## 一、发现清单

### F1 [P2·实错] workspace_containment.h — 非 http(s)/file/vsi 的 `scheme://` payload 绕过 remote 默认拒（file-backed 与 unknown 驱动两侧都漏）

- 位置：`src/agent/tool_catalog/workspace_containment.h:255-260`（`embeddedDriverPath` 的 URL 直通白名单只有 `http://`、`https://`、`file:/`、`/vsi`）与 `:288-293`（`unknownDriverPayloadIsDangerous` 同样的四前缀）。
- 反例链：
  1. **file-backed 侧**：`Zarr:s3://bucket/arr`（`zarr` 在 file-backed 白名单，`src/.../workspace_containment.h:211`）。`embeddedDriverPath("s3://bucket/arr")`：不带引号、不匹配四前缀；`rest.at(1)=='3'` 非盘符 → 从 0 扫冒号，`sep==2` → 提取出 **"s3"**（`workspace_containment.h:267-269`），被判为工作区相对名 → `DriverPath` 递归 containment 放行。而真正交给 GDAL 的是原串，Zarr 驱动可经 `/vsis3` 打开 S3 —— 未经 `SICNU_MCP_ALLOW_REMOTE` 的远程访问。同构：`GeoJSON:ftp://…`、`CSV:s3://…`。
  2. **unknown 侧**：`TileDB:s3://bucket/array`（GDAL 真实驱动短名 "TileDB"，不在两张表里）。payload `s3://bucket/array` 首字符 `s`、次字符 `3`、无 kv 键、无 `..` → 判"良性相对名"放行 → TileDB 驱动直连对象存储。同类：`AmigoCloud:<token>`（云端库）、`MEM:::ptr`（进程内内存引用，需猜中有效指针，实际不可利用）。
- 影响评估：#1704 的头号目标（本地文件逃逸 `CSV:/etc/passwd`）已闭合；本条是**远程默认拒策略**的旁路，利用需要构建内含相应驱动（TileDB/Zarr）且环境有云凭证/网络。本机 vendor GDAL 是否含 TileDB 未证实，但该头文件是被 MCP/CLI/data-platform 共享的策略权威，应按任意构建假设封死。
- 建议修复（小改动）：在 `embeddedDriverPath` 与 `unknownDriverPayloadIsDangerous` 中，对 payload 复用已有的 `kScheme` 形状（`^[a-z][a-z0-9+.-]+://`）——任意 `scheme://` 一律按 remote 类递归/拒绝，而不是枚举 http/https。这同时覆盖 s3/gs/oss/ftp/azurefs 等全部形态。

### F2 [P3·意见] workspace_containment.h — 嵌套驱动前缀提取错位（验证路径 ≠ 打开路径，靠偶然安全）

- 位置：`embeddedDriverPath`，`workspace_containment.h:261-269`。
- 反例：`CSV:GPKG:/etc/x.gpkg` → `rest="GPKG:/etc/x.gpkg"`，`at(1)=='P'` 非盘符 → `sep==4` → 提取出 **"GPKG"**（相对名，放行）；实际传给 GDAL 的原串由 CSV 驱动剥掉 `CSV:` 后打开 `GPKG:/etc/x.gpkg`——只是因为 CSV 驱动不会把 `GPKG:` 再当驱动前缀，才未成逃逸（打开失败）。当前结果是"验证了错误路径"，属于纵深防御缺口而非可利用洞。
- 建议：`embeddedDriverPath` 若提取结果本身仍是 `gdalDriverPrefixOf` 可识别的驱动形态，递归 `classifyReference` 而非当相对名。

### F3 [P3·意见] workspace_containment.h — 未知驱动 payload 的其余边角（均为当前安全、建议随 F1 一并收紧）

- `%2fetc%2fpasswd`（URL 编码）：首字符 `%` 不在黑名单 → 良性相对名放行；GDAL 不做百分号解码、`WEIRDDRV` 非真实驱动 → 打不开，安全是"靠驱动不存在"。若驱动名真实存在且其 Identify 解码（无现实例子），即成洞。
- `WEIRDDRV:home/x`（无斜杠 home 引用）：按相对名 join 到工作区 → 包含，正确。
- 空格/制表前缀：`classifyReference` 顶层 `trimmed()`（:338）与 `unknownDriverPayloadIsDangerous` 的 `trimmed()`（:280）都先剪裁，`CSV: /etc/passwd` 提取 ` /etc/passwd` 后递归分类被拒，正确。
- 多行：MCP 参数来自 JSON，字面 `\n` 进不了参考串；即使进来，`CSV:/etc/passwd\n/x` 的提取路径仍以 `/` 开头被拒。
- Windows 盘符：`GPKG:C:/x.gpkg:layer` 提取 `C:/x.gpkg`（跳过盘符冒号，:263-266），Win 下绝对→按工作区判，POSIX 下相对→CWD（=工作区，mcp_server.cpp:559 与 rs_pipeline_runner.cpp:178 均已验证 chdir）内，正确。
- `QSet` 函数级 static：C++11 magic statics 线程安全初始化，`inline` 头文件多 TU 共享一份，无竞态。仅提示每次调用 `toLower()` 分配 + QSet 哈希查找在热路径的微小开销，可接受。

### F4 [P3·意见] python_ipc_server.cpp — 256B 前缀抢救正则可命中"非响应行"与 id 溢出（可信信道上的理论误杀）

- 位置：`src/python/isolated/python_ipc_server.cpp:189-211`。
- 反例 A（错杀无辜请求）：守护进程→宿主的**请求**行（`ui.add_plugin_menu` 等）若超过 1 MiB 且携带 `"id": N`，而 N 恰与宿主在途请求 id 撞号（两侧 id 计数器都是小整数，必然重叠），抢救逻辑会把该无辜在途请求立即以 -32001 回错并 `dropInFlight`。当前 daemon→host 的所有方法 payload 都很小，造不出 >1MiB 行，故为理论风险；且旧行为（静默丢行+挂到超时）更差。
- 反例 B（toInt 溢出）：`"id": 9999999999` → `toInt()` 失败返回 **0**，而 0 是真实在途 id（`m_nextRequestId` 从 0 起）→ 误杀 0 号请求。负数 id 正则放行但 `m_callbacks` 无负键，安全。
- 建议：把正则锚定到响应形状（如 `^\{"jsonrpc":"2.0","id":`，daemon 固定键序，本文件注释也以此为由只取 256B），并校验 `toInt` 的 ok 位或用 `qint64` 解析后判界。

### F5 [P3·意见] task_center.cpp — `m_forwardedLogCounts` 双语义并存（绝对索引 vs 本地索引），当前数学上安全但脆弱

- 位置：`src/processing/framework/task_center.cpp:2014-2044`（消费端未随 #1705 修改）。
- 推理链：delta 路径（:2027-2037）写入**绝对**索引；cumulative 路径（:2039-2043）写入**本地** size。由于 `droppedLogLines` 单调不减，cumulative 追加的本地区间 `[f, size)` 映射到绝对 `[D+f, D+size)`，恒晚于已展示水位（上次 cumulative 展示至 `D_then+f-1`，上次 delta 展示至 counter 本身），穷举交错可得**任何可达序列都不重复投递**；代价只有"驱逐窗口造成的空洞"（这是 #1705 的既定语义："bounded window，绝非完整历史"）。提交时 catch-up 快照发生在运行前（dropped==0，两语义相等），与 delta 流的竞态由 :2027-2036 的 `engineIndex < seen` 幂等跳过吸收——与旧 #616 语义兼容。
- 但这是"靠 dropped 单调性 + 单一监听者成立"的隐式不变量；建议跟进：TaskCenter cumulative 分支补 `droppedLogLines` 换算成绝对空间（记录里现在带了这个字段），把计数器统一成单一语义。终态记录在 cursor 存在时改为 delta（job_engine.cpp:1104-1124）已验证与 :2017 分支兼容，且 :2079 终态清理不受影响。

### F6 [P3·意见] tests/CMakeLists.txt — `test_r22_agent_ghost_algo`/`test_r22_agent_ipc` 未随 `if(SICNU_EMBED_PYTHON)` 门控

- 位置：`tests/CMakeLists.txt:16520+`（新注册块在文件尾，位于 8118-8349 的门控块之外；对照 `test_python_plugin_host` 在块内且 `RUN_SERIAL`）。
- 影响：本车道 EMBED_PYTHON=OFF 下两测试仍被注册执行（ghost 需要真 python venv + numpy，本机已配且双跑通过；`sicnu_python_isolated` 是无条件 target，configure 不受影响）。在无 venv 的 CI 车道上 ghost 可能失败而同族被跳过——车道一致性建议对齐门控（或按 `SICNU_PYTHON_EXECUTABLE` 存在性门控）。

### F7 [P3·意见] external_process.cpp — 空 key 的 E5007 文案误导

- 位置：`src/sdk/exprs/external_process.cpp:121-122` 返回 `<empty environment key>`，拼进 `"explicit environment key '<...>' influences the dynamic linker and is refused"`。空 key 实际是 splice/截断拒绝而非 linker 名命中，文案张冠李戴；信息足够定位，建议换措辞。

---

## 二、逐项攻击记录（未成为发现的部分）

### #1704（workspace_containment.h）
- 逃逸形态逐一验证：`CSV:/etc/passwd`、`CSV:<工作区外真实文件>`（大小写两种）、`GPKG:...:layer`、`NETCDF:"...":var`、`SQLite:`、`csv:` 均拒绝（file-backed 提取后递归 containment，DriverPath 分支 :470-481 递归复用同一策略含符号链接弱规范化）；`WMS:/WFS:/PG:/MSSQL:` 走 remote 默认拒，`SICNU_MCP_ALLOW_REMOTE=1` 时与 http 对称放行；未知驱动 `BOGUSDRV:/etc/passwd`、`WEIRDDRV:host=…`、`server=x port=…`、`../up.csv`、`<xml>`、`~/x`、`/vsicurl/http://…`、盘符、引号包绝对路径全部拒绝。
- 误伤检查：`data:a.tif`、`./data:a.tif`、`CSV:inside.csv`、`CSV:out/result.csv`、`NETCDF:"x.nc":var` 保持放行（未知良性 word:fragment 维持旧 LocalPath 行为，POSIX 冒号文件名不回归）；`C:/x`、`C:`、`Q:/data/x.tif` 保持 LocalPath（`gdalDriverPrefixOf` 的 `colon < 2` 拒单字母）。前缀含空格（`ESRI Shapefile:`）不识别为驱动——该驱动本就不支持前缀剥离，无洞。
- 递归深度：`CSV:CSV:…` 在第二层即截断为相对名，无法构造无界递归。
- 无工作区模式（root 空）：仅 scheme/驱动策略生效，`CSV:/etc/passwd` 与裸 `/etc/passwd` 同放行——与既有"无沙箱=只查 scheme"契约一致，测试有断言。
- 遗留：F1/F2/F3。

### #1690（external_process.cpp + plugin_manifest.cpp）
- 攻击面收口验证：`ExternalProcess::run` 是全仓唯一生产 spawn 入口（`src/plugins/framework/external_tool_operator.cpp:203`），Windows（:636-644，先于 :649 `environmentBlock`）与 POSIX（:1167-1175，先于 :1206 `childEnvironment`）都在组包前过 `dangerousEnvironmentKey`；`=`/`\0`/空 key 在任何比较前拒绝（:121-125），`A=B`、`LD_PRELOAD=/x.so `、大小写变体全部 E5007（RED 日志实锤旧行为 started=1）。
- Windows environmentBlock：拒绝发生在组块之前，`"A=B" + "=" + v → "A=B=v"` 的拼接不再可能到达子进程；继承路径 key 由 `environ` 按 `find('=')` 切出，天然无 `=`。
- manifest 解析期校验（`[A-Za-z_][A-Za-z0-9_]*`）比 OS 宽松规则更严（POSIX 仅禁 `=`/NUL）：对存量 manifest 是收紧性破坏（工具整条加载失败），已核对仓库内全部 fixture（`test_plugin_manifest.cpp:179` 的 `FULL_MODE` 等）均合规；方向 fail-closed，接受。`isalpha/isalnum` 用 `unsigned char` 转型正确，locale 敏感性可忽略（C locale 下高位字节非字母）。
- 遗留：F7。

### #1691（python_plugin_adapter.cpp + app_interface_bridge.*）
- 失败路径撤销（:182-186）：撤销发生在 `m_bridge.reset()` 前后同线程、无事件循环重入窗口；桥析构自动断开 `messageReceived` 连接，晚到的 daemon 侧注册请求无人应答（daemon 侧代理自超时，符合 #1462/#1384 既定模式），"迟到注册与撤销赛跑不可能"的注释成立。
- 双重撤销：`unload()`（:270-271）与失败路径先显式撤销再 `reset()` 触发析构兜底——`unregisterRegisteredAlgorithms` 首行空列表早退（app_interface_bridge.cpp:361-362），幂等，无双重 remove。
- 析构兜底（app_interface_bridge.cpp:288-302）：无 IPC 调用（只动 AlgorithmEngine provider / AtomicAlgorithmRegistry），try/catch 吞异常不逃逸析构；GUI 对象析构线程约束与既有成员（QAction 等）一致，非新增风险；与 JobEngine 锁无嵌套（无死锁路径）。
- 测试真实：真 daemon、注册后 sleep(8)>5s、GREEN 双跑 7 断言。

### #1692（python_ipc_server.cpp + worker_daemon.py）
- 阈值一致性：宿主拒 `newlineIdx > 1048576`（不含 `\n`）；daemon 拒 `len(payload) > 1048576`（含 `\n`）→ daemon 正常发送的最大行 1048575 < 宿主上限，**不存在 daemon 发出而被宿主丢弃的窗口**；daemon 的替换错误帧（几十字节）自不超限。
- 回错路径：先 `erase` 再回调（防 #624 重入）、`dropInFlight` 防崩溃重放复活——与正常分发路径（:234-242）完全同构；`takeInFlightRequests` 语义（retire 全部 pending）已被测试注释与断言覆盖。
- 解析存续：丢行后 `m_buffer.remove` + `continue`，后续短行正常分发（测试覆盖）；`m_buffer` 超过 4MiB 的整体断连路径未受影响。
- daemon 侧：`resp.get("id")` 恒为原请求 int id（各分支都带 id），替换帧键序 `jsonrpc,id,error` 与宿主 256B 前缀提取假设一致。
- 遗留：F4。

### #1705（job_types.h + job_engine.{h,cpp}）
- UTF-8 截断（job_engine.cpp:769-778）：进入条件 `size > 4096` 保证 `text[4096]` 合法（index < size），`cut` 单调递减、`cut>0` 兜底——无越界、无死循环（合法 UTF-8 最多回退 3 字节；恶意连续续字节最多回退到 0）。截断标记追加后行长略超 4096，可接受。
- 块驱逐（:780-790）：size 恒 ≤10001 → `evict` 恒 5001，`droppedLogLines` 与 `logLines` 之和恒等于累计追加数（测试 25002/12002 两例精确验证）；驱逐不改绝对编号，`m_deltaLogCursor` 无需调整——注释论证正确。
- `m_deltaLogCursor` 生命周期：`finishJobLocked`（:825）在终态置位**之后**同锁擦除；#1693 早退守卫（:980-981）让迟到日志回调在终态记录上直接返回——不会重插 cursor（终态不可变 + 无泄漏）；`m_jobs` 查不到（被 prune）时 :973-974 早退同样覆盖。
- 绝对索引换算（:994-1000、:1112-1117）：`localStart = cursor > dropped ? cursor-dropped : 0`，`cursor ≤ absTotal` 保证 `localStart ≤ total`，`begin()+localStart` 无越界；off-by-one 由测试的连续性断言（`d.offset == expectedNext`、终值 `== appended+1`）钉死。终态 else 分支（:1119-1123，`absTotal==cursor`）按"终行必然新增"推理不可达，属防御性死代码，无害。
- TaskCenter 兼容：见 F5——无重复投递（数学验证），空洞=设计内驱逐窗口；TaskCenter 自身 logBuffer 另有 2000/4000 上限（task_center.cpp:98-109），OOM 目标端到端成立。
- shutdown/appendLog 持锁：全部 8 个调用点（shutdown:154、cancel:341、Started:857、未知算法:942、日志回调:982、finishTerminal:1096）都在 `m_mutex` 内，与注释一致。
- 性能：终态/进度 notify 仍在锁内整拷贝至多 10k 行（原有 O(n) 拷贝从无界变有界），满足 issue 目标。

### #1693（mcp_server.cpp 死分支删除）
- **实测验证**（本机 g++/libstdc++，kMax=8）：恰 kMax 字符+`\n` → `fail=0, eof=0, gcount=9`（分隔符被消费且计入 gcount）——由非 fail 路径正常投递；恰 kMax 字符+EOF → `fail=0, eof=1, gcount=8`——由 :456-468 的 eof 分支投递并退出；kMax+1+`\n` → `fail=1, gcount=8` → sentinel。三态都与删除后的代码路径吻合：被删分支（`fail() && gcount()==kMaxMcpLine`）可达时下一字符必非 `\n`/EOF（否则标准库按条件序先命中 delim/eof），确系死代码；且旧行为在该态本就 fall-through 到 sentinel，删除零行为变化。EOF-at-exact-boundary 不回归（实测走 :456 分支正常投递）。
- #1693-2（终态日志早退）归入 #1705 段落验证。

### tests/（断言质量）
- 五个新二进制断言真实、无偷懒：containment 64 断言含正反例与 no-workspace 契约；env_keys 46 含 E5007 通道、控制组（合法 key 真到达子进程）、manifest 四分支；job_logs 24031 断言含精确驱逐账目（5001/7001）、连续无重 delta 流（set 去重 + offset 严格递推）、截断标记、40 行控制组；ipc 17 含 5s 时限（对 RED 是 300s 挂起）、in-flight 排除、解析存续、未知 id 静默；ghost 7 为真 daemon 端到端。唯一宽松处：job_logs 截断断言 `< 8*1024`（实际 ≈4.1k，容差偏宽但方向正确）。
- 独立复跑：本审查当场重跑 containment（64 通过）与 env_keys（46 通过）；job_logs/ipc/ghost 受本机共享库路径限制未直接重跑，但 `.planning/r22-agent/evidence` 的 GREEN 双跑（24031/17/7）与 LOOP_LEDGER R11 一致。
- 文件白名单：`git diff --name-only` 恰为 12 个 src 文件 + 6 个 tests 文件 + `.planning` 文档，与任务书范围一致；无越界改动。

---

## 三、结论

| 级别 | 数量 | 清单 |
| --- | --- | --- |
| P0 | 0 | — |
| P1 | 0 | — |
| P2（实错，须跟进） | 1 | F1（任意 `scheme://` payload 绕过 remote 默认拒，file-backed/unknown 两侧） |
| P3（意见） | 6 | F2 嵌套前缀提取错位、F3 未知驱动边角加固、F4 IPC 抢救正则锚定/溢出、F5 TaskCenter 双语义计数器统一、F6 ghost/ipc 测试门控对齐、F7 空 key 文案 |

六项修复本体（#1704/#1690/#1691/#1692/#1705/#1693）在其各自声明范围内均验证成立：本地文件逃逸闭合、splice 拒绝收口两个平台、幽灵注册撤销幂等且析构安全、oversized 行立即回错且阈值两侧一致、日志有界且 delta 绝对索引单调、死分支删除经实测零回归。RED→GREEN 证据链完整且断言与 oracle 对齐。

**最终判定：APPROVE**（附条件：F1 作为 #1704 的直接后续必须开票修复——一行 scheme 形状检查即可闭合；F2-F7 建议随手或后续处理）。
