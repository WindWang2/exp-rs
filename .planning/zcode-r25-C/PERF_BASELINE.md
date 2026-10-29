# PERF_BASELINE — R25-C

本轨变更均为 O(1)/O(steps) 投影与元数据字段，无新热点路径：
- algorithmEntryMap 每条目新增 ≤3 个小字段（仅声明非空时），列表分页上限不变（500）。
- runForRunIdReadOnly：内存命中 O(runs)；checkpoint 回退一次原子文件读（仅 run_id 查询路径）。
- bridge onGeneration 回调：每次成功握手 ≤1 次 tools/list 再列（仅 respawn 代际 ≥2 且已有注册集时）。
未引入 wall-clock 硬门槛；无性能声明需要基准（如评审异议可补测）。
