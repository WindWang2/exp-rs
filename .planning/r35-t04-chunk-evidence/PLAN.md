# PLAN — R35-T04 (20 WP, 阶段A→B→C)
## 已确认差距(当前HEAD源码逐条核对, 2026-11-19)
- WP01 tile_spec.h: bufferElementCount() 无checked乘法(INT_MAX×INT_MAX×8在size_t回绕); validateBuffer/decode无几何一致性检查. CONFIRMED
- WP02 memory_planner.h: 溢出饱和UINT64_MAX与budget==UINT64_MAX比较 Admit违背"overflow always refuses"注释; 溢出不可识别. planTileMemory当前仅测试调用. CONFIRMED
- WP03 bounded_chunk_queue.h: cancel()后已有item仍被pop/tryPop交付(与头注释"A push/pop pair that observes cancellation returns false"矛盾); 文档行"later pops fail immediately"与drain语义自相矛盾. CONFIRMED
- WP04 chunk_pipeline.cpp run(): 局部vector<std::thread>中途emplace_back抛错→析构joinable线程→std::terminate; queues(unique_ptr)同样UAF. CONFIRMED
- WP05 chunk_graph.cpp: addStage/addSink/queueFor仅assert(id range); NDEBUG下越界/负ID→m_nodes[id] UB; Fn空未检查(addJoin有typed id检查但无空Fn检查). CONFIRMED
- WP06 disk_tile_store.cpp: readFileImpl分配仅被实际文件大小约束, 无单tile读取上限(有效巨文件→bad_alloc); decode不验几何一致性(bufferWidth==width+2*halo等), digest自洽的坏几何被接受; path file_size与已开stream存在TOCTOU. CONFIRMED
- WP07 resumable_tile_run.cpp loadJournal: getline无行长/总扫描上界; 坏末行probe-based判断把"完整换行坏末行"也当torn tail截断(应为ChunkCorruptTile). CONFIRMED
- WP08 ResumableTileRun: totalTiles可达INT_MAX→bitmap 256MiB无预算拒绝; "能表示"≠"能运行". CONFIRMED
- WP10 tile_run_contract.cpp tileRunIdentityFromKey: sscanf接受尾随junk/短写"1-2-3"; 失败返回all-zero与合法all-zero身份不可区分. 仅测试调用. CONFIRMED
## 实施顺序
A: WP01→05(新测试test_r35_t04_chunk_contracts.cpp) → WP06-08+10(test_r35_t04_resume_store.cpp) → 构建+全chunk套件
B: WP11-15(OutputCommitter/Verifier)
C: WP16-20(experiment/制图) — 按剩余容量真实推进, 每WP独立oracle
