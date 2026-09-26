# REVIEW_LOG — Track 12 Teaching & Lab Pack R4

全程评审记录：每个 subagent 评审轮的发现与处置。

## Round R1（实现前）— API 交叉核对（Explore subagent，只读）

对象：6 个新测试文件 vs 真实头文件/实现。
发现 15 处不匹配，全部在编译前修复：

| 类别 | 发现 | 处置 |
|---|---|---|
| 编译错 | `Json::parseFromStream` 无 std::string 重载（2 文件） | 改 `std::istringstream` |
| 编译错 | `test_teaching_batch_r4` 缺 `<stdexcept>` | 补 include |
| 编译错 | `test_lab_pack_boundaries_r4` 用 `kAnySha` 未定义；`std::min(int,qsizetype)` 推导失败 | 本文件定义常量 + `std::min<qsizetype>` + `<algorithm>` |
| 必挂断言 | autonomy session block 缺 `schema: sicnu.autonomy-policy/1` → 会话层永不生效 | 测试补 schema 字段 |
| 必挂断言 | `labUiStatusFromWire` 等失败时不改写 out（合同为"保持调用方值"） | 断言改为保持原值 + 合法 wire 往返 |
| 必挂断言 | parity 基线 transcript 含真 fail（evidence a2）→ overall=fail 非 indeterminate | 期望改为 fail（更强断言） |
| 必挂断言 | alien 文档含 `steps` 会被路由进 fromLabSpec 而非 unknown 分支 | 移除 steps 键 |
| 细节 | B14 消息不含资产名（needle 改按消息固定文本）；B19 path 字段校验；B17 offlineAvailable 不随 byte_mismatch 降级；B01.. 写文件缺 data/ 目录 | 逐一修正 |
| 命名冲突 | 测试本地 sha256Hex 与 json_util.h 同签名（using-directive 二义） | 改名 oracleSha256（并明确其"独立 oracle"身份） |

## Round R2（实现后）— 假绿陷阱确诊（过程发现，非 subagent）

`raise-compiler-stack.sh` 对确定性编译失败重试 12 次后 exit 0 且不产出 .o
（实测复现）。副作用：三处"失败"实为陈旧对象（core:1241 bytePin、batch BOM、
boundaries B17 零字节）。处置：/tmp/r4_verify.sh 协议（删对象真重建+全量日志
FAILED 检查）；脚本修复移交（白名单外）。

## Round R3（收口前）— 独立对抗性评审（general-purpose subagent，只读）

对象：`git diff 15e5c66b5..HEAD` 全量 + 独立实跑 4 套件（92/18、77/7、46/8、29/7，与文档声称精确一致）。

**总裁决：YES-WITH-FIXES。P0：无**（两个真缺陷修复——令牌门反转、BOM——均确认正确且必要）。

| 级别 | 发现 | 处置 |
|---|---|---|
| P1-1 | json_util.h 注释现在时声称"mirror 权威 fileSha256"，但本基（#1336 未合并）权威仍按原始字节——合并序耦合未在代码注释披露 | **已修**：注释改为如实陈述合并序事实 + "#1336 合入后本入口须委托权威"的义务说明；测试头文件同步勘正 |
| P1-2 | 流式 pending-CR 跨 chunk 逻辑零直接覆盖（全部 inventory fixture <8000B；canonicalFileSha256 无直接调用点） | **已修**：CHUNK-1（64KiB 边界 CRLF + EOF 孤立 CR，对整段 oracle 直测）、CHUNK-2（NUL@7999/8000 探测边界 + 可观测 CRLF 差异）、CHUNK-3（空文件/全 CR 恒等） |
| P2 | B03/B18 两类无直接用例（矩阵"22 类"实为 20 类有例） | **已修**：B03（BOM 参与哈希）、B18（generated 字节漂移 warning）各补一例 → 22/22 类全有例（23 用例 121 断言） |
| P2 | parity"byte-stable"名不副实（Json::Value 语义比较非字节比较） | **已修**：升级为 jsoncpp 重序列化字节串比对 |
| P2 | preflight digest 同对象两次调用近同义反复 | **已修**：两次独立 runPreflight 摘要相等 + 输入扰动摘要必变 |
| P2 | 22/22 表第 3 行声称未行使的"不可读文件"路径 | **已修**：措辞改为实际行使路径 |
| P2 | canonicalSize=-1 时 byte pin 静默跳过（基线 fi.size() 仍可报） | 接受：committed 层 sha256 必填（B09 authority 兜底）使该路径实际不可达；已在代码注释与表格中如实标注 |
| P2 | 并发 parity 用例近乎必过（被测为纯函数） | 接受：定位为防回归护栏（纯函数化回归即变红） |
| 确认 | 越界 C1 / 新轴 C2 / CMakeLists 冲突 C3 / 文档诚实性 D1 / LF 回归 E1（48/48 pin 独立复算） | 全部通过 |

处置后复验：/tmp/r4_verify.sh 全绿（boundaries 121 断言/23 用例，parity 47，authoring 30，零回归）。
