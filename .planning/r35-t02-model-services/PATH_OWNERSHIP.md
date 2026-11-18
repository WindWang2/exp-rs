# PATH_OWNERSHIP — R35-T02

## 本轨写集（T02）

- `src/operators/runtime/python_worker_provider.*`
- `src/operators/runtime/provider_wire.*`、`src/operators/runtime/http_provider.*`
- `src/operators/framework/model_catalog.*`、`src/operators/runtime/model_runtime.*`
- `src/operators/runtime/model_execution_service.*`、`execution_contract.*`、`inference_plan.*`
- 既有 onnxruntime/opencv_dnn/openvino/tensorrt providers、`forward_cancel_bridge.*`
- `src/operators/runtime/model_ensemble.*`、`detection_postprocess.*`、`detection_fusion.*`、`model_publish.*`
- `src/runtime/gpu/**`、`plugin_model_runtime_bridge.*`
- 新测试 `tests/test_r35_t02_*` + 对应 `tests/data/` fixture
- `tests/CMakeLists.txt`：仅本轨新 target 的最小独立追加（不动其他轨块）

## 活动 PR 保留区（动态）

快照时 0 open PR → 空。执行中出现新 PR 时把其 files+head 精确登记到此节后再动笔。

## 排除（他轨整文件 owner）

- `python_environment.*`、`python_script_operator.*`、`python_managed_site_path.h`、`python_environments.qrc` → T05
- `src/python/isolated/**`、SDK worker daemon → T05
- `portable.h`、`durable_sidecar.*`、`src/geospatial/util/atomic_fs.*`、其余 data → T03
- TaskCenter/OutputCommitter/tool_call_dispatcher/workflow_session_controller/verify/chunk/governor → T04
- main_window_*、全局 TS/staging/Windows 交付 → T06
- `rs_spectral_index_operator.cpp`、`rs_change_streaming.cpp`、`src/experiment/evaluation.*` 归 T04/T01（非本轨）
