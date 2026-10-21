# R18-D ORACLES

- **Oracle-A 重复性**：每个修复先写最小 RED（最小失败复现），修复后同测试 GREEN×2（两次独立运行）。无法复现的候选项标 FALSE POSITIVE 并废弃，不写凑数 patch。
- **Oracle-B 本地验证**：本轨相关 ctest 全 GREEN（在 build-r18-d，QT_QPA_PLATFORM=offscreen）；环境受限项记 `NOT_EXECUTED` + 原因 + 补偿验证。不因线上 CI 拖延。
- **Oracle-C 性能**：涉及热路径的修改做前后对照（同输入×3，热/冷缓存分开），只报实测数字。纯正确性修复不虚造性能声明。
- **Oracle-D 回归契约**：已有用户路径不退化；取消/异常/关闭后无遗留（map tool、lease、dangling signal）；fail-closed 状态可辨识。
- **Oracle-E 所有权**：`git diff --name-only origin/master...HEAD` 全部落在 OWNERSHIP.md 写入区；每次基线变化重查 overlap。
- **Oracle-F Review**：R1 独立 reviewer（未参与修改的 context）对实际 diff 按 P0-P3 出意见；修完 P0/P1/可复现 P2 后 R2 由另一未写 patch 的 context 再审；R2 有 P0/P1 则继续迭代。reviewer 必须尝试证伪（至少提出一种竞态/异常/反例假设）。
- **Oracle-G 交付**：原子 commits、`git diff --check` 干净、无 secrets/大数据、review_pack.py 证据包完整、push 成功、`gh pr create` 拿到真实 URL；不等待线上 CI；不自行合并。

## 量化自检矩阵（任务书 §7）

- 命令 enabled/disabled 与实际 QGIS layer/canvas/edit session 状态一致；取消/异常/关闭后无 map tool/lease/dangling signal 残留。
- 相同 MapSpec 的结构输出、版面尺寸、图例与导出内容可稳定重现；不可比环境明确限定基线。
- offscreen 自动测试能跑尽跑；headful 手动路径 NOT_EXECUTED 时写明。
- 只收敛已有能力，不引入第二套地图引擎。
