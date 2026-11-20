# R35-T01 ACCEPTANCE（切片1：WP01–WP06）

## 状态总览

| WP | 状态 | Oracle 落点 | 证据 |
|---|---|---|---|
| WP01 | DONE | C2L2 SR 键绑定 MTL MULT/ADD；混合 SR+ST 仅 per-band；非均匀不 stamp；置换按名绑定；(stored-offset)/scale==DN*mult+add；DN 原样 | r35_t01_landsat:: 4 cases Passed |
| WP02 | DONE | helper -1/0→全部band 在扫描前；1/B 成功；B+1 早拒 typed 消息且无输出文件 | r35_t01_science:: processPcaFile… Passed |
| WP03 | DONE | 0/负→全部band；B+1 早拒；单band 拒绝保留；整体缩放×100 分量不变（1e-2 容差，numpy 复算实际 ≤7e-7） | r35_t01_science:: 2 cases Passed |
| WP04 | DONE | 常量双图/单侧常量 → false+“zero variance”；有方差同图 → 成功且 chi²≈0（1e-9 容差，复算 7e-24）；B+1 样本饥饿消息保留；真实局部变化 >0；IR-MAD 常量对同拒 | r35_t01_science:: 2 cases Passed + 旧 MAD 流式/等价回归 |
| WP05 | DONE | 含 NaN/±Inf 行的 cube 仍提出 3 顶点且输出∈原有效谱；有效<requested → false（VCA 与 PPI）；全 NaN → false 非零填充 | r35_t01_science:: 2 cases Passed |
| WP06 | DONE | PPI seam 恰好 requested（新 case）；FCLS QA 混合 NaN 行时有限且=唯一有效行的违反量；全 NaN → NaN 非伪 0 | r35_t01_science:: 2 cases Passed |

## 实际命令与退出码（本 HEAD）

1. configure：exit 0（含 host 级 OpenCV_DIR）
2. build 6 目标：exit 0
3. ctest 旧 78 case：`100% tests passed out of 78`，exit 0
4. ctest 全 91 case（两遍验证）：`100% tests passed out of 91`，exit 0（13 新 case 全 Passed）

## E2E 链对齐

- E2E-01（部分）：Landsat C2L2 → stack → affine stamp → 重开逐 band 验证（新 WP01 case；#2147 谱指数 consumer 只读未改）
- E2E-02（部分）：PCA/MNF 参数域 + 端元/解混 seam 合同（新 WP02/03/05/06 case）
- 未覆盖（后续切片）：WP07–WP20；可见桌面；Release；Windows；完整 CTest 全集
