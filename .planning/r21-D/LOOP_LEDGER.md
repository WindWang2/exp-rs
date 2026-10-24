# R21-D LOOP_LEDGER（goal-loop 每轮记账）

## Round 1（2026-10-24 07:0x–07:5x CST）— W1b 实现 + 构建修复

- 问题：属性表/facade/vectorLayerTools 三条编辑入口绕过 DataManager Edit Lease（W1b，R19-03→R20-04 移交，live 复核仍在）。
- 修改（单一归因：lease 记账统一）：
  - `src/app/qgis_app_facade.h/.cpp`：新增 `SicnuEditLeaseCoordinator` 接口 + facade 保存/协调器存取；saveEdits(per-layer/batch)/toggleEditing 三方法加 lease bracket（commit→commitEditLease→按 id/displayLayerId 重解析→leaveEditable 时重取 lease）；批量版改按 id 快照迭代防重定位 UAF。
  - `src/app/main_window.h/.cpp`：QgisDesktopWindow 实现 coordinator（acquire 单参 override 转发 2 参 shell 版；commit/rollback 现有签名即 override）；setupUi 注册、析构 guarded 释放（同 #1454/#1559 seam 模式）。
  - `src/app/qgsguivectorlayertools.cpp`：startEditing/saveEdits/stopEditing 三路径同 bracket + 重定位重解析。
  - `src/app/qgsattributetabledialog.cpp`：`editingToggled()` 空指针守卫（协调提交会同步换层销毁 mLayer，destroyed→close 不覆盖同步残余路径）。
- RED 测试（`tests/test_r14_D_vector_edit_honesty.cpp` 追加 4 用例 + W1bAssetLayer 夹具）：facade stop 释放 lease+revision 前进；facade saveEdits(leaveEditable=false) 终结会话；批量 save 重开会话持新 lease；vectorLayerTools start/stop bracket。判据：`hasActiveEditLease` 与 `revision().value()`（DataManager 公共 API）。
- 构建插曲：首轮全量编译在 libqgis_core.so 链接失败——vendored libsqlite3.a 无 -fPIC（非 OOM）。对账兄弟轨 cache：三轨均钉系统 sqlite（/usr/lib/libsqlite3.so）。补 `-DSQLite3_INCLUDE_DIR/-DSQLite3_LIBRARY` 重配置后续编（进行中）。
- 验证：待编译完成后执行 stash-fix→RED→unstash→GREEN×2 流程。
- 结论：继续。

## Round 0（2026-10-24 05:45–06:30 CST）— 基线与盘点

- LIVE 盘点完成：origin/master=911e0b17a3，open PR=0，126 open issues 全量导出+分诊（ISSUE_TRIAGE.md）。
- 关键发现：D 轨 P1/P2 候选中 #1535/#1536/#1537/#1559/#1560/#1561/#1562/#1563/#1564 全部 FIXED-OPEN（源码逐条实证，证据在 ISSUE_TRIAGE）；**W1b 属性表绕过 Edit Lease 仍在**（facade :181-287 无 lease）。
- worktree 建立：exp-rs-r21-D-20261023T214750Z @ 911e0b17a3。
- 构建配置成功（vendored 依赖路径沿用兄弟轨）；全量编译后台进行中（flock -j2）。
- 规划文件齐备：BASELINE/ISSUE_TRIAGE/OWNERSHIP/PR_COLLISION_MATRIX/RISK_REGISTER/TASKS/ORACLES。
- 下一轮：M1 seam 回归 + CP11 分类；W1b RED。

## Round 模板

```
## Round N（时间）— <主题>
- 问题：<单一清晰问题>
- 归因修改：<file:line + 意图>
- 验证：<RED 证据 / 定向编译 / GREEN 轮次与 exit code>
- 结论：继续/换假设/阻塞
```
