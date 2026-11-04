# PR_COLLISION_MATRIX — R25-C

基线时点：0 open PR（实时查询）。并行轨 worktree：R25-A（science）、R25-B（data/model）同期创建。

## 与并行轨的共享文件风险
| 文件 | 风险 | 协议 |
|---|---|---|
| `tests/CMakeLists.txt` | A/B/D/E 各轨都会追加注册块 | 仅末尾追加 `# BEGIN R25-C`/`# END R25-C` 标记块；不重排/去重/删除他人块；push 前重新 fetch 合并 |
| `.gitignore` | 各轨追加 planning allow-block | 末尾追加本轨块 |
| `tests/surface_diff_snapshot.json` | 任何工具 schema 变更都触碰 | 本轨 W3 变更 get_workflow_status schema → 需再生；push 前检查新增 PR 是否也改（冲突则再再生，保并集） |
| `pi/*` | 本轨独有（历史无他轨触碰） | 单写者 |
| `src/agent/mcp_server.*`、`src/agent/tool_catalog/meta_protocol_tools.cpp` | 上轮 r23-C/r24-C 已合并触碰，但当前无 open PR | 单写者；push 前 fetch 复查 |
| `src/workflow/workflow_run_coordinator.*` | r23/r24 已合并触碰，当前无 open PR | 单写者；仅加只读方法 |

## push 前复查清单
- [ ] `git fetch origin --prune` 后 `gh pr list` 仍为空或无路径交集
- [ ] `python scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs`（存在则跑）
- [ ] 与新增合并 commit 的 changed files 求交集，有交集先 merge 验证
