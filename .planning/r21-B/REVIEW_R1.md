# R21-B REVIEW_R1（独立评审第 1 轮，2026-10-24）

评审人：fresh general-purpose 子代理（与实现者隔离，全程只读，未编译未运行）。
结论：**REQUEST-CHANGES** → 全部 P0/P1/P2 已闭环（本轮），P3 部分闭环/记录。

| ID | Severity | 发现 | 处置 |
|---|---|---|---|
| R1-F1 | **P0** | RED 阶段 `git checkout 911e0b17 -- src/` 的回退被索引副作用提交到 HEAD——分支尖端的生产码静默回到 BASE，而新测试断言修复行为 | ✅ 提交 100b1975b6 显式恢复六项修复；RED_GREEN.md 记录该流程教训 |
| R1-F2 | P1 | A5 早拒是死代码（BASE :295 已拒空 id），原测试空洞通过 | ✅ 测试重写为 trigger 注入 oracle（BEFORE INSERT RAISE(ABORT) 使 run 安装必败、metrics 先落库），pristine 上实证 RED |
| R1-F3 | P2 | B5 role 按索引查 heads[h]，而 manifest 只钉 heads[0]==tensorNames[0] | ✅ 改为按名查 + 未声明 head 默认不阈值化 |
| R1-F4 | P2 | A4 probe 失败 fail-closed 门无测试 | ⏳ 记录（WAL 下无确定性触发；R20-02 同结论）；oracle 覆盖 stamp write-once 契约 |
| R1-F5 | P3 | 并发首开 stamp INSERT 竞争可误报 open 失败 | ✅ INSERT OR IGNORE（artifact_store + workspace_catalog） |
| R1-F6 | P3 | 既有 Missing 资产身份键变化（一次性重复） | 记录为已知行为变化（PR body 声明） |
| R1-F7 | P3 | 并发测试 busy_timeout 极端下 flake | 接受（5s 超时 + 40 轮实测稳定 0.3s） |
| R1-F8 | P3 | project_path 线程亲和性未注记 | 记录（与文件既有模式一致；R2 核实无死锁） |
| R1-F9 | P3 | 未声明 head 默认可阈值化 | ✅ 默认不阈值化（与 F3 同修） |
| R1-F10 | P3 | fs alias 夹在 include 间 | ✅ 移动 |
