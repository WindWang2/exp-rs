# CURRENT_ARCHITECTURE — surface authority/seam 图（Phase 0 实测）

```
                    ┌────────────────────────────────────────────────┐
                    │           schema/capability authority           │
                    │  AtomicAlgorithmRegistry (rs:/gdal:/otb:/qgis:) │
                    │    └─ AlgorithmDescriptor + AgentMetadata       │
                    │         (memoryPolicy/costClass/taskFamily/…)   │
                    └──────────────┬─────────────────────────────────┘
                                   │ algorithm_tool_provider
     interaction_tool_provider ┐    │    ┌ spatial_tool_provider
   (InteractionToolRegistry)   ├── AgentToolCatalog (singleton) ── (SpatialToolRegistry,
                               │    │    │   spatial:/temporal:/workspace:/… 25+ families)
        data_tool_provider ────┘    │    └── exports: toMcpToolDefinition /
                                    │        toOpenAiToolDefinition / toJson
                                    │
   并行真值（catalog 外）:      kMetaTools[] (mcp_server.cpp:317)
   dataPlatformToolDefs()       harness/tool_manifest.h (derived)
   (data_platform_tools.h)      data/processing/algorithm_meta/*.json (#707 sidecars)

  消费面:
   MCP  sicnu_geo_rs --mcp  → tools/list = kMetaTools ∪ dataPlatform ∪ catalog
                                 （唯一全集中消费者）
   CLI  sicnu_geo_rs_cli    → 直读 AtomicAlgorithmRegistry（只见 Processing 子集；
                                 不链接调用 AgentToolCatalog）
   Pi   exp-rs-spatial.ts   → 消费 MCP tools/list(includeSchemas)，无自有 schema
   GUI  toolbox/workbench   → catalog + registry
   help src/help            → help registry ← live operator schema（另一投影）

  执行/进度 authority:
   TaskCenter (task 状态/进度/取消/日志，bounded)  ← MCP tools/call(异步任务) 与 CLI pipeline
   std::function<bool()> isCancelled + progress cb  ← 全库取消惯用法
   worker_protocol v1 (sicnu_worker)               ← framed progress/cancel 先例（host 域）
   runtime/observability telemetry ring            ← 诊断
```

## 本 track 新增 seam（after）

```
   surface_registry.collectSurfaceTools()  ← 唯一并集投影（catalog ∪ meta ∪ dataPlatform）
     ├─ MCP tools/list / artifact_read / 进度通知      （mcp_server.cpp 消费）
     ├─ CLI tools list|schema                          （cli_tool_commands.cpp 消费）
     ├─ CLI batch runner 的工具解析                    （cli_batch_runner.cpp）
     └─ tests/test_surface_parity.cpp                  （三方相等 drift gate）
   surface_progress  → MCP notifications/progress + CLI --progress-json 同词汇
   surface_redaction → MCP result/log 边界 + batch result index
```

依赖方向不变：cli → agent（catalog）；agent 内部 mcp_server → tool_catalog。
无新 CMake 目标（新文件进既有 sicnu_agent / sicnu_geo_rs_cli）。
