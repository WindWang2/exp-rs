# [Operators/Runtime] TensorBlob::fromMat 对非连续多维 Mat 的回退拷贝零次执行——产出字节数合法但内容全零的张量

P3
Affected Location: src/sdk/../operators/runtime/tensor_blob.cpp:163-173（for (int r = 0; r < mat.rows; ...)）
Root Cause & Impact: dims>2 的 cv::Mat 的 rows 恒为 -1（cv::Mat 契约：rows/cols 仅 2D 有意义），非连续回退循环零次执行；bytes 已按 total()*elemSize 分配（vector resize 值初始化为 0）。结果：ROI 化的多维张量（插件/第三方 provider 的真实用法）得到 isValid()==true（字节数匹配）但内容全零的 TensorBlob——静默垃圾输入，无错误信号。subagent V 复核：当前 8 个第一方调用方全部传连续矩阵（clone/fresh blob），现网不可达，P3-latent 成立。
Reproduction: review/tests/F-OPS-2.cpp——4-D 连续 Mat 取 Range ROI 后 fromMat，断言 blob 内容与源平面逐字节相等。
Recommended Fix: dims>2 且非连续时先 clone() 复用连续分支，或按 mat.dims 逐维步进拷贝。
