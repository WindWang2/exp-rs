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

（评审完成后回填：P0/P1/P2 分级发现与处置、终审裁决）
