# BASELINE — R35-T02 model services

刷新时间（本机）：2026-11-18 23:55 (+08) 前后，与快照一致后开工。

- default branch：`master`（gh api 实测，一次 TLS 超时后重试成功；与快照一致）
- origin/master = `120677e6e165d3e88bb9d2ec2a3fd98042ec719d`（`fix(execution): isolate retries to the current task generation (#2147)`，2026-11-18 22:32:59 +0800）
- open PR：0（`pulls?state=open` 分页实测）→ 活动 PR 保留集为空
- remote branches：29（与快照一致，无未关联 PR 的公开活跃分支）
- 相对 R34 base 358343c7：147 files, +11370/-369（与快照一致）
- 本 checkout `/home/kevin/projects/rs-studio/main` 停在 59edc791（旧 master），保留原状未 reset/clean；本轨工作全部在新 worktree。

## worktree

- WT：`/home/kevin/projects/rs-studio/exp-rs-r35-t02-model-services-20261118T155435Z`
- branch：`zcode/r35-t02-model-services-20261118T155435Z`，base=origin/master 120677e6
- build：`build-dev`（dev-default preset，Debug）；CTest `-j1` + offscreen；host lock `~/.cache/exp-rs-agent/host-build`，-j4 上限（用户本批明确）。

## 已合并基线（继承、不重做）

- #2144（e6791c81→master）：VRAM checked admission、wire typed 拒绝、rank/byte/base64 解码前准入、4096 output 初始 reserve（非总条目 cap）。
- #2137：multi-head feather、NoData pending flush、TensorBlob checked 算术、provenance no-throw。
- #2142/#2143/#2145/#2147：科学/Mission/数据/执行域，均已合入，相邻能力可消费（owner 仍归各轨）。
- 旧 PR 排他全部解除；本快照 0 open PR → 无活动保留区。执行中出现新 PR 再动态登记到 PATH_OWNERSHIP.md。
