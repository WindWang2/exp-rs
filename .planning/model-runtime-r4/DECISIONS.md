# DECISIONS — Track 15 Model Runtime R4

## D-1 白名单边界修正：provider 文件实测位置
提示词把 model provider 写在 `src/plugins/`；实测 TensorRT/OpenVINO/ONNX provider 全部位于
`src/operators/runtime/{tensorrt_provider,openvino_provider,onnxruntime_provider}.{h,cpp}`，
编译门为 `SICNU_ENABLE_TENSORRT`/`SICNU_ENABLE_OPENVINO`/`SICNU_WITH_ONNX_RUNTIME`（src/operators/CMakeLists.txt:305-340），
`src/plugins/` 内 rg 零命中。WP-E 的"provider 守护矩阵"按实测落位到 src/operators/runtime provider 文件
（只守护既有开关语义，不新增 provider 代码）。src/operators/runtime/python_worker_provider.{h,cpp} 与
src/operators/framework/model_catalog.{h,cpp} 同理是 P1-8/WP-F 的实测接缝（提示词点名的
worker_protocol.h 实测不做 manifest 校验）。此为"白名单内实测空、接缝在白名单字面外"的测量修正，
非越界扩权：全部改动仍限定在 model-runtime/python 通道域。记账可审计。

## D-2 WP-A 接缝实测修正
提示词称 manifest 校验链在 `worker_protocol.h`+`trace_id.h`；实测 worker_protocol.h 是 LocalWorkerHost
帧协议（无 manifest 语义），manifest 校验真身在 `model_catalog.cpp` 的
`parseManifest`/`validateManifestJson`/`verifyArtifactLocked`（RUNTIME_FILE_MAP.md §七）。
WP-A 主体落在 model_catalog.cpp 校验链；worker_protocol.h/trace_id.h 侧仅做错误契约一致性
（frameErrorCode 分类对齐既有错误码族，不新开码表）。

## D-3 #1334 重叠策略
#1334 未合并（实测 2026-09-27）。本轨道基于 pre-#1334 master（15e5c66b5）：
1. WP-A 只做 #1334 未触碰的错误信息/一致性/顺序（其 model_catalog hunk 仅 defaultModelsDirectory）；
2. interpreter/worker_script 的 acquire 期校验**不重复实现**——本轨道仅把"catalog 纯校验面与执行面
   结论不一致"作为 P1-8 一致性缺陷记录并加 validate 层镜像断言（若 #1334 合并造成语义重叠，
   rebase 时以其语义为准做 union，不回退其白名单）；
3. tests/CMakeLists.txt 新 target 追加在尾部，5 个在途 PR 同文件冲突按 hunk union。

## D-4 子代理使用
Phase 0 用了 2 个只读 context-sweeper（chunk/exec 扫读、P1-8 校验链扫读）；Phase 5 预留 1 个只读
对抗评审。全程 ≤3，无递归派生。subagent token 消耗计入账本。

## D-5 既有红测处置
基线 ctest 中"本来就红"的测试（#1335 分类 + 本机实测）不修（非本轨道范围，除非落在白名单内且
与本轨道改动同因），逐个记账分类；本轨道 Oracle 是"零新增失败"，不是"全仓绿"。
（若基线红恰好落在 provider|model|runtime|session|interpreter 过滤面且根因在白名单内，
升级为修复项并记录。）

## D-6 OOM"真实层级"的判定口径
真实=在本机可控资源约束下触发逐级降档且降档有可观测痕迹（VRAM 配额用 fake backend 的预算记账+
真实分配路径；CPU 内存阶梯用测试专用小图/预算参数触发 planTileMemory 阶梯降档——配额是真实的
进程内记账而非模拟注入）。显存硬件级 OOM 本机无 GPU，不可真实触发——保留注入测试并标注"模拟"。
（口径依据 ADR 0130 D4/D5：预算记账失败路径本身就是生产路径。）
