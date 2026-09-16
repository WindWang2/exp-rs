# FAILURE_MATRIX — 失败/取消/资源语义（本 track 新增面）

| 面 | 语义 | 位置 |
|---|---|---|
| 探针解析失败 | typed DATASET_NOT_FOUND / ENTITY_AMBIGUOUS（唯一 resolver），无猜测 | grounding_probes.cpp |
| 探针未知 scope | INVALID_PARAMETER，I/O 之前拒绝 | 同上 |
| 探针超时 | 不假装可中断：elapsed_ms + deadline_exceeded 诚实回报 | 同上 |
| 探针 grounding 失败 | 透传底层 typed code（errorCode），不吞错 | 同上 |
| 理解缓存键 | (path, revision) / (path,size,mtime)，文件改写必 miss | understandingCacheKeyFor（唯一实现） |
| 不可解析时间戳 | 解析失败列入 unparseable[]，绝不静默丢弃 | workflow_facts.cpp |
| 超 bounds 日期/掩膜 | truncated=true 诚实截断（irLimits 纪律） | 同上 |
| naive 时区 | UTC 约定 + NaiveAsUtc 假设标记 | parseInstant |
| degree CRS 分辨率 | unknown_meters，不做纬度相关换算 | spatialResolutionFacts |
| 声明时间契约无观测日期 | check skip（非 pass 非 fail） | workflow_analysis.cpp |
| probe 失败 → compile 路径 | 仅当 ref 可解析且 .json 描述符才合成 collection 事实；否则无合成 | workflow_planner.cpp |
| sidecar 写失败 | QSaveFile atomic + typed error，不碰产物 | provenance_projection.cpp |
| metadata.compiler 已存在 | digest 不同 → compiler_superseded 保留一层 | attachToWorkflowJson |
| 拒绝的修复 | 永不自动插入；prepared decision auto_applicable=false | workflow_repair.cpp |
| explain 截断 | causes ≤8 + serialized_bytes 计量 + truncated 标记 | workflow_explain.cpp |
| corpus 行为测试宿主不可执行 | canary 判定 + 显式 skip 理由（不静默） | pi/test/scientific_workflow_compiler_11.test.mjs |
