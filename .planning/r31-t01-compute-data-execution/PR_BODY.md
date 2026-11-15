# R31-T01: 科学计算、数据与公共执行链的报告/产物身份、缓存恢复与预算真实性

## 原问题与影响

科学/模型/数据/任务执行链上有一类共同缺陷：**"执行过"被当成了"产物可用"**。

- **报告被当成产物**（WP02/WP03）：TaskCenter 把任意字符串参数映射成输出资产，`io:inspect`/`io:doctor`/`io:subdatasets`/`io:verify_dataset` 这类只读报告因此进入 commit/cache/autoLoad 通道；多个显式输出时只保留第一个，其余身份丢失。基线实测：混合字段任务的报告被提交为资产（`commitCalls` 1 对 0、伪造的 `assetId`/output 路径、autoLoad 触发），且 IO 报告工具在输入目录旁写出 19 个文件、登记了资产（`catalogGeneration` 3 对 1）。
- **缓存命中不收敛**（WP04）：`OutputCommitter::reconcilePublished` 对任何已登记路径直接返回既有 AssetId。文件被外部删除后又恢复同样字节时，目录记录仍停留在非 Ready，缓存命中把不可用记录当作已交付返回——永远不会收敛到完整 run 的可用状态；kind 变化的记录被静默复用；缺失的 Derivation Record 永不修复；同路径不同内容的替换可冒充未变化的命中。
- **恢复定位可被 CWD 改写**（WP05）：resume 原样保存相对 checkpoint 路径，启动后 CWD 变化会 persist 到别的文件。
- **VRT NoData 与 fill 混淆**（WP07）：直接 VRT 把输出 fill 写成源 NoData，warp 同时设置 src/dst NoData，源自身声明的 NoData（如 -9999）语义丢失。
- **warp 中间产物弱验证**（WP08）：复用只校验尺寸/CRS/部分 transform，同网格但 band/dtype/NoData/内容不同的中间产物会被复用。
- **ROI 预算在分配后**（WP11）：`maxCandidates` 在 owner hash 与候选集合物化之后才检查。
- **模型卸载按最后一段猜身份**（WP15）：带 `|load:` 的缓存键永远匹配不到。
- **digest memo 混存快照**（WP16）：并发 insert 用新 sample 配旧 digest，后续 probe 长期信任一个从未描述过这些字节的摘要。
- **预算不随输出平面缩放**（WP17）：VRAM 批次门与 plan 的工作集估计用固定输入通道倍数，而 TaskCenter 的 RAM admission 已经按输出类数计费——1 波段 21 类头部在 4 平面估计下被准入。

## 基线

- Track 起始 base：`85ce917bea0ece4df6548a677b97a72ecde8659b`（PR #2067）。
- 本 PR 基于执行时最新 `origin/master`（Pi P2/P3 合并后），与该 base 的唯一重叠是 `task_center` 的增量函数 `isExecutionQuiescent`，无冲突。

## 本次增量（按工作包）

- **WP02/WP03**：`PortDescriptor::producesFile` 区分文件产物与值/报告输出；显式 `outputs: null` 不再合成默认 raster 输出；`AlgorithmTaskInfo` 记录 `outputDeclarationResolved`/`declaredFileOutputNames`；单文件端口按声明名消费，零文件端口不 commit/cache/autoLoad，多文件端口写入 `declaredOutputs` 保留全部命名身份；`io:subdatasets` 的 entries 改为 array 输出、`metadata`/`report` 为 object、`verified` 为 boolean。
- **WP04**：reconcile 按记录事实决策——Ready+kind 匹配+provenance 齐备才是真正的未变化命中（不升 revision、不发 assetChanged）；外部事件留下的非 Ready 记录以稳定 AssetId 重新登记（快照重建、revision 前进、assetChanged），与完整 run 收敛到同一可用状态；仅缺 provenance 时原地 attach（内容事实未动，不伪造 revision）；kind 不匹配以 `output.reconcile_kind_changed` 失败关闭；chunked-run 的通用 published-output 探针在产物带 finalize manifest 时要求通过独立完整性门（同路径不同内容降级为重建），republish 会从新字节刷新已携带的 manifest。`ExecutionPlane::resetForTests()` 修复按 taskId 键控的 commit 缓存在测试主机上的串味。
- **WP05**：resume 进入 coordinator 线程前用 `absoluteFilePath()` 固定 checkpoint 身份。
- **WP07**：源 NoData 只取输入自身声明；warp 只设 `targetNodata = fill`；新增 `preflight.fill_value_alias` 明确披露"有效值等于 fill 无法与未覆盖 fill 区分"的表示限制。
- **WP08**：新增 `sicnu.vrt.prepared/1` 身份 sidecar（内容 SHA-256 + 宽高 + band 数 + CRS + 六项 transform + 每 band dtype/NoData/mask flags），复用前重新哈希并逐项比较，缺失/损坏/不符即重建；sweep 同时保留/清理 sidecar。
- **WP11**：`RsPixelRasterizer::rasterizeEach` + `PixelSink` 在唯一候选写入 `pixelOwner` 前执行 `maxCandidates` admission；缓存 `pixelIndices` 走同一检查；超预算返回 `BudgetExceeded`，不留下半成品。
- **WP15**：raw artifact identity 释放全部 device/load 配置；带 `|load:` 的 identity 只释放精确配置。
- **WP16**：并发 insert 整体替换条目并返回本次自己的 digest；hash 前后各探测一次 sample，跨快照的重写只损失一次未缓存 acquire，绝不缓存撕裂对。
- **WP17**：`declaredOutputChannels`（classes × tensorNames + uncertainty）成为引擎批次门、plan 工作集与 `estimateExecution` 的共同基准；未声明头部的历史倍数保留并在代码中披露。
- **WP18**：HTTP provider 的 event-dispatcher 拒绝、取消与恢复、静默端点超时纳入测试（原仅有错误映射与 body-cap 覆盖）。

## 边界与所有权

- 不触碰 T02 文件（`python_environment.*`、`python_script_operator.*`、`python_environments.qrc`、`src/sdk/exprs/external_process.*`、`src/agent/runtime/**`）、T03 文件（GUI/制图/mapspec/layout）与 #2061 的 active-exclude 业务路径（#2061 在本 PR 准备时仍 open）。
- `tests/CMakeLists.txt` 仅追加本轨独立命名的 `test_r31_t01_*` 块；两处 R30 测试 glob 的修改由生产文件 owner 完成，只为排除身份 sidecar 这一元数据文件。
- 不引入第二套提交器/调度器/验证权威；manifest 门与身份 sidecar 复用既有平台权威（`geospatial/io/finalize_manifest`、`platform/durable_sidecar`）。

## 实际命令与结果

全部在独立 worktree 的 `build-dev` 中、双层 resource guard（host lock + build lock，`--parallel 4 --max-parallel 4`）下完成；测试串行（`CTEST_PARALLEL_LEVEL=1`），Qt 测试子进程 `QT_QPA_PLATFORM=offscreen`。

- `cmake --preset dev-default -DOpenCV_DIR=...`：exit 0。
- `cmake --build build-dev --parallel 4 --target <40 个受影响 target>`：exit 0（0 个 `error:`）。
- 新轨测试（均 exit 0）：
  - `test_r31_t01_result_semantics`：4 cases / 99 assertions（WP02 基线 RED：exit 42、18 条断言失败——报告被提交为资产、输入目录被写 19 个文件、资产被登记）。
  - `test_r31_t01_workflow_resume`：1 / 25。
  - `test_r31_t01_model_identity`：1 / 20。
  - `test_r31_t01_vrt_reuse`：1 / 49。
  - `test_r31_t01_vrt_nodata`：1 / 197。
  - `test_r31_t01_sampling_admission`：2 / 33。
  - `test_r31_t01_output_recovery`：5 / 176。
  - `test_provider_http`：8 / 53（含 3 个新 WP18 用例）。
- 受影响既有套件 40 个 target：**38 个 exit 0，合计 329 cases / 117,720 assertions**；2 个（`test_r30_t03_generations`、`test_r30_t03_vrt_prepare_control`）因 WP08 sidecar 与 warp 文件名前缀相同被旧 glob 多计，测试侧修正后待重跑（见"限制"）。

## ALREADY_DONE_WITH_EVIDENCE（未重复实现）

#2055/#2057/#2058/#2059 已合并的宽计数/稳定回归、ROI 行窗 burn 与取消、sieve 全局标记、RSOperator majority/recode 流式路径、四 lane 执行契约、per-invocation 取消框架、Σ-feed 预算、流式 tile 遍历、VRT post-publish sweep、host-lane CAS、fresh-catalog cache 登记、stats_coverage、resume attempt cap、ADR0172 验证权威均保持为基线，本轨只做剩余闭环。WP06（host-lane/wait/cancel 边界）在本 HEAD 上复跑既有套件全绿，未发现新的可复现竞态。

## 限制与未执行项

- **独立 R1/R2 审查未完成（BLOCKED）**：所有 `Agent` 启动均因提供方月度配额限制失败，无法起干净上下文的 reviewer。已附实现者的自审（`REVIEW_R1.md`），其中 S1（provenance 修复误升 revision）已修复，S2–S4 为披露/接受项。PR 合入前需要一次真正的独立审查。
- **ORT 相关取消隔离 NOT_EXECUTED**：本构建 `SICNU_WITH_ONNX_RUNTIME=OFF`，带 tag 的 ORT 用例跳过。
- **WP09/WP10/WP12/WP13 NOT_EXECUTED**：旧 reader 代际保留、多级 VRT 漂移矩阵、特征提取完整字节 admission、连通区域/sieve 有界数据路径未实现（残差矩阵逐条记录证据）。
- WP16 的并发交错 oracle（固定 A/B barrier）无可确定性进程内复现手段，未执行并在代码中披露。
- 两个 R30 VRT 套件的修正后重跑在 PR 时排队于宿主 build lock（另一轨持有），结果以本 PR 评论或后续 commit 补充。
- 未运行线上 CI，也不以线上状态代替本地验证；Windows 路径未验证（ENV_LIMITED）。

## 相关 issue

Refs #1955（workflow resume 身份，WP05 剩余项）、#1939/#1938（数据生命周期，WP04 消费侧）、#1941（VRT/IO 验证，WP07/WP08/WP19）、#1944（runtime 预算，WP17）、#1949（资源/admission，WP11）。不批量关闭历史 issue。
