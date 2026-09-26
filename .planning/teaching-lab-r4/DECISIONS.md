# DECISIONS — Track 12 Teaching & Lab Pack R4

每行一个契约决策（拒绝 or 归一/语义二选一），实现与测试必须与行一致。

## 边界矩阵（WP-A，15+ 类；"R"=拒绝，"N"=归一）

| # | 类 | 语义 | 拒绝/归一后的表现 |
|---|---|---|---|
| B01 | CRLF 混合（text fixture） | **N** | canonical sha256/字节按 git 规范化（CRLF→LF），与 pins 对账 |
| B02 | CR-only（经典 Mac 行尾，text fixture） | **N** | 孤立 CR 按 git 行为保留（不转 LF），digest 按规范化字节 |
| B03 | UTF-8 BOM | **N** | BOM 是内容字节：参与 canonical 哈希（git 不剥离 BOM）；pack JSON 解析侧 BOM 文档 → **R** `lab.pack_schema`（jsoncpp 拒绝）→ admin 映射 `pack_schema` |
| B04 | UTF-16 误标为 .json | **R** | 解析失败 → `pack_schema` error，信息含 NUL/解码事实 |
| B05 | 非 ASCII 资产名（NFC/NFD 变体） | **N** | 路径按 UTF-8 字节精确匹配（不做 Unicode 归一）；pins 对 NFC/NFD 各自独立成立；清单中不存在归并 |
| B06 | 缺失资产（committed-fixture） | **R** | `input_missing` error，offline_available=false |
| B07 | 缺失资产（generated-samples/tmp） | **W** | `input_missing` warning（regenerable）；admin inventory 的 offlineAvailable 仍置 false（保守诚实），verdict 侧由 verifier 报 degraded |
| B08 | 零字节资产（declared>0，committed） | **R** | `byte_mismatch` error（declared vs 0） |
| B09 | 零字节资产（无 declared，committed 无 sha） | **R** | sha 必填（authority `lab.pack_input`） |
| B10 | 目录冒名资产（path 指向目录） | **R** | 非 regular file → `input_missing` 语义类错误（exists&&!isFile 分支） |
| B11 | 重复条目（同 path 两次） | **R** | authority `lab.pack_input` duplicate path |
| B12 | 大小写冲突（a.TIF vs a.tif，大小写敏感文件系） | **N** | 按字节精确路径两独立条目（不做大小写归一）；仅在同字节路径时 B11 拒绝 |
| B13 | 相对路径逃逸（`../`、绝对路径、盘符、反斜杠） | **R** | `path_traversal`（admin 层）+ authority `lab.pack_input` |
| B14 | symlink 逃逸（资产在 root 外） | **R** | `path_escape`（canonical containment） |
| B15 | 清单 digest 与实际字节不符（committed） | **R** | `digest_mismatch` error + digestsOk=false |
| B16 | 清单 digest 与实际字节不符（generated） | **W** | `digest_mismatch` warning（tier 强度跟随） |
| B17 | declared bytes 与实际不符（committed） | **R** | `byte_mismatch` error（canonical 字节口径） |
| B18 | declared bytes 与实际不符（generated） | **W** | `byte_mismatch` warning（GDAL 漂移容忍） |
| B19 | pack 级 declared_offline_bytes ≠ inputs 和 | **W** | `byte_mismatch`（pack 级 warning，信息性） |
| B20 | 超长路径（>PATH_MAX 量级） | **R** | 文件打开失败 → `input_missing` 类错误（打不开=缺失，诚实降级） |
| B21 | JSON 尾随逗号 | **R** | authority `lab.pack_schema`（jsoncpp 严格解析） |
| B22 | 空 pack 目录 | **N** | 空 inventory（不是错误）；byteBudget 内 |

（#1336 合并后 B01-B04 同步在 PackVerifier 直测面复核，语义不得漂移。）

## 批量导入失败语义（WP-B，二选一 → 选定）

**选定：断点续传（durable checkpoint + resume），非全回滚。** 依据：既有 `checkpointPath` 契约（每 16 行 + 末行原子重写；resumed 计数；cancelled 永不入 checkpoint）已在 f3887c7af0 落地并在 `test_teaching_admin_core` 有用例——本轨道**钉死其失败路径**：空批（0 行，typed 空 report 非错误）、全失败（每行 typed，无半批假成功）、同学生多文件（两独立行，发现序确定性）、检查点损坏/外来 digest（降级 fresh start，绝不采纳 garbage）、注入第 N 个失败（前 N-1 已持久化可枚举可续跑）。

## 凭据三路径语义（WP-D）

**三路径同源**：lab_copilot（labAsk/labReference 教师面）、autonomy gate（harness:autonomy_status 会话层）、execution gate（harness:execute_plan 角色降级）全部经 `teacherCredentialValid()`。契约：env 未设/空 → 三路径全部 fail-closed（教师声明降级 student / 会话层忽略）；令牌错误（同长逐字节翻转/长度不等/非串）→ 同上；正确 → 三路径一致放行。`return diff;`→`return diff == 0;` 为三路径共同根修复（与 #1335 P0-1 同一行，rebase 收敛）。**不新增错误码轴**：拒绝走既有 TEACHING_REFUSAL 信封与降级语义；文档段落追加既有运维文档，不新开文档。

## transcript parity 语义（WP-C）

重放真值：事件日志重放结果独立于投影代码。乱序事件 → 投影按 index 归位而非按到达序；并发提交 → 后到者不撕裂先前行；恢复会话 → 转录 fromJson 后再 toJson 字节稳定；部分失败回滚 → 已投影行不携带失败行状态。

## cockpit 权威语义（WP-E）

registry 落盘内容是唯一权威：漂移（改字段/删条目）→ prefill/解析**拒绝**（typed error / UNKNOWN readiness），**不回退硬编码 allow-list**。"空串"与"键缺失"两态分开断言。

## 明确不做（对照铁律）

无新功能/新工作台/新算子/新错误码族/新事务框架/新校验入口（WP-F 复用 WP-A 入口）。`lab_copilot.cpp` 触碰仅 :432 一行（账本记账）。
