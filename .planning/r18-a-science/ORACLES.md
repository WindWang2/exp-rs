# R18-A ORACLES — 质量契约清单（完成指标；token 消耗不是指标）

## Oracle-A 重复性
每个声称修复的缺陷：最小失败复现（RED，在未修复代码上跑出失败）+ 修复后两次 GREEN。
无法复现 → 标 FALSE POSITIVE 并停止该子项（不为凑任务写 patch）。
#1514 教训内置：先证伪，再动手。

## Oracle-B 本地验证
编译 / CTest（本轨 target）/ CLI smoke / Qt offscreen / 合成数据路径的结果与退出码全部写入
TEST_RESULTS.md。环境受限测试明确 `NOT_EXECUTED` + 原因 + 补偿验证。不因线上 CI 排队拖延。

## Oracle-C 性能
基线先行：机器、命令、构建配置、输入规模、重复次数、峰值 RSS、时延/吞吐、产物 hash。
改动后同输入同配置 ≥3 次对照（热/冷缓存分开）。不声称未测量的加速比。正确性优先，
不允许改输出语义换速度；性能改动必须带输出 parity oracle（bit-exact 或明示容差）。

## Oracle-D 回归契约
用户路径不退化；数据不完整/无权限/取消/超时/崩溃/并发 → 可辨识状态（Fail/Indeterminate/
Cancelled，绝不伪 Success）；线程安全、错误类型、schema、资源释放有覆盖证据。

## Oracle-E 所有权
`git diff --name-only origin/master...HEAD` 每个文件匹配 OWNERSHIP.md 写区或有书面跨轨许可。
基线变化重查 overlap；碰 #1513 写集即停。

## Oracle-F Review
≥2 轮 Review-Fix-Review。R1 独立 reviewer（未参与实现）按 P0-P3+文件位置给可复现意见，
要求提出至少一种竞态/异常/反例并尝试证伪修复；修 P0/P1/可复现 P2 后 R2 由另一未写 patch
context 再审。R2 仍有 P0/P1 → 继续迭代。未处理 P2/P3 记录原因与影响。

## Oracle-G 交付
原子 commits、无 secrets/大数据/构建产物、`git diff --check` = 0、目标测试完整、
review_pack.py 证据包、分支推送成功、`gh pr create --base master` 成功且拿到真实 URL。
无权限时保留 commit & PR body 并说明真实拒绝原因，不伪造链接。不自行合并。

## 自检矩阵（任务书 §7）
- CPU scalar/SIMD、整幅/分块、重复运行：定义容差内一致；类别标签 bit-exact 或明示差异。
- 关键算法覆盖：空数组/单像元/NoData/Inf/NaN/无效 metadata/仿射旋转/取消 的适用组合，
  反例用可重放合成数据。
- ≥3 组尺寸（小/中/近内存边界）× 基线与改进对照，记录峰值 RSS；变慢须解释或撤回。
- 不新增算法 family、不换框架、不动 R18-B/C/D/E 范围。

## 迭代纪律
阻滞 3 轮 → 重列 3 种独立原因假设；8 轮 → 重核前提、缩小问题。禁止忙等、无限重编译、
伪造观察。上下文将满 → 全部状态/证据/下一指令写入 LOOP_LEDGER.md 续接。
