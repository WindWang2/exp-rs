# R18-E DECISIONS — 常规设计决定的默认选择（不向用户反复申请）

| # | 决定 | 理由 |
|---|------|------|
| D-1 | goal-loop 技能不存在（`.agents/skills` 与 `~/.zcode/skills` 均无）→ 使用任务书内嵌 Oracle 协议原生执行，不伪造斜杠命令 | 任务书 §0 明示降级路径 |
| D-2 | 构建并发取 `-j2`（min(用户硬上限4, 仓库上限2, RAM允许)），不提高资源脚本上限 | §3 纪律 |
| D-3 | D1（课程完成状态未持久化，P0）root cause 在 `src/app/teaching/lab_cockpit_dock.cpp`（R18-D 写域）→ 本轨在 `src/teaching` 提供完成权威记录契约 + 测试，dock 接线写入 CROSS_TRACK_FINDINGS.md，不越界改 src/app | Oracle-E 所有权 |
| D-4 | D5 rubric 契约不一致在 `rubric_builder.cpp`（本域）对齐 grader 权威规则（weight∈(0,100]、Σ=100、passing_score/severity 词表），不改 `src/agent/output_verifier.cpp`（R18-C 域） | 最小所有权内修复 |
| D-5 | D3 checkpoint 判定绑定：不引入签名/HMAC（新依赖+密钥管理超范围），采用「checkpoint 行必须携带与重算一致的 grader transcript 摘要 + verdict 由 transcript 重导出验证」的确定性校验 | 无新依赖、可复算 |
| D-6 | D7 伪造 pass 摘要：给 `last_validation_summary` 增加与被评 artifact 内容绑定的摘要记录（session 内 `verdict_artifact_digest`），恢复时 artifact 可得则校验 digest，不可得则降级 Indeterminate 而非沿用 pass | fail-closed、无签名依赖 |
| D-7 | V2 重复 check id：在 `runSpec` 入口拒绝重复 id（SPEC_INVALID 类失败，fail-closed），并在 rollUp 侧防御性合并取最差 | 双层防御 |
| D-8 | V4 派生 pack 词表脱节：把 `scientificContractFor`/builtin packs 的 params 改成求值器真实词表（`expect.*`/`required_dimensions`），保持既有 JSON 输出 schema 兼容（新增字段、不删旧字段时评估迁移） | 修语义而非加旁路 |
| D-9 | V3 faultlab 缺基线：`Equals/NotEquals/DeltaGe/DeltaLe` 在 clean 基线缺失时判 not-evaluated（期望失败），与 Changed 同一规则；loader 对无 operand 的 delta 关系改为拒绝 | 与 #1521 修法同族 |
| D-10 | H2 CSV 前导空格绕过：strip 后再判定首字符，防护覆盖 `=+-@` 与 TAB/CR；对既有输出格式保持兼容（不含前导空格的单元格字节不变） | 语义不变的安全加固 |
| D-11 | 测试注册：新增 `tests/test_r18_e_*.cpp` 独立文件 + `tests/CMakeLists.txt` 最小块追加（沿用既有 `sicnu_add_test` 风格）；不改动共享注册块的既有行 | OWNERSHIP 约束 |
| D-12 | CLI `lab --grade --out` 原子化（D9a）：改走 durable sidecar 与其余写路径一致 | 统一写权威 |
| D-13 | 每 RED 均以「改前代码上可复现失败的最小测试」形式提交在同一 commit 内（先测后修，原子 commit 内含 RED→GREEN 证据）；对行为改变的修复，测试断言新契约 | Oracle-A |
| D-14 | 性能声明仅附实测（PERFORMANCE.md 记录命令/规模/次数/RSS），不引用未测比例 | Oracle-C |
