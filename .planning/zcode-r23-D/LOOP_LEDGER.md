# LOOP_LEDGER — goal-loop 迭代账本

| 轮 | 改动 | 验证结果 | 通过/未通过 | 下一步 |
|---|---|---|---|---|
| 1 | Phase 0 盘点：fetch/PR/Issue/worktree 复核（master=ad98a7a70 未漂移；OpenPR=0；Issue=144）；建独立 worktree exp-rs-D-20261026T165151Z；vcpkg_installed 3GB 隔离复制；cmake configure build-D 启动 | origin/master 与协议快照一致；无兄弟 worktree 竞争 | 通过（盘点） | 等 configure 完成后 ctest -N + 基线测试 |
| 2 | Explore 全模块普查 + CT-6 现场 LIVE 确认 + #1648/D-F1/D-F2 实锤（jsoncpp 1.9.6 源码证实 throwRuntimeError 语义） | 缺陷候选 3 项实锤 + CT-6 仍 LIVE（规则 9/13 未动、生成器仍 256/UTM、run_lab.cmd 钉 repo 规则目录） | 通过（分诊） | 写 WP 计划（TASKS.md 已立 6 个 WP）→ 等构建 → RED |
| 3 | 分诊纠偏：#1648/#1653 现场读码证实 FIXED_OPEN（a1ffd5c66 / 51eb7d1a6）→ 撤销 WP-D3/D6；WP 代码全部落盘（规则+测试+修复+注册）；主仓预构建二进制全流程预验证 CT-6：foundry 工件 vs overlay 规则 = pass 100/0 扣分/exit 0（mean 0.452126129651、sigma 0.508464660071、nodata 131/65536 逐位吻合 overlay 钉值）；vs repo 规则 = 45/100 fail（CT-6 本机复现） | 校准零风险；WP-D1 oracle 本机证实可达 | 通过（预验证） | 等基线构建 → commit A(仅测试) → RED → commit B/C → GREEN×2 |
| 4 | 基线构建完成（-j2→-j4 协议条款内，3300+ 单元 0 编译错误；测试工具链修 Qt DLL PATH/TEST_PREFIX）；RED：master 源码 5/5 全红（含 `Exceeded stackLimit in readValue().` 异常逃逸实证）；GREEN×2：5/5 双绿（exit 0）；三提交（069fc43c4/2be7bc258/7365c991a） | RED→GREEN×2 闭环，退出码与日志存档 TEST_RESULTS.md | 通过 | WP-D5 矩阵 |
| 5 | WP-D5 回归矩阵 33 套逐 exe：25 绿 + 7 KNOWN_BASELINE_FAIL（其中 4 套 BASE 源码对照复跑，失败签名逐位一致：lab_runtime/faultlab/lab_data_pack/lab_chains；labspec/recipe_equivalence/r16_a_grader 环境签名明确）+ 1 NOT_EXECUTABLE（r16_a_faultlab=unistd.h） | 存量 Windows 基线限制全部归因，非本 PR 引入 | 通过 | R1 评审 |
| 6 | R1 独立评审（新上下文代理）：PROCEED-WITH-FIXES（0P0/1P1/2P2/4P3）；4 组对抗突变全转红全还原；F1=TEST_PREFIX 未提交（已修）、F2=措辞超交付面（已校准）、F3=收尾未提交（已收）、F4/F5 小修（跨跑 oracle + RAII）；修复提交 ed30c9790 后 5/5 双绿 | R1 闭环 | 通过 | R2 评审 |
| 7 | R2 独立评审（第二新上下文代理）：**APPROVE（0P0/0P1）**；3 组反例独立复现（去 catch / sigma 换轴 / isfinite 禁用）全红全还原复绿；R1 修复提交零回归（TEST_PREFIX 落库实选 5 例、cross-run 扁平键亲证、RAII 无 double-remove、wrapper PATH 亲启验证）；26/26 白名单、纯 append、规则契约逐参数 IDENTICAL；标题全 ASCII | 双审闭环（O5） | 通过 | 交付门 + PR |

## 卡壳升级记录
（连续 3 轮未通过时强制换假说；8 轮重审需求。当前无。）
