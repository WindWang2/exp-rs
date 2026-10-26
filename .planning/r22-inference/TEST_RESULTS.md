# R22-03 TEST RESULTS

基线 `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（= origin/master 实测，2026-10-25 gh api 双核对）。
分支 `agent/r22-inference-20261025T132554Z`，commits：`b55aa5fd9a`（修复）→ `1381e5d80a`（R1 P3 补强）→ `0e…`（R2 注释修正）。

## 环境

- 构建机：40 核 / 62GiB RAM；Release；`-j2/-j4`（共享锁 `/tmp/exp-rs-r21-build.lock`，排队纪律见 LOOP_LEDGER 轮3）
- 依赖：vendor prefix（GDAL/PROJ/GEOS/GSL/NetCDF/QCA）+ 系统共享 zlib/zstd/expat/sqlite3 + OpenCV 5.0.0 + Qt6 6.12
- 环境配方（复刻 r21-B，CROSS_TRACK X1）：`build-r22-inference/link-shim` + `-Wl,--allow-shlib-undefined` + 系统共享 sqlite3/zstd
- 运行时：`PROJ_LIB=<vendor>/share/proj`；`LD_LIBRARY_PATH=build-r22-inference/link-shim:<vendor>/lib/x86_64-linux-gnu`；`QT_QPA_PLATFORM=offscreen`

## RED（未修复基线上，@911e0b17 + 仅新增测试）

`tests/test_r22_inference_feather`：**5 用例全败 / 1136 断言中 39 失败**，失败值特征与 #1706 数学精确吻合：

- 所有类别波段 = 0.33333 = mean(0.8,0.1,0.1)（槽位别名）
- uncertainty 槽 = 0.31314 = (1.0+0.25257)/4（4 槽含权重 1 的别名和 / 4 倍权重）
- Labels 右半全 0（argmax 全并列）等

完整输出存 `.planning/r22-inference/RED_run1.txt`。

## GREEN（修复后，关键验证双跑）

| 套件 | PASS 1 | PASS 2 |
|------|--------|--------|
| test_r22_inference_feather（本轨新增，7 用例） | 1203 断言全过 | 1203 断言全过 |
| test_model_runtime_9（M5/#r18B 单通道羽化金样例，17 用例） | 1226 断言全过 | 1226 断言全过 |
| test_model_runtime（主推理族，25 用例） | 110429 断言全过 | 110429 断言全过 |
| test_r14_B_tile_engine（分块引擎族，4 用例） | 20695 断言全过 | 20695 断言全过 |

R1 P3 补强后（Mask/remap/poison）再次双跑：1203×2 全过。
R2 注释修正后复跑：全过。

## NOT_EXECUTED（如实申报）

- **CUDA/GPU 路径**：本机无 NVIDIA GPU——所有 GPU 实测（TensorRT/CUDA EP、显存账本、设备切换）**NOT_EXECUTED**。本次修复只触及 CPU/GPU 无关的纯数学累积层（cv::Mat float 运算），由 CPU fake-runtime 测试覆盖其全部逻辑。
- 线上 CI：按任务书不参与完成门，未等待未轮询。
