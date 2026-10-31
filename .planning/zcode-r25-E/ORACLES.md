# ORACLES — R25-E（逐项可判定）

- O-E1: discovery 取消——构造大目录，在扫描中置 cancel → 返回空/部分且有 typed 痕迹；不取消 → 与 master 行为一致（同 items/skips）。preDiscovered seam：传入扫描列表的 runBatchAssessment 与内部发现逐字段一致（rows/report JSON 相同）；config digest 不因 seam 改变。RED=master 无取消参数（编译期/行为）；GREEN×2。
- O-E2: run_lab.cmd 第 3 步 `--lab ndvi_landsat`（脚本内容契约测试 + data 规则存在性）；grade_all.cmd 等其他 repo 脚本不指向 fixture 评 foundry。
- O-E3: recordAnswer("   ") → typed 拒绝；sessionFromJson 载入含空白作答的文档 → 同样拒绝（双边界）；非空白作答不受影响。RED 在 master。
- O-E4: 已撤销（#1653 实码复核 FIXED_IN_MASTER @51eb7d1a6，WP-E4 取消）。
- O-E5: 引擎解析器与 teaching_admin 校验器在 N 个合法/非法 rubric 样例上判定一致（防 #1652-1 复发）。
- O-E6: buildFeedbackPack 对含 topDeduction/graderDigest/unavailableReason 的行输出 grading_evidence 块；空行不捏造（块内为空值而非假数据）；守卫不误报。
- O-E7: 学生 foundry 场景（生成→流水线→ndvi_landsat 评分→pass + 证据）；教师批量场景（2+学生→批量→JSON/CSV 成对→反馈守卫→异常可见）；对抗矩阵 ≥4 类全部 fail-closed。
