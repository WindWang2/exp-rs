# REVIEW_R2 — R27-T2 第二轮独立评审（地理空间契约 / 安全 I/O / 兼容性 / 证据质量）

评审者：独立 general-purpose subagent（fresh eyes，未参与实现与 R1）。
范围：`git diff 10652fd003..HEAD` 全部变更 + 依赖闭包（ResourceUri/offline_gate/http_fetch/range_cache/process_network_policy/GDAL 头与 vendored NEWS/stac_browser_dialog/gdal_raster_source_provider/chunked_run）。

## FINDINGS（13 组）与处置

| # | 严重级 | 摘要 | 处置 |
|---|---|---|---|
| F1 | P1 | stac_browser_dialog 仍 raw /vsicurl/；safe_open 声称"every caller uses"言过 | ✅ 跨轨条款（dialog 专归 T4）不可改本轨；header 措辞改为"for adopters — RemoteDatasetPool routes today; dialogs/CLI adopt as they migrate"。GUI main 不 engage 策略属 T4/T5 决策面，PR body 如实披露 |
| F2 | P1 | OSRSetFromUserInputEx 是 GDAL 3.9+，3.8 lane 编译断 | ✅ `GDAL_VERSION_NUM >= 3.9` 守卫；<3.9 预扫描拒绝网络/文件拼写（同契约） |
| F3 | P1 | ~15 个其余 SetFromUserInput 站点未收敛，commit 声称过宽 | ✅ helper 提升为公开 `geo::osrSetFromUserInputNoNetworkNoFile`；修白名单内 2 站点（detection_tile_engine、tile_inference_engine——untrusted 模型元数据）；其余站点跨轨（agent/core/analysis/operators-rs/processing 超本轨白名单），PR body 与 issue 后续如实标注 |
| F4 | P1 | RIO-11 确定性击穿 test_r12b_e2e_durability Area 6（gov_audit 触发器注入失效） | ✅ 测试改为在 audit_log（唯一权威）上注入 abort 触发器；66/66 过 |
| F5 | P2 | "DNS-free"/"guarded transport" 契约两处失实：isPrivateHost 对主机名跑 getaddrinfo；Ok 路径在策略未 engage 时 transport 无条件跟随重定向 | ✅ 新增 GuardUnavailable typed 拒绝（process 或 cache config 的 blockPrivateNetworks 必须至少一个 engage）；header 诚实重写（DNS 说明 + 姿态门）；测试补 unguarded-posture 拒绝用例 |
| F6 | P2 | 全信号剥离仍可降级 v1（固有）；缺 v1 兼容 oracle；缩进 | ✅ 残差在代码注释如实记录；新增"genuine v1 manifest 仍 size-only 复放"oracle；缩进随注释修复 |
| F7 | P2 | journal 一次瞬态失败永久报 non-durable；journal 按序号匹配丢状态 | ✅ 成功发布愈合 m_journalError；journal 匹配改按 path（QHash） |
| F8 | P2 | cleanupAfterPublish 持锁竞争把已发布成功 run 打成失败（abandon+discard） | ✅ chunked_run：cleanup 移出 try + 专捕 ChunkRunLockHeld 降级 best-effort（#1766"不破坏前次产物"的直接要求，D-1 类授权最小改动）；NFS 语义（flock 本地性）在锁 header 文档化 |
| F9 | P2 | RIO-14 零测试、上限降 10×无 oracle | ✅ 判定提取为公开纯函数 `catalogContinuationRepeats` + 5 断言 oracle（同 offset+同 href 判重、偏移推进/换 href 不误杀、纯本地 offset 语义） |
| F10 | P2 | RIO-12 oracle 近乎空转（异常类型无法区分策略拒绝与解析失败） | ✅ 断言拒绝消息含被阻断向量（GDAL 3.9+ "due to ALLOW_NETWORK_ACCESS/FILE_ACCESS=NO" 已用探针验证）；GeoError 消息并入 gdal_error 提升可操作性 |
| F11 | P3 | gov schema 版本 ping-pong、retention 无测试 | ✅ retention oracle（seed 100002 行 → open → ==100000 + 最新行存活）；schema 版本保持 "1"+open 时幂等 DROP（旧二进制重建表会被新二进制下次 open 清理，无害化）——决策记录于此 |
| F12 | P3 | WHERE 守卫注释夸大（UNION 仍可注入字面量拼接） | ✅ 注释如实：第二语句/注释截断被阻断；UNION 残留 + 字面量误伤 fail-closed 说明；在树调用方均受信 |
| F13 | P3 | 杂项（锁 m_path 残留、chunk_graph 越界无 oracle、engine 测试 timeout 未复位、kSchemaVersion 死字段、缩进） | ✅ 全部修复（m_path 失败即清、越界 id oracle、timeout 复位 5000、schema 字段投入使用） |

## hunted-clean 摘要（评审者攻击未遂清单）

URL 解析器混淆全套（userinfo@、?/# 截断、反斜杠、大小写、嵌套 /vsicurl//vsirangecache/、百分编码、IPv4 简写/十六/八进制/十进制整数、IPv6 mapped/compatible/NAT64）；拒绝顺序无泄漏；受控 transport 的逐跳策略+FOLLOWLOCATION=0+RESOLVE pinning（engage 时）真实有效；mirror 三方一致（resolveMirrorHit/verifyChunkEntry/prune）；chunkKey/manifestVersion 对旧读端纯增量；保留 off-by-one 干净；锁文件命名与 wipe 不相交；崩溃恢复 oracle 真实（子进程 _Exit）；JobEngine 唤醒/静态寿命/终态流一致；租约四释放点真实；addJoin 强保证；tests/CMakeLists 重建抽查（R26 注册恰好一次）。

## 修复验证

39/39 本轨（新增 4 oracle）×2；r12b 66/66 ×2；p3 7/7 ×2；parity 11、r24_b_eo_preflight 2（tile_inference 改动消费方）、job_engine 34（446 断言）全绿。
