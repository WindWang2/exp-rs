# BASELINE — r33-t04-execution-backend

- 创建：2026-11-18（UTC）
- 基线：`origin/master` = `1dfb04382e0fda43ee99665bed8b98666b3f92d7`
  （#2139 顶部；#2134/#2135/#2136/#2137 已含）
- 研究快照参考：`5ff1540004`（2026-11-17）——仅问题出处，非执行基线
- 开放 PR：0（快照中的 4 个保留区 PR 已全部合并，排他路径运行时重取后清空）
- 远端分支 20 个：master + 已合并 codex/zcode 留存；无 R33-T04/T05 分支
- 同批活跃 worktree：r33-t01(SAR)/t02(模型)/t03(data)/t05(agentops)/t06(desktop)

## 已继承基线（不重复实现）

- #2071：报告/产物身份分离、缓存命中收敛、输出 Ready/kind/provenance 对账、
  manifest 验证、absolute resume 路径
- #2061：Studio pending/success 分离、spec/digest/VM 一致、Matrix 冻结投影、
  教师反馈源 pin、未保存草稿保护
- #2132：双语交付、chart/symbology 保护、分页/取消/双语手册、Windows 原子导出
- #2131：Pi 端点一致性、真实进程双语交付
- #2133：Windows UTF-16 工作区与真实夹具
- #2134：Pi 生命周期、Windows UTF-8 SDK 路径、redaction 语义
- #2135/#2136/#2137：科学 CRS/数值边界、EO slice origins、feather 顺序

## 本轨环境

- 平台：linux x86_64（cachyos），gcc/clang 按 dev-default preset
- 构建：`build-dev`（Debug），双锁 host=`~/.cache/exp-rs-agent/host-build` + build dir
- 并行上限：-j4（用户上限；六轨共享 host 锁）
