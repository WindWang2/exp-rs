# Resource Measurements

## Environment discovery (not a performance result)

- Host: `cachyos-3080`; RAM 62 GiB, approximately 33 GiB available at initial `free -h` reading.
- Canonical host lock: `/home/kevin/.cache/exp-rs-agent/host-build`; no lock file was present at discovery.
- Build cap: 4 across the host; test cap: 1; OMP/OpenBLAS/MKL: 1.
- CMake 4.4.5, Ninja 1.13.2, Python 3.14.9.

No wall-time, peak-RSS, large-raster or model benchmark has been executed. Any later optimization claim must use fixed input/seed/configuration and report baseline and comparison wall time, peak RSS and noise rather than an arbitrary percentage.

## Provider environment for WP18 (discovery only)

- Existing host build has `SICNU_WITH_ONNX_RUNTIME=OFF`, `SICNU_BUILD_OTB=OFF`, `SICNU_EMBED_PYTHON=OFF`, `ENABLE_TESTS=ON`. ORT-specific in-forward cancellation is therefore **NOT_EXECUTED** unless a real dependency is later provided; it must not be inferred from fake providers.
- Real CPU model fixtures exist: `tests/data/test_infer_two_input_add.onnx`, `test_infer_identity.onnx`, and `test_infer_addmul.onnx`. The OpenCV DNN lane and loopback HTTP lane can provide genuine CPU/network behavior once built; this is distinct from ORT/GPU evidence.
- GPU/TensorRT/OpenVINO hardware and real provider availability are not established by repository files. No such run will be claimed.
