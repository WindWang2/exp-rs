# r23-A BASELINE — 遥感科学算法、科学算子与数值性能闭环

- 执行日期: 2026-10-26/27 (UTC)
- BASE SHA: `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea` = origin/master(=任务起草快照,fetch 后复核一致)
- Worktree: `../exp-rs-r23-A-20261026T164112Z`,分支 `agent/r23-a-science-algorithms-20261026T164112Z`
- Host: linux x86_64 (CachyOS LTS 6.18),gcc/clang 工具链,16 核/62GB 共享主机 → 构建限 -j2(上限 -j4),全局 flock
- GitHub 事实(2026-10-26T16:41Z fetch + gh):
  - default branch = master;open PR = 0;open Issues = 144
  - 最近 merged: #1715(冲突标记修复)、#1714(r22-agent)、#1713(r22-workbench)、#1712(r22-science:#1707/#1684/#1685/#1686)、#1711(r22-teaching)、#1710(r22-data)、#1709(r22-inference)、#1708(r22-infra)、#1677(r21-D)、#1676(r21-A 科学算法 17 项)、#1675(r21-C)、#1674(r21-E)、#1673(r21-B)、#1671(r20-01 科学算法 7 项)
  - 前序科学轨已合并大量修复(R20-01 #1671、R21-A #1676、R22-science #1712)——本轮必须排重,禁止重复修复
- 本地 worktree 列表:38 个(多为 hardening/*/agent/* 历史轨;并行 r23 三轨各自独立 worktree)
- tests/CMakeLists.txt 为共享文件:只允许 append-only 追加,push 前逐字节对照 origin/master 注册块

## 主干健康度
- 干净 checkout ad98a7a704(无冲突标记;#1715 已把 #1714 带入的 tests/CMakeLists.txt 冲突标记清除,抽查确认)
- 构建策略:独立 build-r23-A,cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON,-j2 + 全局 flock(`$HOME/.cache/exp-rs-build.lock`)

## 工具版本(关键)
- git 2.x,gh CLI(已登录),cmake(见配置输出),gcc(CachyOS 默认;配置后记录)
