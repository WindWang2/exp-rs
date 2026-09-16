# PERFORMANCE — 资源模型（启动版，P5 回填实测）

## 构建/测试资源硬限
- build: cmake --build build-dev -j2（CMAKE_BUILD_PARALLEL_LEVEL=2）；RSS>70% 或 load 过高 → -j1。
- test: ctest -j1；新测试全部 bounded（fixture ≤ 数 MB；无 wall-clock gate）。

## 逻辑规模声明（本 track 新增面）
- stage_ledger sweep：目录枚举 O(staged 文件数)，无递归全盘扫描；ledger 文件条目上限 4096（超限 refusal）。
- finalize_manifest digest：流式 sha256，1 MiB 块，内存 O(1)；大文件不整读。
- verify：同上流式；不缓存全文件。
- subdataset_inventory：条目上限=InspectOptions::maxSubdatasets(64) 同源；URI 显示一律 display() 红显。
- metadata_patch：白名单字段数上限 64/次；无像素 IO。
- COG deterministic：NUM_THREADS=1 明确声明为正确性选择（吞吐换字节一致），非性能回退。
