# R22-02 (r22-data) CROSS_TRACK_FINDINGS — 跨轨发现

## 对 03 轨（operators/runtime）

无新增接口需求；本轨未触碰 `src/operators/runtime/**`。

## 对 05 轨（桌面 UI）

- 无。

## 对 04 轨（agent/tool_catalog）

- #1704（MCP 工作区沙箱把 GDAL 驱动前缀连接串当相对路径放行）与本轨 #1679 同属「字符串解析面 vs 实际语义面」分歧类，但归属 agent 轨 write-set。本轨仅提示：#1679 的修复模式（以连接方的真实解析语义为准做统一解析器）可复用。

## 数据域后续候选（本轨未实施，诚实记录）

- #1587（mirrorAllAssets 用 (void) 丢弃治理库写入失败，src/data）：与 #1702 同型的静默丢弃，位于 `src/data/workspace_catalog.cpp`——注意该文件在 open PR #1673 的 write-set 内，本轨让位，待 #1673 合并后由数据轨下一迭代处理。
- #1548（身份解析冷路径持锁 probeAndStore 自死锁，P1）、#1545（range cache 探针不传播 SSRF 防护标志，P1）：需运行时复现窗口，列为下一迭代首选。
- readMask（#1703-②）扩到 GDAL mask/alpha band：issue 前提（无调用者）不成立（tests/ 有 6 处调用钉死现语义）；若产品需要 alpha 感知掩膜，应作为新 API（如 readMaskWithAlpha）走独立 issue，而非改现语义。
- B3/B5/B6 里程碑（catalog 全链路、10 万级性能、系统接口真值）：NOT_STARTED，见 FINAL_REPORT。

## 环境/基础设施观察（面向 infra 轨）

- 构建缓存复制（同基底 worktree 间 cp -a + 路径改写）可将全量配置验证降到 ~3 分钟，但**不含**未构建过的目标（qgis_core 等）；跨轨可约定共享「常用测试目标集」的缓存来源。
- R21 期报告的 vendor GDAL wrapper2_/PCRE2 伪失败问题本轨未遇到（沿 R21-B 同配方 + link-shim）。
