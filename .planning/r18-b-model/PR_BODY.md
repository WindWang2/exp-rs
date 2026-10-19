# hardening(r18-b): 遥感 AI 模型推理 — 恢复被合并事故回滚的 r15-C 全部工作 + 共享会话/发布/分块 P1-P2 修复

Branch `agent/r18-b-model-runtime-perf`（worktree `exp-rs-r18-b-model-runtime`），base `origin/master` = `2d2881631fc821537100c00da677888e3a31ea8f`。

## 0. 关键事故发现与恢复（本 PR 的第一 commit）

**合并 a8d5cd510（#1516，r15-D "keep both track ignore rules"）在冲突解决中把已合并的 r15-C 轨道（#1515）整体回滚**：14 个源码修复、6 个新测试文件、4 个被修改的测试文件、.planning 文档（共 −3893 行）。git 证据：`f326d8ca9`（#1515 合并）持有全部内容 → `a8d5cd510` 全部缺失 → 之后零提交触碰这些文件。r18-B 按域继承原则从 `f326d8ca9` **逐字恢复**（commit `restore(r15-c)`），并重插其 tests/CMakeLists.txt 注册块（原锚点）。恢复项覆盖 #1515 记录的 C02/C04/C05/C07/C08/C11/C12/C13/C14/C16 全部修复与测试。

跨轨影响（只记录，未越界修）：同一合并还删了 `.planning/r15-a` 文档并回滚了 `src/processing/algorithms/image_fusion.cpp` 的 r15-a 修复（R18-A 域）→ 见 `.planning/r18-b-model/CROSS_TRACK_FINDINGS.md`。

## 1. 新修复（全部经 RED→fix→GREEN×2；证据见 .planning/r18-b-model/）

| WP | 缺陷 | 严重度 | 证据 |
|---|---|---|---|
| W1 | OpenCvDnnRuntime::infer() 在 **m_inferMutex 释放之后**才 clone forward() 的内部输出 blob —— 共享缓存会话上下一个 worker 的 forward 在 clone 读时覆写缓冲，撕坏即将发布的 tile（inferMulti 已在 r14-B 于锁内 clone，单输入路径漏掉） | **P1** | test_r14_B_runtime_providers 新 case（自生成 4-op ONNX fixture，forward 返回内部缓冲） |
| W2 | python worker **写超时**分支在 worker 存活时既不 stopWorker 也不标 permanentlyDead —— 半写请求留在管道里，worker 稍后作答，**下一个请求读到上一个请求的答案**（读侧三个分支早已 fail-closed，写侧漏掉） | **P1** | test_provider_python 新 case（py_worker_slow_start.py：ready 后 sleep 过写期限） |
| W3 | runMultiInput 的 **EO 强制校准 preflight 在 publishStaged/sidecar/disarm 之后**才执行 —— 强制拒绝的运行把完整产品+sidecar 留在输出路径上（“失败”的运行其实已发布） | **P1** | test_multimodal_inference 新 case |
| W4 | feather 混合下，高于累积器行窗口（tile+2·halo）的 all-NoData 延迟块：addInvalidMask 把窗口外的 NoData 判定**静默丢弃**，下一个有效 tile 的 halo 权重把预测**伪造到 NoData 地物上**（A4 契约违例）；尾部超窗时 finish() 还会读越界行 | **P1** | test_model_runtime_9 新 case（头/尾两种形态） |
| W5 | scene 分类 lane 的波长窗 preflight 不传有效波段选择 —— role i 永远对照文件 band i+1：选段输入错拒/错放 | P2 | test_eo_platform_10 新 case（同栅格 bands={2} 应过、bands={1} 应拒） |
| W6 | model-publish lane（ProductPublishGuard::publishStaged + publishProvenanceSidecar）**零 fsync** —— 掉电后 rename 完好但内容可为零长/截断（检测 lane #1174 与 chunk 家族早已 fsync；本 lane 是孤例） | P2 | 与 #1174 的代码一致性 + 恢复的 publish 套件在 fsync 路径上回归（掉电注入不可行，如实披露） |
| W7 | ChunkGraph sink 不受单消费者守卫约束（两种注册顺序都静默共享队列分片 tiles）；graph/pipeline 的 producer 契约门对全默认 payload（null pixels, 0==0）**放行** | P2 | test_chunk_graph 新 case ×3 |

## 2. 只改已有模块的证据

- `git diff --name-only origin/master...HEAD` 全部落在 `src/operators/runtime/**`、`src/runtime/chunk/**`、`src/operators/framework/{model_catalog,chunked_run}.*`（后者为恢复 #1516 回滚所需，见 OWNERSHIP.md 说明）、`models/**` 未触碰、tests 为本轨已注册文件 + r15-C 原注册块重插。
- 与 open PR #1513（r15-B geodata）零文件交集；唯一共享文件 `tests/CMakeLists.txt` 的插入块在 model-runtime 区（~3043 行），#1513 追加在尾部，git 可干净合并。

## 3. 测试与验证

- 恢复套件（6 个 test_r15_C_* + 修改的 4 个文件）对恢复后的源码全部 GREEN ×2 —— 即恢复内容自洽。
- W1-W7 RED→GREEN×2（详见 TEST_RESULTS.md：命令、退出码、双重运行）。
- 回归：test_model_runtime / test_model_runtime_9 / failure_matrix / model_tasks / ensemble / chunk / multimodal / provider 套件。
- 性能：见 PERFORMANCE.md（同输入 ≥3 次对照）。
- NOT_EXECUTED：CUDA/TRT/OpenVINO/ORT 真机车道（无硬件/SDK）；掉电注入（W6 用代码一致性+回归替代）；Windows 专属路径；线上 CI（issue #1491：master CI 自 2026-10-11 起无完成记录；按任务书不等 CI）。

## 4. 独立 Review

两轮（reviewer 未参与生产代码）：REVIEW_R1.md / REVIEW_R2.md。

## 5. 遗留与建议

- KNOWN_FINDINGS.md：16 项经审计验证但本 PR 未修的 P2/P3（ORT 无超时车道、注册表锁内析构、旋转地理变换检测框、ensemble ±Inf 门等）。
- 强烈建议维护者对以后每个触碰 .gitignore 的合并跑 `git diff --stat <merge>^1 <merge>` 复核删除内容（#1516 事故的根因模式）。
