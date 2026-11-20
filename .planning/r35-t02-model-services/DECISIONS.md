# DECISIONS — R35-T02（unattended 常规决定记账）

1. **EXPRS_BASE_BRANCH=master**：首次 gh api TLS 超时一次；同会话早前成功调用已取 default_branch=master，且 origin/master 与快照 SHA 一致，直接显式设 master 继续（符合协议 fallback 条款）。
2. **WP01 修复形态**：readLine 改 QElapsedTimer 单调绝对 deadline，检查移到循环顶（数据分支不再绕过），`waitForReadyRead` 用 min(100, remaining)。不新增 idle-timeout 旋钮：现有 `timeoutMs` 语义就是"一帧交换的绝对时限"，保留 partial-frame 合法语义（deadline 内完成的慢分段帧照常成功）；不重排其余分支。
3. **WP01 RED 防 CTest 挂死**：trickle fixture 有界 8s 后静默——旧代码在 ~8s+startup 处失败，新代码 ~1.2s 失败；断言上界 5s 区分红绿且永不挂死测试进程。
4. **fixture 数量**：4 个微型 stdlib 脚本（trickle/segmented/silent/stderr_chatter），对应 oracle 五种形态；worker-exit 路径由既有 test_provider_python crash 用例覆盖，不重复造。
5. **测试注册**：tests/CMakeLists.txt 仅追加本轨块——deadline/typed 两块紧跟 test_provider_python 块后、ensemble 块紧跟 test_ensemble_parallel 块后（sicnu_add_test RUN_SERIAL 用于 ensemble），不重排他轨块。
6. **会话策略**：WP01 先独立闭环提交；后续 WP 视证据与时间推进，不凑数。
7. **WP05 语义**：missing/null → 默认；0 → 保留文档化 "provider default" 语义（model_catalog.h 注释与 provider 构造一致）；负值/非整数/非数值类型/超 int → 拒绝并带字段路径。跟随 max_concurrent_members 的严格先例（floor/int-range + "must be a whole number"），不发明新词。null 视为"未声明"（比 manifest_version 的 null 宽容略宽，但 provider knobs 是可选调优项，误伤历史 manifest 的代价大于收益）。
8. **WP05 程序化入口**：只加 python provider 工厂负值拒绝（makePythonWorkerRuntime，进程启动前）；HTTP provider 无对应新增（其 max_body_mb=1 等正负值场景由既有 #1056 测试覆盖，负值路径当前构造子句已 clamp 为默认——不扩写非必需面）。catalog 单测走 validateManifestJson（不污染注册表）+ registerManifestJson/find（历史 round-trip）。
9. **修复以 patch 形式暂存**（wp01_wp18_fixes.patch / wp05_catalog_typed.patch / wp05_provider_domain.patch，git apply --check 通过），保证 RED 构建编译的是 pristine 源码 + 新测试。
10. **OpenCV 依赖路径**：裸 `--preset dev-default` 找不到 OpenCV（configure 失败一次）。核对 R33-T02/T04/T06 三个成功构建的 CMakeCache，全部使用主 checkout 共享 `.scratch/ui-build-deps/opencv`（opencv5 cmake 目录）+ 系统 Qt6/GDAL——这是宿主标准环境而非某轨私有路径，采用同样的 `-DOpenCV_DIR` 重新 configure。
