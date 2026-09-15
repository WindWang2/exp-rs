# CAPABILITY_MATRIX — cn-eo-product-physics-11

Before = 基线 a5b11b7f；After = 本 track 交付后。状态：implemented / refused-with-reason / degraded(说明)。

## 产品家族支持

| Family / 模式 | Before | After（目标） | 备注 |
|---|---|---|---|
| GF-1/2/6 PMS/WFV, GF-7 FWD/BWD | implemented | implemented（回归） | ADR 0157/0147 |
| ZY-3 TLC/NAD/FWD/BWD | implemented | implemented（回归） | |
| ZY-1 02C PMS/HRC | implemented | implemented（回归） | |
| HJ-1A/1B CCD, HJ-2A/B CCD | implemented | implemented + rpc_rpb 诊断完善 | WP-E |
| GF-3 SAR (FSAR 等) | refused-with-reason | implemented（declared-metadata 级：identity/mode/pol/几何/RPC/calibration 传递） | WP-B; 不做 sigma0 kernel |
| GF-4 (GEO PMI/PMS) | refused-with-reason | implemented（declared-metadata 级 + GEO 语义 passthrough） | WP-B |
| GF-5 AHSI/HSI | refused-with-reason | implemented（高光谱 band axis + subdataset inventory + stack/virtual access） | WP-C; HDF5 driver 缺失=typed 拒绝 |
| GF-5 其他载荷 | refused | refused-with-reason（细化） | |
| ZY-1 02B PMS/HR | refused-with-reason | implemented（PMS/HR 模式适配） | WP-D |
| ZY-1 02D/02E AHSI | refused-with-reason | implemented（复用高光谱机制） | WP-D |
| ZY-1 IRS / ZY-5 | refused | refused-with-reason（保持显式） | |
| CBERS（CRESDA 分发代发） | refused-with-reason | implemented（INPE generation parser，不混用 schema） | WP-E |
| CBERS 未知变体 | refused | refused-with-reason + UnsupportedVersion 诊断 | |

## Registry / schema

| 能力 | Before | After |
|---|---|---|
| 字段类型/单位 guard | 松散（tests 保底） | v2 逐字段严格验证 + 生成式 validator |
| unknown key forward-compat | ignored+reported | 保持 + validator 汇总报告 |
| pan/ms 交叉引用 | 运行时解析 | validator 静态校验 + drift test |
| 高光谱 band axis | 无 | `band_axis` 描述块 + 落盘波段展开 |
| bad-band 语义 | 无 | passthrough + stack 时 explicit 标记 |

## 导入 / 诊断

| 能力 | Before | After |
|---|---|---|
| ImportPlan | plan（无 dry-run 封装） | explicit dry-run + constituent graph + checksum |
| 取消 | operator context 有 cancel | stack 循环检查点 + 零半成品验证 |
| 只读 source | 未显式测试 | 显式测试 + typed 报告 |
| 中文路径 | test_cn_products 已有 | 回归 + 新 family 覆盖 |
| GUI 诊断 | 对话框显示结果 | constituent graph/缺失项可视化（消费 JSON） |
| CLI 诊断 | inspect 命令 | 同一服务输出 graph |
| agent | io_tools 产品识别 | product plan 工具（同一服务） |

## Fixture corpus

| 类型 | Before | After |
|---|---|---|
| 合法/损坏/缺字段/多 generation | 测试内嵌字符串 | `tests/fixtures/cn_products/**` + golden metadata + drift gate |
