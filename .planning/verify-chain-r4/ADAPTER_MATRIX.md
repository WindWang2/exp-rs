# ADAPTER_MATRIX — production adapters × 检查族覆盖矩阵（Track 16 / WP-A）

行 = kCheckKinds（verify_types.cpp:21，10 族，机械枚举）；列 = 5 个 production adapters（verify_adapters/）。
格值：**✅直** = 本轨/既有测试经 production adapter 实例直驱该格；**🔗e2e** = 装配进 VerificationContext 由 verify_engine 求值；**N/A-声明** = 结构上不可能；**不处置+理由** = 非本域/无生产事实源。

| 检查族 \ adapter | fs_artifact_probe (IArtifactProbe) | gdal_grid_probe (IGridProbe) | checkpoint_state_view (IStateView) | provenance_sidecar_view (IProvenanceView) | bounded_io (内部闸门) |
|---|---|---|---|---|---|
| state.invariant | N/A-声明（不经 artifact 探针） | N/A-声明 | 🔗e2e ✅直（"Real adapters drive…" + 本轨五-kind 装配） | N/A-声明 | ✅直（文档装载路径） |
| artifact.exists | 🔗e2e ✅直 | N/A-声明 | N/A-声明 | N/A-声明 | ✅直（pathExists） |
| artifact.type | ✅直（shallow kind 嗅探既有案） | N/A-声明 | N/A-声明 | N/A-声明 | — |
| artifact.grid | 🔗e2e（grid e2e 用 fs 开档） | 🔗e2e ✅直（"Grid facts drive the engine…"真 GTiff） | N/A-声明 | N/A-声明 | — |
| artifact.schema | 🔗e2e ✅直（readJson→schema 门） | N/A-声明 | N/A-声明 | — | ✅直（parseJsonBounded 有界解析） |
| metric.range | N/A-声明 | N/A-声明 | N/A-声明 | N/A-声明 | N/A-声明 |
| relational.consistency | N/A-声明 | N/A-声明 | N/A-声明 | N/A-声明 | N/A-声明 |
| provenance.complete | — | N/A-声明 | N/A-声明 | 🔗e2e ✅直（sidecar + run lineage 两路） | ✅直（有界读取/信封门） |
| reproducibility.digest | 🔗e2e ✅直（本轨装配格：真文件 sha256 钉值） | 🔗e2e（grid e2e 篡改字节→DigestMismatch） | — | — | ✅直（预算超限→空 digest→引擎 Indeterminate） |
| cross.output.consistency | ✅直能力位（digest/sizes 对比经 fs 探针；引擎侧 fakes 全判据覆盖） | 🔗e2e（needGrids 分支经 grid e2e） | N/A-声明 | N/A-声明 | — |

## 不处置声明（每格理由，零未声明缺口）

1. **metric.range / relational.consistency × 全部 adapters = N/A-声明 + 不处置**：
   仓库内**不存在**生产 IMetricView adapter（metric 事实源是 workflow/lab 的 metrics 记录域，
   属他轨）。两族的全部判据（band/tolerance/sum_is/approx…）由引擎层 fakes 覆盖
   （test_verifier_numeric 5 案 + 81-格聚合格）。补一个生产 metric adapter = 新功能方向（铁律禁止）。
2. **artifact.type × checkpoint/provenance/bounded_io = N/A-声明**：kind 嗅探是 fs 探针的
   职责（头注释："kind sniffing is by name only"），其他 adapter 无此语义。
3. **cross.output.consistency ✅直能力位说明**：该族在引擎经 IArtifactProbe（digest/sizes）+
   可选 IGridProbe；fs 侧真文件对比在本轨装配格中由 reproducibility.digest 行代表（同一探针
   路径），双文件 pair 形态（leftPath/rightPath）由引擎 fakes 覆盖；GDAL 侧经 grid e2e。

## 本轨新增格（提交 85ef6362f3）

- checkpoint_state_view：**Unreadable typed 格**（磁盘坏 JSON；枚举值此前零覆盖）。
- bounded_io：**直测负例格**（OverCap/Missing/坏 JSON/UTF-8 存在性）——它是所有 adapter 的输入闸门。
- 装配格：**4 生产 adapters + metric fake（声明缺口）→ 5 kinds 一评测**（exists/provenance/state/metric/reproducibility 全 Pass + seal）。
