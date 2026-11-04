# RISK_REGISTER — R25-E

| 风险 | 等级 | 缓解 |
|---|---|---|
| 共享构建锁竞争（五轨并行）| 高 | 全部构建 flock 串行、-j2、合并目标减少锁循环；低余量降 -j1 |
| tests/CMakeLists.txt 五轨同文件冲突 | 高 | 仅尾部追加 R25-E 标记块；push 前跑 check_merge_registration_integrity |
| src/agent/output_verifier（C 轨）不可改 | 中 | grading_evidence 从 row 取值，无需动 C 轨；pixel-size 深化已弃（会需改 C 轨 caller），记录为跨轨建议 |
| Qt JSON 深度行为版本差异 | 低 | 对抗测试 A1/A2 只断言结果语义（fresh start/typed error），不依赖解析器内部限制值 |
| seam 被传入过期列表 | 低 | 文档化 freshness 契约（与内部扫描同契约：按 path 现盘评分）；digest 不受影响；R1 评审复核 |
| 大目录测试（4000 dirs）慢 | 低 | 一次性 mkdir 4000 ≈ 秒级；ctest 无墙钟断言 |
