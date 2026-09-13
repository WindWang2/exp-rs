# DECISIONS

（架构决策全文见 ARCHITECTURE.md「决策记录」；本表只追加执行期新决策。）

| # | 决策 | 理由 | 日期 |
| --- | --- | --- | --- |
| D-1 | 分支/worktree/目录名 = professional-workbench-visual-cartography-10 | goal-template 命名规则 | 2026-09-14 |
| D-2 | cartography 算子族命名 `cartography:`（非 C-1 建议的 `rs:mapspec_*`） | 与 agent 工具名/模板 catalog/文档词汇一致，避免第二词汇 | 2026-09-14 |
| D-3 | 修复 test_shortcut_conflicts 编译（GCC16）与 test_provenance_section i18n 期望漂移 | 两者为本 track 必跑回归且 master 当前红；最小修复并如实记录 | 2026-09-14 |
| D-4 | cartography 算子注册不写入 rs_operators_init.cpp（原计划放弃），改为 initCartographyOperators() 由 app/cli main 显式调用 | sicnu_operators 无法反向链接 sicnu_agent（循环依赖）；显式 init 零共享文件冲突 | 2026-09-14 |
