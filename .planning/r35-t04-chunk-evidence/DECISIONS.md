# DECISIONS — R35-T04 (unattended常规决定)
- D1 WP03契约: cancel()=fail-fast——丢弃未交付item, pop/tryPop/push一律false; close()=正常排空drain. 依据: 头注释明示cancellation语义用于失败传播/协作取消, 取消后交付半途数据违背"停止边界无额外业务consumer调用"oracle. ChunkGraph F-A-1注释已预期cancel清空队列的语义.
- D2 WP04注入: 沿用SICNU_FAULT_POINT("chunk.pipeline.threadStart")于每根线程创建前, 走真实异常unwind路径(与std::thread构造失败同路径), 不新建thread pool/mock.
- D3 WP06上限: kMaxTilePayloadReadBytes = 1 GiB(2^30), ChunkCorruptTile typed拒绝; 合法几何先验(bufferElementCountChecked)在分配前执行.
- D4 WP08: Config::maxResumeBitmapTiles上限默认1<<27(134M tiles=16MiB bitmap), 构造期typed拒绝; 不发明checkpoint v2.
- D5 WP10: tileRunIdentityFromKey改严格解析(恰3×16hex+2连字符,长50), 失败仍返回all-zero形状(保持现有nullopt-shaped合同), 头注释更新说明gate方式(canonical重序列化比较); 不加optional新API(YAGNI, 仅测试调用).
- D6 WP07: 坏行判定改用stream eof状态(完整换行坏行=corruption, 未终止尾行=torn tail); 行长上限4096, journal总扫描上限256MiB, 超限=ChunkCorruptTile.
