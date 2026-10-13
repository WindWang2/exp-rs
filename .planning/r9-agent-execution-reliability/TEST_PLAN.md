# R9 Test Plan

## 构建与运行约定
- `CMAKE_BUILD_PARALLEL_LEVEL=2`，preset `dev-default`（MSVC+Ninja，本 worktree 独立 build-dev）
- `CTEST_PARALLEL_LEVEL=1`，`QT_QPA_PLATFORM=offscreen`
- 关键 gate 至少跑两次（green×2）才记 PASS；记录命令、退出码、套件名、失败日志路径

## 每 WP 的复现与回归

### WP-A ipc_channel 回放死锁
- Red：测试先 `setEventSink(lambda{ channel.protocolFailure(...) 或 close(); })`，回放队列非空时挂起检测（超时线程 + 标志位，不真挂死 CI）
- Green：修复后同一 sink 回放完成且 channel 状态一致；已有事件顺序（hello-first）测试不回归
- 套件：现有 sdk/exprs 相关测试 + 新增定向 case

### WP-B external_process Windows 解析一致性
- Red：`/tools/worker.exe` 形态：validation 探针路径 ≠ resolveWindowsProgram 实际路径（断言两者字符串/存在性判定不一致）
- Green：probe 与 spawn 对同一输入得到同一目标；PATHEXT 遍历（.COM/.EXE/.BAT/.CMD…按序）；无扩展名/已带扩展/UNC/盘符绝对/相对各形态表驱动
- 平台注意：Windows 专属逻辑；非 Windows 编译不回归（#ifdef 保持）

### WP-C plugin_registry 快照有界
- Red：循环 N 次 record() 同一 plugin，快照容器尺寸随 N 增长（> 上界）
- Green：同 plugin 重复查询不新增快照（或总量有界）；refresh() 后旧快照不悬垂（已有 #943 契约保持）
- 套件：plugin registry 现有测试 + 新增内存上界 case

### WP-D model_runtime 同 key 并发
- Red：CPU/mock provider 下两个线程同 key 同参数并发 acquire，第二个得到假拒绝（device unavailable）
- Green：第二个等待并复用第一个的 session（coalescing），或至少不因 admission 竞争假拒绝；失败广播不悬挂等待者；取消传播
- 套件：model runtime 现有测试 + 并发 case（无 GPU，用现有 mock/factory hook，不声称实卡）

### WP-E task_center m_catalog
- Red：静态审efits + 定向测试：构造后不调 shutdown() 直接销毁 DataManager 的宿主场景（若现有测试架构允许模拟）
- Green：guarded handle（QPointer/weak）或销毁路径反注册；正常路径行为不变
- 注意：涉及与 src/data（A 方向）边界 —— 只改 task_center 侧持有方式，不动 DataManager

### WP-F worker pool 忙等
- Red：acquireWorker 在无 worker 时的重入双占（构造两 acquire 竞争，断言不出现双占计数）
- Green：事件驱动等待（QSignalMapper/wait condition）替代 processEvents 轮询；adapter 嵌套循环去除
- 风险：Qt 事件循环语义敏感，改动最小化；失败则回退为仅消除嵌套轮询并记录

## 回归范围（收尾全量）
- ctest -R 定向：sdk/exprs、plugin、model runtime、task center、python worker 相关套件
- `git diff --check`；无构建产物/大文件入库
