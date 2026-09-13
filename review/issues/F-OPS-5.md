# [Operators/Detection] 全栅格 NMS/去重 O(n²) 且位于取消检查点之外——逼近 max_detections 预算时 worker 长时间不可取消阻塞

P2
Affected Location: src/operators/runtime/detection_postprocess.cpp:150-163（nonMaxSuppression 双层循环）、:166-187；调用点 src/operators/runtime/detection_tile_engine.cpp:417-427（dedup 先于 throwIfCancelled）
Root Cause & Impact: output.detection.max_detections（默认 100000）实际是 NMS 输入上界而非纯拒绝线；去重为 O(n²)（10 万框 ≈5×10⁹ IoU 比较），且 dedupDetections/nonMaxSuppression 不接收取消谓词。高候选密度输入时 worker 被阻塞数十秒至分钟级，用户取消延迟到整段结束。
Reproduction: review/tests/F-OPS-5.cpp——纯函数调用 dedupDetections 传入 10 万个互不重叠框，断言 1s 内完成或提供可取消重载。
Recommended Fix: 空间分桶限制 NMS 比较域；dedup/NMS 接受 std::function<bool()> 取消谓词（每 N 次迭代检查）；或把 maxDetections 语义改为 NMS 输入预算并分块。
