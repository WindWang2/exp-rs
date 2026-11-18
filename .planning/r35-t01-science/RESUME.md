EXPRS_TRACK=t01-science
EXPRS_BRANCH=zcode/r35-t01-science-20261118T155456Z
EXPRS_BASE_SHA=120677e6e165d3e88bb9d2ec2a3fd98042ec719d
EXPRS_WT=/home/kevin/projects/rs-studio/exp-rs-r35-t01-science-20261118T155456Z
EXPRS_BUILD=/home/kevin/projects/rs-studio/exp-rs-r35-t01-science-20261118T155456Z/build-dev
EXPRS_CONFIG=Debug
EXPRS_PLAN=/home/kevin/projects/rs-studio/exp-rs-r35-t01-science-20261118T155456Z/.planning/r35-t01-science

## 检查点 2026-11-19 00:42 (本地) / 2026-11-18T16:42Z

- 首切片 WP01–WP06 已实现（未提交），2 个新测试文件 + CMake 注册 + .gitignore 白名单。
- 首次 configure 实际未运行（host 锁 45s 等待失败，退出码被管道 tail 掩盖——已修正为 pipefail+显式标记）。build-dev 尚不存在。
- 当前有一个 60min 长等待的 configure+build 后台任务（host 锁 + build 锁）。host 锁被兄弟轨（T06/T02/T03 轮流）持续占用。
- R1 独立 review agent 已在后台运行（审未提交 diff）。
- 下一步：构建成功 → ctest 六目标（2新+4旧）→ 回填 LEDGER → 处理 R1 发现 → 提交 → R2 → 刷新 master → push + PR。

## 检查点2：host OpenCV 依赖（重要，后续 shell 必须带上）

- 本机无系统 OpenCV CMake 包；configure 必须加
  `-DOpenCV_DIR=/home/kevin/projects/rs-studio/main/.scratch/ui-build-deps/opencv/usr/lib/cmake/opencv5`
  （host 级共享依赖前缀，所有兄弟轨同用；已核实 OpenCVConfig.cmake 存在）。
- R1 独立审查完成：0 P0/0 P1/7 P3，3 条已修（正则大小写、注释更名、恒真断言），详见 REVIEW_R1.md。
- R1 后新增改动：satellite_products 正则加 CaseInsensitiveOption；image_enhancement 注释更名；change_detection IR-MAD 注释补角落说明；测试删恒真断言。
