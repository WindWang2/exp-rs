## hardening(r19-03): Qt QGIS workbench and cartography UX fidelity

基线 `origin/master = e12a9afbc8883a474dc9ffcaf3345ae55863dc71`（LIVE recon 实测，与任务书快照一致）。全部为**已有模块深化**：零新产品方向、零 CMake 改动、零 vendored QGIS/弱桩修改、零教学域触碰。

### LIVE recon 摘要（.planning/r19-03/BASELINE.md）

开放 PR 仅 #1663（R18-A）/ #1664（R18-E），与本轨写集零交集（#1664 src/app 0 文件已核）。**#1662 之后实测排除**（当前代码实读，非 Issue 状态）：#1558/#1571/#1561/#1562/#1563/#1564/#1559/#1535/#1536/#1537/#1621(大部)/#1622/#1657(主)/#1632(1,2,5,6)/#1639(1) 及 r18-D 遗留 C4 produce legend 守卫——全部已在 master。真实残留收敛为下列三项，全部 RED 实证后修复。

### 修复清单（RED → 最小修复 → GREEN×2）

1. **Edit 菜单会话命令入 gate（r18-D R2 P3-5 遗留，C01）**：Cut Features / Paste Features / Delete Selected 原为裸 addAction（无 objectName、无门控，无会话时 enabled-but-will-fail）。现注册 `m_editingSessionMenuActions` 并入 updateEditingUI 同一契约（禁用+reason+tooltip），`editingAction()` 可查；ribbon 面同样改用既有 `editingAction()+addActionButton` 投影（digitizing 15+ 按钮同模式）。**Copy 保持无门控**（只读层复制合法）。RED：`editingAction("mActionCutFeatures")` 查无（nullptr×2）。
2. **terminology summary 共享上界（#1639 条目 2，C11）**：`d.summary = truncateSummary(term.definitionZh)`——≤120 码元 + 代理对不切半，与其他全部 summary 产出方一致。RED：302 > 120。负向：119 CJK + 星面字符恰跨 120 切点，断言 `== left(119)`。
3. **SpectralLibraryDialog 构造期诚实状态 + 恒真断言替换（#1621 残余，O-C5）**：summary 标签补 objectName；构造尾调用 `updateSpectrumSummary()` 使未显示对话框 match/save 起始禁用（原默认 enabled 直到首个 showEvent）。m2_batch_a 两处 `CHECK(true)` 换成真断言（DataManager 切换的输入可见性互换 / setSpectrum 后 "4 bands"+按钮使能+空态前置）。**变异探针**：移除构造尾调用 → 1 用例红（131/132）→ 恢复绿。

### 验证证据（.planning/r19-03/TEST_RESULTS.md）

- **RED×3 全部真实失败**（file:line + 断言展开）
- **GREEN×2**：`[r19d]` 7/7×2、`[menu_session_gate]` 30/30×2、m2_batch_a 132/132×2
- **全量回归**：r14D 370/370、help_core 2173/2173、mapspec 2830/2830、e2e_open_issues 443/443、terrain 1450/1450、visual_analytics 100056/100056、spectral_library 48/48、m2_dialog_stress 720/720、classification_studio 35/35、spectral_profile 154/154、pipeline_canvas 139/139、georef_dw 27/27、help_system 78/78、global_help_tips 25/25、gui_job_adapter 29/29
- **Review**：R1（独立子代理逆向，亲跑测试）条件通过 → P1-1/P2-1/P2-2/P3-1 全处置（6104933e6）；R2（再新子代理）**APPROVE 零 P0/P1/P2**、4×P3 全处置（d1a89fe9f），五组复测由 reviewer 独立重跑证实
- `git diff --check` 干净；分支 0 behind origin/master（fetch 实测，免 rebase）

### KNOWN_BASELINE_FAIL / NOT_EXECUTED（如实声明）

- `test_cartography_production_11` 本机 3/28 渲染路径失败（atlas/e2e/corpus）：**本 diff 零编图文件**（git diff --stat 核对）；#1655 已在 master 把渲染门反转为默认运行，本机离屏渲染即触发。`SICNU_CP11_RENDER=1` 探针 EXIT=42（失败非挂起）。留编图 owner 在带显示环境复核。
- Windows/macOS 路径（本机仅 Linux offscreen）；sanitizer 构建（三轮共享构建资源排满；本轮改动无新线程/锁）

### 所有权与交叉

- 写集 20 文件全在 OWNERSHIP（src/app 菜单/外壳/对话框、src/help/terminology_provider、3 个测试文件、.gitignore 自块、.planning/r19-03）——与 #1663/#1664 零交集
- 跨轨记录（CROSS_TRACK_FINDINGS.md）：#1560 取消通道设计、W1b lease 路由、#1638 弱桩三题、DPI 下限不一致、#1655 车道归属
- 资源纪律：全程 `-j2` + git-common-dir 全局锁 + resource_guard（与 r19-01/02 轨实测互斥排队 ~2.5h 如实记录）

### 提交

- `04e81fac0` fix(app/help): 三项修复 + 测试
- `6104933e6` review(r19-03): R1 处置（ribbon 投影/OWNERSHIP/证据/切点强化）
- `d1a89fe9f` review(r19-03): R2 P3 处置

> 不等待线上 CI；PR 由维护者按证据审阅合并。
