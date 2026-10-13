# RISK_REGISTER — r9-science-data-integrity

| # | 风险 | 概率/影响 | 缓解 |
|---|---|---|---|
| 1 | MSVC 全量构建耗时长/-j2 限速，阻塞 red/green 验证 | 高/中 | 构建后台跑，期间完成全部源码分析与 red 测试编写；定向 target 优先 |
| 2 | resources/icons junction 造成 git status `D resources/icons` | 中/低 | 不入提交（不 stage）；主 worktree 同状态；必要时提交前临时恢复占位文件 |
| 3 | Windows lane 无法直接验证 POSIX 专属行为（冒号路径、localhost 解码） | 高/中 | 平台感知断言 + QUrl 行为依据 Qt 文档/源码 + NOT_VERIFIED(Linux) 标注；不虚构通过 |
| 4 | 跨界文件（model_catalog.cpp、rs_json_params.cpp）与 B 方向并行改动冲突 | 中/中 | 仅最小追加、PR 明示；结束前 fetch 重查新 PR/冲突 |
| 5 | CTest 既有家族在 Windows lane 的已知 flaky/失败干扰判定 | 中/中 | 以 BASE 构建先跑一遍定向基线，区分既有失败 vs 本分支回归 |
| 6 | durable_sidecar/stepFailed 修复触碰共享契约，可能影响 B/C 测试 | 低/中 | 只收紧错误可见性不改成功路径语义；跑相邻套件 |
| 7 | 内存压力（32GB 物理且采集时空闲仅 4GB）OOM 风险 | 中/高 | -j2 上限、CTest -j1、监控 RSS，必要时 -j1 |
