# R20-02 REVIEW_R2（独立终审）

评审人：fresh general-purpose 子代理（与 R1 及实现者隔离；全程只读，未编译未运行）。
对象：28e1891390...f74a27cddd 全 diff。
**结论：APPROVE**（附 1 项可达 P2 + 3 项 P3；P2/P3 均为本轮跟进修复，见处置列）

## 发现与处置
| ID | Severity | 发现 | 处置（本轮） |
|---|---|---|---|
| R2-1 | P2（基线既有、F1 同失效类可达残留）| probe 路径把 `deferredNoData.front()` 压进可能非空的尾部 pending 批而不先 flush——非整除栅格 + 尾部全 NoData tile 逐个 defer + 前缀未满批时，36 宽 probe 撞 64 宽 pending → "mixed tile sizes" 整跑失败 | ✅ probe 前置 `if (!deferredNoData.empty() && !writer && !batchMats.empty()) flushPendingBatch(totalTiles-1);`；**stash 法取得真 RED**（`mixed tile sizes (36x64 vs 64x64)` 实跑复现）+ 新测试（100×100 仅左上 64×64 有效、其余全 NoData、象限/NaN 计数断言）；恢复后 GREEN 待 build5 |
| R2-2 | P3 | F6 heal 分支先减账后 remove：remove 失败时对象在盘、记账却缩水 | ✅ 先 remove 成功再减（tornBytes 先取，防 remove 后 stat 为 0） |
| R2-3 | P3 | per-input interpolation 词表接受但从不按 feed 执行（多输入 stitch 只用全局值）——#646 式"声明但惰性" | ✅ 改为与 resize/pad 同姿态：非空即拒并指向全局段 |
| R2-4 | P3（覆盖备注）| F2 的 feather-blend 变体与多输入 lane 的 NoData 恢复无直接测试 | 记 RISK_REGISTER 跟进（blend 为纵深防御：FeatherAccumulator::add 跳过 NaN + addInvalidMask 兜底，R2 已核实语义；多输入恢复循环与单输入同构） |

## R2 核实通过面（摘要）
- P0 修复五向量不变量逐路径（几何 flush/满批/allNoData/OOM 阶梯/probe）等长成立；resize 异常路径无半推入。
- tileIndex-1 语义（首 tile 必不触发；lastTileIndex 仅影响进度标签；flush 天花板由 batchCories.front().y 派生）。
- F2 边界（三条映射路径均物化 core 尺寸；blend fed 窗口恒满尺寸）；NaN×accumulator 无毒化。
- F3/F4 影响面（parseManifest 唯一入口；随库 27 manifest 无误拒；仓内唯一 uncertainty 声明者 3 通道不受影响）。
- F5/F6 错误路径；范围/泄漏（8 文件 + .gitignore 5 行 append）；failure matrix（取消/异常→abandonOnFailure 清 staging）；测试质量无弱断言。
