# PR 草稿 — Track 15: fix(runtime,python): harden model-runtime boundaries, P1-8 manifest error contract and python worker channel (R4)

（提交 PR 时以本文件为底稿，gh pr create --body-file）

## 标题对齐说明
任务建议标题为"模型运行时与 Python 集成加固 R4"；实际内容为**既有模块的缺陷收口与边界加固**
（四子域边界用例、P1-8 manifest 校验错误契约、OOM 阶梯真实路径验证、provider 守护矩阵、
crash-orphan 竞争 fence、python worker 通道边界），无任何新功能方向/新算子/新 provider。

## 一、基线
- `origin/master` = `15e5c66b543ef3874cb929f17529ef456bd6c059`（PR #1333 合并点；2026-09-27 实测无漂移）
- 本分支领先/落后：（回填 `git rev-list --left-right --count origin/master...HEAD`）
- 构建：独立 worktree + `build-gcc15`（gcc-15/Debug/ENABLE_TESTS=ON/-j2；本机 GCC16 有 Debug ICE）
- 基线 ctest 红绿分布：（回填）

## 二、与在途 PR 的文件重叠
- #1334（open，30 文件）：其 model_catalog hunk 仅 defaultModelsDirectory；解释器白名单/worker_script
  containment 在 python_worker_provider 的 **acquire 面**——本轨道的 P1-8 是其明示不处理的
  **catalog 校验面错误契约**，只消费不重复。若 #1334 后合并，union 点在 model_catalog.cpp
  的 defaultModelsDirectory 与 python_worker_provider（本轨道未触碰其安全语义）。
- #1335/#1339/#1340/#1336（open）：共享 `tests/CMakeLists.txt`（本轨道全部为尾部追加 hunk，可 union）。
- #1335 属 master 预存编译断裂（本机实测 `test_lab_data_pack` 缺 Qt include 无法编译，-k 绕行；
  该文件不在本轨道白名单，未触碰）。

## 三、逐 WP 根因与修复
（回填）

## 四、ADR 0130 六条三态对账表
见 `.planning/model-runtime-r4/BASELINE.md` §3。

## 五、18 用例子域分布表
见 `.planning/model-runtime-r4/SUBDOMAIN_BOUNDARIES.md`。

## 六、OOM 真实/模拟层级表 + provider 矩阵
见 `.planning/model-runtime-r4/PROVIDER_OOM_MATRIX.md`。

## 七、用户可感知的行为变化
（回填：python worker 崩溃原因从固定字符串变为带退出分类与 stderr 摘要；并发发布同路径的
第二个请求获得 typed AlreadyRunning 拒绝；manifest 校验拒绝消息携带 manifest 路径前缀与
期望/实际值；其余为测试与文档。）

## 八、本地验证（未等待线上 CI）
（回填双跑命令与结果）

## 九、未解决项（移交对象明确）
（回填）
