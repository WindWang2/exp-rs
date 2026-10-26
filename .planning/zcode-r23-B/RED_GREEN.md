# R23-B RED_GREEN — 逐 slice 红绿证据

## W1（#1576/#1577）provider TU 合并回归 — 完成闭环
**RED（master TU，g++ 16 `-fsyntax-only -std=gnu++20` + tests/fixtures/provider_stubs）**
```
openvino_provider.cpp:50:9:  error: 'm_artifactPath' was not declared in this scope
openvino_provider.cpp:60:36: error: 'selectDevice' 在此作用域中尚未声明
openvino_provider.cpp:128:47: error: binding reference of type 'std::lock_guard<std::mutex>::mutex_type&'
                             {aka 'std::mutex&'} to 'const std::mutex' discards qualifiers
tensorrt_provider.cpp:148:56: error: 'm_artifactPath' was not declared in this scope
```
（第三个 OV 错误为本轮 stub-compile 新发现：health() const 锁非 mutable mutex——合并丢掉 mutable 限定。）

**GREEN ×2（修复后同一命令）**：两 TU 均 0 error（两次独立运行输出 "stub-compile OK"×2，exit 0）。

**门反证（变异测试）**：把 master 版 OV TU 回喂工作树 → gate 输出上述 3 error 且 `stub-compile FAILED`（exit 1）；恢复修复版 → OK。TRT 同理（1 error → OK）。
**commit**：ca1413a4ef。ctest 注册名：`test_r23_b_provider_stub_compile`（构建后实跑记录入 TEST_RESULTS.md）。

## W2（B5 残留）derived output.format × head 契约门
**RED 设计**：拒绝始于 preflight（早于 raster open）——以不存在的输入路径作判别器；master 引擎上 6 个非法组合全部落入 "failed to open input raster" 而非契约拒绝（构建后实跑记录贴此）。
**GREEN 设计**：6 拒绝（embedding/detection/uncertainty role + distance/logit-mask/logit-confidence）+ 3 放行（legacy 无 heads、logit+labels、segmentation+probability+labels → 落入 raster-open 失败证明过门）；×2 轮。
状态：待批量构建（RED 态已就位：引擎文件回退 master，patch 暂存 /tmp/r23b_w2_engine.patch）。

## W3（A5 残留）run+metric 单事务
**RED（崩溃窗口 A/B，fork 子进程）**：子进程按 HEAD 导入器顺序（saveMetricRecord 成功后立即 _exit(9)）死亡 → 父进程重开库读到 metric 行存在、run 行不存在（孤儿）——钉住两调用序列暴露的窗口。
**GREEN**：(a) 注入 run 写失败（BEFORE INSERT RAISE 触发器）→ upsertRunWithMetric 两表皆空；(b) 冲突/改名拒绝零变更；(c) 同内容重存 no-op 成功；(d) 既有 r4 A5 触发器用例与 r21 e2e 在新路径下全绿。
状态：代码就位待构建。

## W4（#1637 sub-4）probeRemote 传输统一
**证据局限（DECISIONS #6）**：离线无法构造公网首跳+私网重定向判别；修复=传输统一（httpFetchStatus per-hop guard），GREEN=既有 probe 契约族（test_io_remote_range probe 用例）×2 全绿。
状态：代码就位待构建。
