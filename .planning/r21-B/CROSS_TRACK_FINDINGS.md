# R21-B CROSS_TRACK_FINDINGS — 跨轨移交（本轨只读发现，不越界修）

| ID | 发现 | 归属 | 证据 |
|---|---|---|---|
| X1 | **共享环境断裂（2026-10-24）**：系统升级后（pcre2 10.49 / Qt6 6.12.0-2.1），vendored libgdal.so 的 wrapper2_*（原 libarmadillo.so.15 供给，已从 paleo prefix 消失）与 Qt6Core 的 PCRE2_10.47 符号使任何新链接失败。r19-02 的 `link-shim + -Wl,--allow-shlib-undefined` 配方可解（本轨已复制到 build-r21-B/link-shim 并以 CMAKE_EXE_LINKER_FLAGS 接线）。R21-A/C 同样会撞墙（观察：C 轨 49 目标仅 1 个产出）。 | 各轨自行采用；根治需重建 vendor prefix | 本轮 ld 实错 + r19-02 CMakeCache CMAKE_EXE_LINKER_FLAGS |
| X2 | R21-A 全量构建未持 `/home/kevin/project/exp-rs-build.lock`（03:5x 起 4×cc1plus 与持锁的 C 轨并行）——共享宿主压力与 R20-02 X6 同类 | 各轨协调 | ps 实测 2026-10-24 03:58 |
| X3 | 沿用 R20-02 X1/X2/X4（CLI flag 黑洞残余、SicnuPythonApi 其余接口、#1544 关闭建议）— 状态不变 | CLI 轨/维护者 | R20-02 CROSS_TRACK_FINDINGS.md |
