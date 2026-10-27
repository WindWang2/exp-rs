# R24-B CROSS-TRACK FINDINGS（跨轨只报告不越界）

| # | 发现 | 证据 | 影响轨 | 建议 |
|---|---|---|---|---|
| X1 | 开放 Issue 列表 ~90 条几乎全部已被 sweep commits（`987161c25`/`9b42bc93f`/`d9f1b886f`/`831f8d600`/`c8ba51990`/`8e0430262`）与 R22/R23 PR 修复，Issue 状态未同步 | ISSUE_TRIAGE.md | 全轨 | 各轨关闭前先实码验证；或由维护者批量核对 |
| X2 | `execution_telemetry.h:144` 注释仍写 "push_back + erase-front"，实现已是环形游标覆写 | `execution_telemetry.cpp:104-117` | 本轨（微修） | 本轨顺手修（OWNED） |
| X3 | `scripts/check_merge_registration_integrity.py` 在主工作树（ad98a7a70）不存在——主树落后 origin/master；新轨勿以主树为准 | 实测 ls | 全轨 | 一律以 fetch 后 origin/master 为基线 |
