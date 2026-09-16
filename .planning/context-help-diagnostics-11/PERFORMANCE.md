# PERFORMANCE — F20 context-help-diagnostics-11

## 资源模型与硬约束

- build：`cmake --build build-dev -j2`（`CMAKE_BUILD_PARALLEL_LEVEL=2`）；压力高降 `-j1`。
- test：`ctest -j1` / 直接跑二进制，`QT_QPA_PLATFORM=offscreen`。
- 主机：16 核 / 62GB RAM / ccache（冷缓存起步）。

## 构建期间资源采样

（按 60s 间隔或一次性记录，格式：时间点 CPU% RSS-MB load1/5/15）

## 逻辑规模界（非 wall-clock gate）

- help registry 条目数：master 基线 >200 descriptors（组合后）；本 track 新增 ≤30 条（10 命令页 + 7 诊断页 + 参数微调），registry 规模不变量由 test_help_core 既有断言守护。
- census 扫描器：源码正则扫描文件数 ≤3 个（command_defs.cpp、main_window_workbench.cpp 等），运行期开销为一次注册表快照。
- zero-diff gate：5 页 markdown 字节比较，内存比较不落盘。
- corpus 测试：场景数 ≤40，每场景断言 O(registry) 查找。

## 实测记录

（构建/测试实际耗时与峰值 RSS 按测填写）
