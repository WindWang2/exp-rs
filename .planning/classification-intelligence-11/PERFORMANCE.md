# PERFORMANCE — 资源模型与逻辑规模

## 硬约束（GOAL）

- `CMAKE_BUILD_PARALLEL_LEVEL=2`，build `-j2`→压力高 `-j1`；禁止 `-j$(nproc)`。
- `CTEST_PARALLEL_LEVEL=1`，测试 `-j1`，`QT_QPA_PLATFORM=offscreen`。
- 编译期间 60s 采样 CPU/RSS/load。

## 主机

16 核 / 64 GiB（启动时 load1≈2.5，内存可用 ≈55 GiB）。

## 逻辑规模模型（新能力）

| 能力 | 复杂度 | 有界声明 |
|---|---|---|
| Platt 拟合 | O(iter × N) per class | N=100k 分数线性；iter≤100 固定上限 |
| isotonic PAV | O(N) per class | 栈式单遍 |
| Brier/ECE | O(N×K) | bins 固定 ≤1000（可配，默认 10） |
| entropy/margin（图） | O(rows×K) | 逐 tile 常数内存（pipeline 流式） |
| ensemble disagreement | O(rows×K×m) | m=成员数，调用方控制 |
| spatial folds 生成 | O(N log N)（按坐标排序）或 O(N)（网格桶） | 块桶 hash |
| 泄漏 audit 距离 | O(N_test × N_train) 上限 → 用块桶近邻剪枝 | audit 报告块级 min-dist（近邻剪枝后线性量级，语义=块间最小距离） |
| 对象邻接图 | O(segments × 平均边数) | segment 上限参数（默认 2e6），超出 typed refusal |
| 平滑迭代 | O(iter × segments) | iter ≤ 配置上限（默认 10） |
| feature schema fingerprint | O(features) | — |
| 100k 样本测试 | NB 训练 O(N×B×K)、预测 O(N×B×K) | RUN_SERIAL + TIMEOUT；断言不变式不断言时长 |

## RSS 防线

- pipeline 新输出（uncertainty 多波段栅格）沿用 256×256 tile 流式，不整图驻留。
- ensemble disagreement 接口按行流式（调用方逐块喂入成员概率）。
- 校准拟合输入为分数向量（float），100k×K ≈ 数 MB。
