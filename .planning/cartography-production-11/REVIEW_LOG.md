# REVIEW_LOG — cartography-production-11

Reviewer A（subagent #2，只读对抗审查，全 diff `a5b11b7f...HEAD`，55 次工具调用）+ Reviewer B（主 agent 全量自查）。
结论：**P0=1、P1=3 → 全部修复并回归验证；P2=6（5 修复、1 disposition）；P3=5（2 修复、3 disposition/记录）。**

| # | Sev | 摘要 | Disposition | 修复位置 |
|---|---|---|---|---|
| 1 | P0 | series 物化违反 pages[k]=物理页 k+1 约定（多出尾空白页） | **已修复**：planSeriesPages 重构——row 0 原位变更文档体（元数据上 `page`），pages[] 仅 k≥1；索引页标签 page=pages.size()；mapspec.cpp v6 校验扩展到体 `page`；新增 compile-only 页数回归测试（3 行→恰 3 物理页） | series_planner.cpp / mapspec.cpp / 测试 |
| 2 | P1 | manifest 阶段取消不回滚已交付产物 | **已修复**：atlas 分支删除 deliveredPaths、单页分支 rollbackArtifacts | produce.cpp |
| 3 | P1 | whitespace 规则测量含不可移动 frame/inset → repair 不收敛 | **已修复**：测量集=可移动集（排除 map_frames/inset_maps）；目录/注释同步；新增收敛回归测试（位移>0 且复检无该码） | quality.cpp / 测试 |
| 4 | P1 | repair 添加的图例仍可触发 legend paint 挂死（"渲染门"不是运行时门） | **已修复**：repair 循环后重跑 hazard 检查（门序：require_preflight → atlas 结构 → 渲染危害 → export，保证各拒绝码不互相遮蔽）；atlas 拒绝/取消/strict fixture 加声明 columns>1 图例隔离变量 | produce.cpp / 测试 |
| 5 | P2 | dock 每次 submit 泄漏一条 taskUpdated 连接 | **已修复**：progress 连接挂到 per-job lifetime 对象 | cartography_dock.cpp |
| 6 | P2 | max_pages 无引擎级钳制；manifest 写侧无 512 上限 | **已修复**：clamp(1..512) + writeExportManifest 拒绝超限 | export.cpp / export_manifest.cpp |
| 7 | P2 | 回滚可能删除被替换的预存在文件；MANIFEST_FAILED 信封携带已回滚 artifact 路径 | **部分修复+disposition**：失败信封清空 artifact_path（已修）；"rename-over 交付即 commit，事后回滚不还原被替换的预存在文件"记录为已知边缘（与单文件导出既有语义一致），limitations.md 不再声称"目录内容逐字节不变"而是"不残留半成品" | produce.cpp |
| 8 | P2 | 目录/文档写 2.5× 与实现 3×+35% 漂移 | **已修复**：preflightRuleCatalog 与 production.md 同步为 3×/35%/12mm | quality.cpp / production.md |
| 9 | P2 | mapspec-reference v5 条目被 v6 插入撕裂 | **已修复**：v5/v6 条目重写完整；补充"物理页 0 元数据在体 page"说明 | mapspec-reference.md |
| 10 | P2 | vector 源零覆盖；series 不编译验证；manifest-cancel 未测 | **部分修复**：新增 vector 负例（未知层/坏 filter）、series compile-only 页数测试、whitespace 收敛测试；manifest-cancel 负例与 e2e 属渲染门（主机限制，opt-in） | 测试 |
| 11 | P3 | buildIndexText "Page " 悬垂 | **已修复**："Page N" | series_planner.cpp |
| 12 | P3 | compose 输出新增 provenance.bindings（additive 漂移） | disposition：有意交付（WP-E），文档已记 | — |
| 13 | P3 | 未来 descriptor 双重告警 | disposition：两条信息语义不同（"newer kept verbatim" + "must be 1..2"），保留 | — |
| 14 | P3 | 测试图层按 title 删除无效泄漏 | **已修复**：按 id 删除（basemap）；coverage 层随进程结束（记录） | 测试 |
| 15 | P3 | 5 项核实非问题（runaway next() 良性、取消探针先于首渲染、命名净化、hash oracle 独立、upgradeMapSpec 幂等） | 记录 | — |

## Reviewer B 补充（自查）

- data_platform_tools / pipeline_run_coordinator 两处 build-unblock 与 #1009 逐字同义（dedupe 记录于 PR_BODY 顶部）。
- resolveCompositionPass/composeProvenance 提升与原两份内联实现逐字节等价（reviewer 独立验证）。
- 秘密扫描：diff 无凭据/token；冲突标记扫描：无；`git diff --check`：见 EVIDENCE。

## 最终双验证（Oracle 6）

- [cp11] RUN1/RUN2：19 cases | 15 passed | 4 skipped（渲染门）| 0 failed — 两次一致
- test_mapspec ~[visual] RUN1/RUN2：213 cases | 211 passed | 2 failed（宿主 rename）— 两次一致
- 宿主 rename 预存在对照：stash 全部改动 → 纯 a5b11b7f 重建 → 同用例同错（"cannot move the export into place"）→ pop 恢复
