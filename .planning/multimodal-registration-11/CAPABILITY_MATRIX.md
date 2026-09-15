# CAPABILITY MATRIX — multimodal-registration-11（before = master a5b11b7f10）

| 能力 | Before | After（本 track 后） |
|---|---|---|
| 单模态特征匹配（SIFT-like/ORB-like + RANSAC） | implemented（D14） | 不变（不重写） |
| Phase correlation（平移粗配） | not-supported | implemented：masked FFT phase corr + 亚像素 + 峰值 SNR 置信 |
| 跨模态（光学-SAR）窗口匹配 | not-supported | implemented：log-gradient 方位直方图 + RANK 描述子 + MI 度量（诚实命名，非通用 SIFT） |
| 金字塔 coarse-to-fine | not-supported（单 octave） | implemented：层数/窗口自适应，逐级过滤 + 终筛 RANSAC |
| 空间覆盖约束匹配 | not-supported | implemented：网格 coverage 配额（每格最多 N 对，空格拒绝） |
| 模型选择（translation→…→TPS，证据驱动） | 部分（仅静态决策树） | implemented：k-fold CV + ≥10% 改进 + κ 门限 + per-model 证据表 |
| RPC bias 精化 | 部分（常数中位数平移） | implemented：constant/affine CV 选择 + 高度敏感性 + 残差诊断（向后兼容，默认行为不变） |
| 多景 stack registration | not-supported | implemented：reference 选择 + pair graph + 全局平移/仿射 LS + 闭环 drift + per-pair 置信 |
| bundle block adjustment（RPC 系数求解） | not-supported | not-supported（D-008，明确拒绝并文档化） |
| 质量产品 | 部分（RMSE/残差散点） | implemented：CE90 经验分位数 + 残差矢量场网格 + 局部置信度 + JSON report（atomic 写出） |
| Agent 工具 | 部分（编译未注册） | implemented：注册 + `multimodal_register`/`stack_register` action + 结构化 explain |
| `rs:` headless 配准算子 | not-supported | implemented：`rs:register_images` / `rs:stack_register` |
| GUI 双窗 tie-point review | implemented（D14/D18） | 不变；GCP 提交路径 fail-closed（#1005） |
| 图像级合成 warp fixtures | 部分（correspondence 级） | implemented：known homography/affine 图像 warp + SAR-like 斑点/辐射 decorrelation + outlier 注入 |

Degradation 声明：无 OpenCV 时 GUI SIFT 对话维持既有 graceful error；本 track 新算法不依赖 OpenCV。无 GSL 时 qgsleastsquares 路径维持既有缺省（不在本 track 修改）。
