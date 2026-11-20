# PATH_OWNERSHIP — R35-T04
## 活动PR保留区(执行时, 2026-11-19核对)
- #2149 (T06): resources/translations/sicnu_zh_CN.ts — 禁写
- #2150 (T01): src/processing/algorithms/{change_detection,image_enhancement,satellite_products,spectral_unmixing}.* — 禁写
- #2151 (T03): src/dataset/dataset_manifest.cpp, src/geospatial/{catalog/asset_query,fabric/catalog_service,metadata/canonical_metadata,raster/raster_reader}.* — 禁写
- 三者均含 .gitignore 与 tests/CMakeLists.txt → 仅做本轨最小独立追加, 不重排他人块
## 本轨主写集(T04)
src/runtime/chunk/**, src/runtime/exec/**, src/processing/framework/{tool_call_dispatcher*,output_committer*,fused_chain*}, src/verify/**, src/verify_adapters/**, src/agent/output_verifier.*, src/agent/cartography/cartography_operators.cpp, src/experiment/**(排evaluation.*归T01), src/study/**, src/experiment_studio/**(backend), src/operators/rs/{rs_spectral_index_operator.cpp,rs_change_streaming.cpp}(T04继承例外), scripts/dev/**
## 禁写
src/runtime/gpu/**, provider_wire, 模型runtime(T02); src/data/**(除execution_fingerprint归T04); src/platform/{portable.h,durable_sidecar.*}, src/geospatial/util/atomic_fs(T03); src/verification/**(归档,禁重接); Agent/Pi/Mission(T05); main_window_*/全局TS(T06); src/experiment/evaluation.*(T01)
## 追加保留区(推送前刷新 2026-11-19)
- #2152 (T02): src/operators/{framework/model_catalog.cpp,runtime/model_ensemble.cpp,runtime/python_worker_provider.cpp}, docs/models/, tests/data/py_worker_*, test_r35_t02_* — 禁写, 无重叠
- #2153 (T05): src/agent/llm_streaming_client.*, src/python/**, test_r35_t05_*/test_llm_streaming_client/test_worker_daemon_proxy.py — 禁写, 无重叠
- 共享处不变: tests/CMakeLists.txt(各自独立追加块), .gitignore(各自白名单追加)
