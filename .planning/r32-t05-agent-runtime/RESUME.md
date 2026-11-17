# RESUME — R32-T05 Agent/Pi/Python/SDK 插件轨

固定轨道变量（新 shell / 上下文恢复时必须重新执行）：

```bash
export EXPRS_TRACK=t05-agent-runtime
export EXPRS_REPO=/home/kevin/projects/rs-studio/main          # 只读原工作区
export EXPRS_WT=/home/kevin/projects/rs-studio/exp-rs-r32-t05-agent-runtime-20261116T184011Z
export EXPRS_BRANCH=zcode/r32-t05-agent-runtime-20261116T184011Z
export EXPRS_BASE_BRANCH=master
export EXPRS_BASE_SHA=53412c86bfd5742f0df0243b51500ba444f7dde1
export EXPRS_BUILD=$EXPRS_WT/build-dev
export EXPRS_CONFIG=Debug
export EXPRS_PLAN=$EXPRS_WT/.planning/r32-t05-agent-runtime
export EXPRS_HOST_LOCK=$HOME/.cache/exp-rs-agent/host-build
cd "$EXPRS_WT"
```

- default branch（API 实查 2026-11-17）：`master`
- 远程：origin = https://github.com/WindWang2/exp-rs.git
- gh 已登录（WindWang2）
- 主机：linux cachyos, node v24.21.0, python3 3.14.10
- 构建规范：双层 resource_guard（host-build + build dir），-j4 上限，CTest -j1
- Node 版本注意：宿主 node 24.21.0，产品 Pi SDK 锁 24.22.3 —— 不降低锁要求；需锁定 SDK 路径的测试用真实准备的 SDK 路径（见 pi/ 下锁文件与 scripts/prepare_pi_runtime.py）

## 状态快照（最近检查点 2026-11-17T04:0xZ）
- [x] worktree 创建于 53412c86（origin/master）
- [x] 12 项 issue 修复 + 测试已提交（5 个功能 commit，HEAD `580833562b`）
- [x] configure EXIT=0（OpenCV_DIR 用共享 scratch 惯例；ccache 无效已确认——绝对路径 flag）
- [ ] build 受影响目标进行中（/tmp/t05_build.log，构建含 qgis_core 全量）
- [ ] R1 独立 review 运行中（后台 agent）
- 已验证：JS checkpoint-redaction 4/4；r23_e dtype 5/5 OK；install 规则生成物含 worker（#2109 证据1）
- 恢复提示：构建完成后按 LEDGER "下一步" 跑 ctest；测试环境变量 `SICNU_TEST_PI_NODE=/tmp/pi-stage-t05/libexec/sicnu-geo-rs/node/node.exe`
