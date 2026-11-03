# R27-T3 Oracles（每条可失败命令/确定性输入）

| # | Oracle | 验证 | 状态 |
|---|---|---|---|
| O1 | MCP/CLI/driver 无法 mint 审批：OpsDriver::apply("approve_repair")（含 ttl=9e15 攻击形）→ ok=false, APPROVAL_HUMAN_CHANNEL_ONLY，actions 不再广告 | test_r27_t03_approval_gate + test_agent_ops_core + test_cli_agent_ops | GREEN ×2 |
| O2 | 伪造 token（无 secret 的 16-hex digest、篡改 expiry、跨 coordinator）→ APPROVAL_TAMPERED；token 级 wrong_plan/coordinator/expired 全 typed | 同上 + 独立 python HMAC 交叉验证向量 | GREEN ×2 |
| O3 | TTL: mint 拒绝 >30min；shape 校验拒绝超窗；now<=0 → expired fail-closed | test_r27_t03_approval_gate | GREEN ×2 |
| O4 | replay ring: 16 个未过期 consumed 后第 17 个 arm → APPROVAL_REPLAY_RING_FULL；窗口过后逐出恢复 | test_r27_t03_approval_gate | GREEN ×2 |
| O5 | 人工门 happy path: mintRepairApprovalForOperator → arm → run 消费（nonce 唯一、双 mint 不互撞） | test_agent_ops_core 更新段 | GREEN ×2 |
| O6 | GDAL 前缀矩阵: GTIFF_DIR/NITF_IM/HDF4_SDS/HDF5/引号/Windows/`..`/`~`/kv/url=、/vsicurl+169.254.169.254、localhost、GPSBABEL、SENTINEL2_L1C → Denied；相对良性形仍 LocalPath | test_r27_t03_containment_gdal_prefix | GREEN ×2 |
| O7 | dock 100× startWorker→cancel→restart: maxInFlight==1, 串行违规==0, joinWorker 屏障, startWorker<200ms 不阻塞, 10× 析构无挂死 | test_r27_t03_dock_lifecycle | GREEN ×2 |
| O8 | arm 诚实性: 无 nonce/42/数组/超窗 token 即时 typed 拒绝；合法形接受且状态行入 panel | test_r27_t03_dock_lifecycle | GREEN ×2 |
| O9 | env: BASH_ENV/ENV/NODE_OPTIONS/PYTHONSTARTUP/PYTHONHOME/PERL5OPT/RUBYOPT/GLIBC_TUNABLES + LD_*/DYLD_* 前缀族 → E5007；良性 GDAL_*/OMP_* 直达子进程 | test_r27_t03_external_env_guard | GREEN ×2 |
| O10 | 脱敏: api-key/x-api-key 键、Bearer/sk-/ghp_/AKIA/PEM 值 → [REDACTED]；findings_digest/科学散文不动 | test_r27_t03_secret_redaction | GREEN ×2 |
| O11 | copilot 单调用: /etc/passwd、/vsicurl/169.254、GTIFF_DIR 嵌套、出沙箱 output → 拒；note 文本豁免；写槽 → needsApproval | test_r27_t03_copilot_gate | GREEN ×2 |
| O12 | worker: initGui 失败 → -32001 + py: 孤儿 → -32602；shm tile 二块坏 → -32000 且 tile0 unlink；dtype=99 → 拒；A/B 插件发布互不泄漏 | test_r27_t03_python_worker（已直接跑 4/4 OK） | GREEN ×2 |
| O13 | CLI: 非法 pipeline JSON（[1]/{steps:[1]}/operator:{}) → 退出码 6（invalid_input）而非 terminate(134)/1 | sicnu_geo_rs_cli --pipeline 实测 | GREEN ×2 |
| O14 | tests/CMakeLists 重建: master 零丢失（多重集）、969 目标无缺无重、configure 绿 | RECONSTRUCTION.md 命令 | GREEN |
