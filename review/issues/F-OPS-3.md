# [Operators/QA] rs:qa_mask 对不可读 QA 样本 fail-open（NaN/负哨兵/声明 NoData → 词 0 = clear），质量门失效方向错误

P2
Affected Location: src/operators/rs/rs_qa_mask_operator.cpp:296-311（convertSample 返回 0）；:106-141（buildMaskRule 的 SCL 类集任何 selection 都不选 class 0/SclNoData）
Root Cause & Impact: QA 词不可读时返回 uint16 0——Landsat QA_PIXEL 词 0=全位清零=clear；SCL 值 0=NO_DATA 类且从不被掩蔽（含 mask=all）。质量掩膜是下游云掩蔽的门：QA 波段带声明 NoData 或损坏的区域被判 clear，云/雪像素流入合成与指数计算，科学结论被污染。仅一条 warning，管线不失败。#699 修复 UB 时注释明确"保留历史 clear 结果"——本 issue 针对该语义本身。
Reproduction: review/tests/F-OPS-3.cpp——Float32 QA 波段含 NaN 跑 rs:qa_mask，断言 NaN 像素掩膜值==1；现实现为 0。
Recommended Fix: fail-closed——不可读 QA 词产生 masked=1 或独立 255=unknown 输出类；SCL "all" 至少纳入 SclNoData。同步更新 #699 的测试。
