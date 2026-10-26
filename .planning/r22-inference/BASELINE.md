# R22-03 Inference Track — Baseline (启动事实盘点)

盘点时间：2026-10-25 (UTC)。执行宿主：ZCode /goal-loop。盘点人：r22-inference 轨。

## 仓库与基线

- 仓库：`WindWang2/exp-rs`（viewerPermission: ADMIN）
- 远端 master（gh api `repos/.../commits/master` 实测）：`911e0b17a3533ca0d79c1b68b000d6052c4c3c61`
  （"Merge pull request #1672 from WindWang2/agent/r20-04-qt-qgis-cartography-20261022T190613Z"，2026-10-23 09:17:48 +0800）
- 本地 `origin/master` remote-tracking ref 与远端一致（`git rev-parse origin/master` = `911e0b17…`）。
  注意：本地 checkout 的 master 分支 HEAD（`28e1891390`，2026-10-21）**落后于**远端 master；
  直连 `git fetch origin master` 当时超时（github.com:443 连接失败），但 gh API 与 remote-tracking ref 双重核对一致，
  且 `911e0b17` 提交对象已在本地存在（曾被成功 fetch）。本轨 worktree 直接基于 `911e0b17` 创建，不基于过期的本地 master。
- 本轨 worktree：`/home/kevin/project/exp-rs-r22-inference-20261025T132554Z`
  分支：`agent/r22-inference-20261025T132554Z`（基于 911e0b17，本轨唯一 worktree/分支）

## 启动命令与证据

```
git rev-parse --show-toplevel; git status --short; git branch --show-current; git remote -v
git fetch origin master --prune        # → 网络超时（github.com:443 直连失败）；改用 gh api 核对
gh api repos/WindWang2/exp-rs/commits/master --jq .sha   # → 911e0b17a3533ca0d79c1b68b000d6052c4c3c61
gh repo view WindWang2/exp-rs --json defaultBranchRef,viewerPermission  # master / ADMIN
gh pr list --state open --limit 200     # → #1673 #1674 #1675 #1676 #1677（R21 五轨，全部 open）
gh issue list --state open --limit 500  # → 156 open（含 #1706 P1、#1554、#1553）
gh api "repos/WindWang2/exp-rs/branches?per_page=100"  # → 10 agent 分支 + master，无 r22-inference 冲突
git log -n 40 --oneline origin/master
```

## Open PR（截至盘点，全部 base=master，非 draft）

| PR | head | 主题 | 与本轨关系 |
|----|------|------|-----------|
| #1673 | agent/r21-b-data-model-runtime-20261023T185101Z | data/model-runtime/reproducibility + e2e | **直接文件交集**：`src/operators/runtime/tile_inference_engine.cpp`（mask_threshold 语义门控，见 PR_COLLISION_MATRIX） |
| #1674 | agent/r21-e-teaching-verification-20261023T215154Z | 统一科学验证器 + FaultLab | 无 src/operators/runtime 交集 |
| #1675 | agent/r21-c-pi-mcp-harness-20261023T185308Z | Pi/MCP Harness | 无交集 |
| #1676 | agent/r21-a-scientific-algorithms-20261023T184835Z | 科学算法 | 无交集 |
| #1677 | agent/r21-d-qt-qgis-cartography-20261023T214750Z | Qt/QGIS 工作台 | 无交集 |

## 历史参考 PR（已 MERGED，重叠文件核对）

- #1668–#1672：R20 各轨（已合并进 911e0b17 基线）。
- #1669（R20-02）：改 `tile_inference_engine.cpp`（batching/uncertainty/contract）——已含在基线。
- #1659（R18-b）：恢复 r15-C 全部工作 + feather/发布/分块修复；改 `chunk_graph.*`、`tile_inference_engine.cpp`、`tests/CMakeLists.txt`、`test_model_runtime_9.cpp`——已含在基线。
- #1508（R14-b）：推理运行时/分块加固，改 `tile_inference_engine.cpp` 等——已含在基线。

## 初读文件

- `README.md` / `CMakeLists.txt` / `CMakePresets.json`：确认 Release+ENABLE_TESTS 配置与依赖门（SicnuDepDoctor）。
- `tests/CMakeLists.txt`：test 目标注册模式（SICNU_HAS_OPENCV AND OpenCV_FOUND 块内）。
- `src/operators/runtime/tile_inference_engine.cpp`（4333 行 @911e0b17）。
- `src/runtime/gpu/gpu_plane.cpp`、`src/runtime/chunk/chunk_graph.cpp`。

## 环境事实

- 构建机：40 核 / 62GiB RAM（盘点时可用 ~47GiB）。强制共享锁 `/tmp/exp-rs-r21-build.lock`，`-j4` 上限。
- 依赖：vendor prefix `/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr`（GDAL/PROJ/GEOS/GSL/QCA），
  系统 Qt6 (`/usr/lib/cmake/Qt6`)、OpenCV 5.0.0 (`/usr/lib/cmake/opencv5`)、jsoncpp、protobuf 7.36.1。
  配置命令复制自 r21-B 已验证 cache（见 LOOP_LEDGER 轮次记录）。
- 无 CUDA GPU（后续以 NOT_EXECUTED 标注 GPU 实测项；CPU mock/契约测试不受影响）。
- CI：不参与完成门（不等待、不轮询线上工作流）。
