# R27-T3 Goal

基于 base 10652fd003，修复并深化 Pi/MCP 空间 Agent 审批权限、工具安全与 SDK/插件闭环：
- P0 #1733 人工审批不可被模型/MCP 自签（HMAC + TTL 上界 + nonce + 一次性 + 人工通道唯一 mint）
- P1 #1741 GDAL 未知前缀沙箱逃逸/SSRF；#1742 dock worker 生命周期串行化
- P2 #1760-1763 审批诚实性/进程 env/脱敏/Copilot 单调用门
- P2 #1796-1799 Python worker SHM 回收、initGui 孤儿、CLI typed terminate、插件发布范围
- P3 #1802/#1806 可行子集
- 前置：修复 master tests/CMakeLists.txt 合并撕裂（不修则全仓无法 configure；证据与重建证明见 RECONSTRUCTION.md）
完成两轮独立 review、本地测试双跑、提交唯一 PR（不合并）。
