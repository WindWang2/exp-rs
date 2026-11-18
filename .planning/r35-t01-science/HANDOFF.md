# R35-T01 HANDOFF

## 对 T04（MAD 消费方 owner）

- `madFinalize`（src/processing/algorithms/change_detection.cpp）现在对 `trace(SXX)<=0 || trace(SYY)<=0`（含 NaN）返回 false + "MAD requires non-constant input: the before/after image has zero variance."。
- IR-MAD 入口 `irMadChange` 同样拒绝（"IR-MAD requires non-constant input..."）并 NaN 填充输出。
- 影响：`rs_change_primitives.cpp` / `rs_change_streaming.cpp`（T04 整文件）在常量输入场景将从"成功+全0 chi-square"变为"失败+typed 错误"。两处现有代码已按 false 返回处理（failNaN/错误通道），无需改动即可正确传播；如 T04 需要在 operator 层把该消息映射为用户可见文案，属 T04 写集。
- 科学依据：常量图的 CCA 在任何正则化下都不携带信息；此前 varMad=2 的 chi=0 是 ridge 制造的伪统计（#2096）。

## 对 T04（IR-MAD 流式镜像文案，R1 发现 #2）

- `rs_change_streaming.cpp` 的 IR-MAD 流式实现（:1525-1529 同款 ridge、:1604 "degenerate weights" 拒绝路径）在常量对上仍会失败，但错误文案与本轨 `irMadChange` 新增的 "IR-MAD requires non-constant input: ... zero variance." 不一致。是否对齐文案由 T04 决定（该文件整文件归 T04，本轨未改）。

## 对 T05（rs:spectral_unmixing operator，如后续接线）

- typed seam `exp_spectral::SpectralUnmixing::extractEndmembers` 合同统一为"恰好 endmemberCount 条谱或 false"；PPI 分支不再静默短返回。调用方（当前仅测试）必须检查返回值，成功时 `outEndmembers->size()/bandCount == endmemberCount` 恒成立。
- `unmixFcls` 的 `meanSumConstraintViolation` 现在只统计全有限 abundance 行；全 NaN 输入时为 NaN（诚实未知）。

## 对 T06（无接线需求）

- 无。Landsat 导出 stamp 变化只涉及 stack 产物 metadata，UI 消费（若有）读取的 key 与语义不变，只是 SR_ 键名场景现在真的有值。
