# REVIEW_R2 — R28-T02（第二轮独立复审，2026-11-06）

评审者：第二个独立 subagent（与实现者、R1 均不同上下文）。范围：R1 修复 commit 8a5d8f3c14 的逐点验证 + 全 diff 新视角隐患扫描 + 跨轨所有权 + 复跑全部套件。

## 结论：**VERDICT: CLEAN（建议 SHIP）**

## R1 修复逐点验证（全部 CORRECT）

| 项 | 验证方式 | 结果 |
|---|---|---|
| 1a cube/mirror CacheGuard | 逐行括号/生命周期追踪（guard 跨 planFabric+executeWindow+栅格写；所有 return 在 try 内；install/uninstall 幂等）；catch 信封与外层逐字节一致 | ✅ |
| 1b qPreflightBatchToJson *ok | 枚举值核实（exit_codes.h:15）+ 实跑（拒绝源 exit 6 / 健康 tif exit 0 import-ready / 未知 flag typed 拒绝） | ✅ |
| 1c Registered 清 message | 清除仅在非空 assetId 分支（成功可达性证明）；失败路径不受影响 | ✅ |
| 1d trace 断言 | detail 格式构造性匹配（同一 shutdown 返回值）；套件 21/1 实跑 | ✅ |
| 1e .moc 守卫 | 生成目录中 moc 存在且新于源；5 个测试二进制均新于其源 | ✅ |

**R2 独立复跑（HEAD）**：import_session_runtime 53/7、closed_loop 43/1、lifecycle_batch 21/1、interop 27/4、infer 452/1、agent_ops 101/28、io_fabric_object_store 97/8 —— 与 commit 信息/验收表/账本声明**逐计数一致**（无 0-test/幻影套件/错误二进制声明）。

## 新发现与处置

| Sev | 发现 | 处置 |
|---|---|---|
| P3-1 | mirror 分支 `--allow-private-network` 在 unknown-flag gate 之后才消费（违反 cliTakeAllowPrivateNetwork 的进程内调用契约；真实二进制不受影响——main_cli 进程级剥离先行验证） | ✅ 已修：消费移到 gate 之前（与 cache/cube/identity 兄弟面一致），commit 见 R2 闭环 |
| P4-2 | ACCEPTANCE.md G7 指向不存在的 TEST_RESULTS 文件 | ✅ 已修：改为指向本文件计数节+logs/（并注明 R2 独立复跑） |
| P4-3 | 环境项（非本轨）：main 仓库根有 10-01 的 0 字节 libsicnu_workflow.so 残留+CLI RUNPATH 尾空组件=CWD 搜索，从 main 目录执行 CLI 会 127 | 记录给仓库所有者；本轨无动作 |

## 核验为干净的隐患面（检查过、无发现）

- 测试注册：5 目标各恰好一次（CMakeLists 18847-18927 纯追加）
- identity_token journal 对称往返；r27 套件仅 .value()/计数断言——可选新键不可破坏
- fabricCachedPath throw 仅 RemoteHttp 可达（/vsis3/=VsiRemote 保持原语义）；6 个仓内调用方全部 try/catch typed
- JobEngine trace 仅显式 shutdown() 内发布；R1-5 守卫阻断 abandoned worker 迟到 unwind 的发布；仅 shutdownForTests 复位
- 导入循环：registerSource（目录先）→ 逐项 journal；窗口间自愈（findByPath dedup-skip）；无双重注册/双重发布
- 跨轨所有权：30 文件全部在声明面内（src/app/agent/workflow/experiment/teaching 零触碰）
