# PR BODY 草稿（提交时按最终数字定稿）

## 标题

fix(teaching,lab-pack): teaching-chain R4 hardening — pack boundary matrix, credential three-path fail-closed, transcript parity, batch failure semantics

## 基线与在途 PR 重叠

- 基线 `origin/master` = `15e5c66b5`（#1333 合并点；fetch 时未前进）。本分支领先 17 提交、落后 0。
- **#1336（未合并）**：拥有 `src/lab_pack/lab_pack.cpp` 与 `scripts/gen_lab_packs.py` → 本分支全程未触碰这两个文件（写作提示声称 #1336 还改 `tests/test_lab_data_pack.cpp`，实测不改——已勘误记录）。WP-A 的边界矩阵因此先落 admin 层（`json_util.h` 单入口 + `data_pack_manager.cpp`），复用 `lab_pack.h` 公有原语，未造第二套校验。
- **#1335（未合并）**：拥有 `lab_copilot.cpp` 与 `test_teacher_credential/test_lab_grading/test_harness_lab_evals` 接线 → 本分支不接这 3 个目标；`lab_copilot.cpp:432` 在 master 仍是反转缺陷（`return diff;`），本分支携带与 #1335 P0-1 相同的一行修复（rebase 时同内容自然收敛），账本逐处记账（共 1 处）。
- 开放 issue：0。本地已验证、未等待线上 CI。

## 逐 WP 根因与修复

### WP-A pack 校验边界矩阵（admin 层）
- 根因：admin 清单的 digest 比对（`sha256OfFile`，磁盘原始字节）与 byte pin（`fi.size()`）都是 git 规范化前口径——#1336 修了 lab_pack.cpp 同类缺陷并在未解决项里移交本文件（:258；:226 同类）。
- 修复：`json_util.h` 新增 `canonicalFileSha256/canonicalFileSize` 单入口（NUL@8000→二进制原样；否则 CRLF→LF、孤立 CR 保留、分块流读），inventory 的 digest/byte pin/pack 级 declared 全部换口径；零字节 committed fixture 是 typed byte_mismatch。Linux LF 检出下逐位不变（既有 3409 断言零回归为证）。
- 矩阵：B01-B22 共 22 类（DECISIONS.md），18 用例 92 断言（`test_lab_pack_boundaries_r4`）。真值来自测试内独立实现的 canonical-bytes oracle（Python canonical_bytes 同源规则），非被测代码反算。

### WP-B 批量失败语义
- 语义二选一：**断点续传**（DECISIONS.md 文档化）——钉死既有 checkpoint 契约的失败路径：空批、全败、throw 隔离、重复发现、损坏/外来 checkpoint 降级、取消行永不入 checkpoint、重启续传。
- **顺带真缺陷**：CSV BOM 双重编码（`QStringLiteral("\xEF\xBB\xBF")` 是码点不是字节 → 落盘 C3 AF…，表格软件不识别；与 run_classroom_batch.py 失去 parity）→ 改为字节级 BOM。

### WP-C transcript parity 残余
4 用例：恢复字节稳定、乱序不变性、部分失败转录完整性、并发==串行；附会话族 3 模块失败路径。

### WP-D 凭据三路径 fail-closed
- 根因：`constantTimeEquals` 返回原始 diff（相等→假、不等→真——完全反转，任何错误令牌被接受、正确令牌被拒）。一行修复 `return diff == 0;`。
- 三路径一致性（labAsk 教师面 / harness:autonomy_status 特权会话层 / labReference 成绩引用）× 4 token 态（未设/空/同长错值/非串），错误码轴不变（TEACHING_REFUSAL）。
- 令牌轮换运维段落追加进既有 `docs/agent/lab-copilot.md`（不新开文档）。

### WP-E cockpit 权威一致性
registry 漂移（未知算子+非空参数）→ prefill 拒绝而非硬编码回退；空/缺两态分明；篡改 student view 被 leak oracle 按坐标点名；course home/autonomy display 对缺失与异 schema 文档 ok=false；status wire 未知拼写拒绝且不改写调用方值；readiness 全缺输入降 UNKNOWN。

### WP-F authoring 失败路径 + fixture 一致性
unsafe_ref（逃逸形引用）、重复 id/index、metric-kind 缺 metricKey、preflight 门未过 ok=false 且 canonical digest 稳定、operator catalog 按单 sidecar 降级、fixture 引用缺失/漂移资产在 inventory 入口红——复用 WP-A 同一校验入口。

## 22/22 头文件模块失败路径总表

见 `MODULE_FAILURE_PATHS.md`（22 行，每行 typed 断言 + 提交号；评审抽查 5 行可追溯）。

## 用户可感知的行为变化

1. 教师令牌门在 master 上是打开的（任何错误令牌被接受）——本分支修复为 fail-closed。
2. Windows CRLF 检出下，教师控制台的 pack 清单不再出现假 digest/byte 漂移错误。
3. 批量导出 CSV 的 BOM 变为表格软件可识别的真 UTF-8 BOM。

## 本地验证

- 轻闭包（删对象真重建协议，对抗 raise-compiler-stack.sh 假绿）：16 二进制全绿，新增 244 断言、既有 5459 断言零回归；48/48 仓库 pin 跨语言对账通过。
- 完整 `ctest -R "lab|teaching|pack|copilot|autonomy" -j1 --timeout 900` 连续两轮：**148/148 实际运行的用例全绿，零新增失败**；19 个 Not-Run 为 master 既有链接破损（libsicnu_agent.so 未链 agent_loop，#1335 P0-3 同根因；本分支未触碰 src/agent/CMakeLists.txt），其中 test_lab_chains 已由本分支恢复（112 断言全绿）。
- 编译资源：全程 -j2；未轮询线上 CI。
- 编译资源：全程 -j2（RSS>70% 降 -j1 未触发）；未轮询线上 CI。

## 未解决项（诚实披露）

1. `cmake/raise-compiler-stack.sh` 对确定性编译失败重试 12 次后 exit 0 且不产出 .o（实测复现）——本轨以"删对象真重建"协议自保；脚本在白名单外，移交修复。
2. `test_lab_data_pack.cpp` master 既有编译破损（#include <QJsonValue>）——#1335 认领，本轨未碰；其余 18 个 NOT_BUILT 同属 #1335 P0-3 链接根因（libsicnu_agent.so 未链 agent_loop），建议 #1335 合并后全部自动恢复。
3. WP-A 矩阵的 lab_pack.cpp 直测面（PackVerifier EOL 类）待 #1336 合并后 rebase 延伸（本次以 admin 层入口 + loadFromBytes 内存面覆盖）。
4. 本分支携带的 `lab_copilot.cpp:432` 一行修复与 #1335 P0-1 同内容——#1335 先合并时按"同内容行"自然收敛；两 PR 同时到达 master 时可能需要一次 trivial 冲突确认。
5. admin/agent 裁决在 Windows CRLF 检出下的分叉（P1-1）依赖 #1336 的落地时点：#1336 合并后 admin 的 canonicalFileSha256 应改为委托 lab_pack 权威，消除镜像实现。

## 零新方向声明

diff 无新功能/新工作台/新算子/新实验/新错误码轴/第二套校验入口。触碰文件全部在白名单内（lab_copilot.cpp 扩展点 1 处，已记账）。
