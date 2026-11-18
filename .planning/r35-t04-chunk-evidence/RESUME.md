# RESUME — 恢复变量(新shell必须显式重设)
EXPRS_TRACK=t04-chunk-evidence
EXPRS_REPO=/home/kevin/projects/rs-studio/main
EXPRS_BASE_BRANCH=master
EXPRS_BASE_SHA=120677e6e165d3e88bb9d2ec2a3fd98042ec719d
EXPRS_BRANCH=zcode/r35-t04-chunk-evidence-20261118T183755Z
EXPRS_WT=/home/kevin/projects/rs-studio/exp-rs-r35-t04-chunk-evidence-20261118T183755Z
EXPRS_BUILD=$EXPRS_WT/build-dev
EXPRS_CONFIG=Debug
EXPRS_PLAN=$EXPRS_WT/.planning/r35-t04-chunk-evidence
EXPRS_HOST_LOCK=$HOME/.cache/exp-rs-agent/host-build
构建: 双resource_guard(host-lock→build-dir), -j4上限; 测试 ctest -j1

## 检查点 2026-11-19 03:4x (本地)
- 关键环境事实: 本机configure必须带 -DOpenCV_DIR=/home/kevin/projects/rs-studio/main/.scratch/ui-build-deps/opencv/usr/lib/cmake/opencv5 (兄弟轨T05同款; 备选 ~/.cache/exp-rs-agent/opencv5-prefix/lib/cmake/opencv5)
- 状态: 12个WP生产代码+5测试文件全部staged; configure2/buildtest2后台链运行中; R1独立reviewer后台运行中
- 已知背景: T05轨构建曾长期占用host lock(~40min), 已释放
