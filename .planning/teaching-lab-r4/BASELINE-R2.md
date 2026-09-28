# BASELINE — Track 12 Round 2（R4 收尾轮，2026-09-28 实测）

## 1. 基线与在途状态（实测，非沿用）

| 项 | 实测值 |
|---|---|
| `origin/master` | `a726d17a62`（#1354 合并点；fetch 实测） |
| 本轮分支 | `hardening/r4-teaching-lab-r2`（复用 worktree `exp-rs-teaching-lab-r4`，物理隔离不变） |
| Round 1（PR #1352） | **MERGED**（390d1b10e5，18+ 提交/20 文件）——R4 主体交付已并入，本轮不重做 |
| PR #1335 | **MERGED**（61de7ff95a）——P0-3 链接修复与 P0-1 令牌行已进 master |
| PR #1336 | **MERGED**（c1d43a3fc0）——lab_pack.cpp `fileSha256` canonical 化与 gen_lab_packs.py 已进 master |
| 令牌行收敛 | `lab_copilot.cpp:432` = `return diff == 0;`（#1352 携带行与 #1335 P0-1 同内容自然收敛，无冲突残留） |
| 当前开放 PR | 仅 #1365（r5-persistence，另一轨）；与本轨白名单唯一交集 `tests/CMakeLists.txt` |
| 重叠策略 | 本轮**不新增测试目标**（只扩展既有 `test_lab_pack_boundaries_r4.cpp`），完全避开 `tests/CMakeLists.txt` 与 #1365 零重叠 |
| 开放 issue | 0 |

## 2. 锚定复核（实测）

- teaching_admin 头 15 个、teaching 头 7 个，合计 **22/22** 逐一在位（`ls` 实测）。
- 本轨 ctest 过滤面目标（`sicnu_add*` 三族注册宏并集 + 名匹配 lab/teaching/pack/copilot/autonomy）：**43 个**。
- Round-1 基线为 148 实跑 + 19 NOT_BUILT；#1335 已修 `libsicnu_agent.so` 未链 `agent_loop` 的库级缺口
  （src/agent/CMakeLists.txt:279-309 实测在位）→ 本轮基线预期 43 目标全部构建，19 个恢复目标首次实跑。

## 3. 本轮开工即修的 master 级 P0

`tests/CMakeLists.txt:14728` 残留孤立 `=======` 冲突分隔行（R4 合并列车 `7bb6398c05`
tmp-merge-1345 删了 `<<<<<<<`/`>>>>>>>` 漏了分隔行）→ **全仓库 configure 解析失败**。
提交 `7999b5ad8a` 删除该行（两侧内容块均合法，原样保留）。同类标记全仓扫描：0 处残留。

## 4. 本轮范围声明（收尾轮，不做而做的都列明）

Round-1 PR #1352「未解决项」中以 #1336 合并为前提的两项，现在前提成立：

1. **遗留5（原 P1-1）**：`teaching_admin/json_util.h` 的 `canonicalFileSha256/canonicalFileSize`
   改为委托 `sicnu::labpack` 权威实现（#1336 的 `fileSha256`），删除 admin 侧镜像
   `detail::canonicalFileDigest`。委托陷阱（本轮识别）：两实现分块对齐不同
   （镜像=8000 字节头后 64KiB 分块；权威=offset 0 起 64KiB 分块），CHUNK-1 fixture 必须按权威
   对齐重造，否则跨块 pending-CR 逻辑失去专属覆盖。
2. **遗留3**：边界矩阵延伸 `PackVerifier::verify()` 直测面——现有 B01-B22 经 admin
   `inventoryPacks` 与 `loadFromBytes` 覆盖，权威类自身的 EOL/编码裁决直测缺位。

不做：Round-1 已交付的 22/22 失败路径表、B01-B22 语义、批量/parity/凭据三路径语义
（全部已并入 master，重做即重复）；白名单外任何目录；#1365 在途文件。

## 5. 基线红绿分布

- 构建协议：沿用 Round-1「删对象真重建 + 全量日志 FAILED 检查」对抗
  raise-compiler-stack.sh 假绿；`ninja -C build-dev -j2` 43 目标，日志
  `/tmp/r2_baseline_build.log`。
- 基线 ctest：构建完成后 `ctest -R "lab|teaching|pack|copilot|autonomy" -j1 --timeout 900`
  实测记录（见 EVIDENCE-R2.md §1，含 19 个恢复目标的首次红绿分类）。
