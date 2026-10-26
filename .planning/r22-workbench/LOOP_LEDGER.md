# R22-05 LOOP LEDGER

轮次 | 假设 | 文件 | 测试命令 | 真实结果 | 下一步
---|---|---|---|---|---
R0 | 盘点+分诊+worktree | .planning/* | gh/git 只读命令 | 5 open PR、#1695-1 唯一 live 入口 | W1 RED
R1 | #1695-1 缺陷 live（async runner 光标/悬垂父指针） | src/app/dialogs/async_*（未改） | 测试 tests/test_r22_workbench_async_runner.cpp 已写+注册 | 构建中（qgis_core 31% @01:34） | 等 RED 运行
R2 | 双子代理审计 14 候选 → 全部对 911e0b17a3 人工复核：live=W2 temporal 页路由(P2)/W3 zoom 原生 CRS(P2)/W4 组删除租约泄漏(P2)/W5 log 标签(P3)/W6 ImportCenter 孤儿(P3)/W7 DPI：(P3)/W8 复数(P3)/W9 工具箱关闭逃逸(P3)/map tool 计数(P3)/batch 外部取消(P3)；已修=#1632 全部；延后=python 编辑器/temporal 同步 GDAL/魔棒/cross_section（记 CROSS_TRACK） | src/app 多文件 | W2-W5 行为测试 + 契约测试已写+注册 | 待构建 | 等 W1 构建完成后统一 RED
R3 | 环境修配：vendor GDAL wrapper2_/静态 expat/sqlite3/zlib + RUNPATH 拉 vendor Qt6.10 → R21-A 配方(--allow-shlib-undefined + /tmp/r21a-deb stub) + 系统 .so pin + 本轨私有 /tmp/r22wb-qt Qt shim | build-r22-workbench | 5 目标 BUILD_EXIT=0 | 通过 | RED 运行
R4 | RED 全量实证（未打补丁）：async_runner 2 红断言+SIGSEGV(父亡 UAF)；view_tree W3 :130 红(3857 度误当米)+W4 :150 红(租约未释放)+组默认动作 SIGSEGV；temporal 6 红(3 算法页路由缺失)；log_tagfilter 1 红(键入标签被忽略)；ui_contract 10 红(外部取消/孤儿 ImportCenter/DPI：/item(s)/close 逃逸/计数 19) | 5 binaries | 输出见上 | RED 确认 | 应用修复 a/b/c
R5 | GREEN 实现迭代：W1 补 null-parent 完成送达(m_hadParentWidget)；W4 夹具改生产同款"先插新再删旧"+代理坐标系 currentIndex；W3 oracle 改中心精确+包含+有界放宽(画布纵横比 pad)；W4 生产补 qobject_cast 组父 | src/app+tests | 见 R6 | 通过 | 全量跑
R6 | GREEN×2 全绿：async 24/4、view_tree 70/2、temporal 30/1、log 24/1、ui_contract 30/5（两遍一致，178 断言×2） | build-r22-workbench/tests | 输出在案 | 通过 | 邻域回归
R7 | 邻域回归 11 目标全绿：async_algorithm_runner 8/2、ui_task_center_contract 169/6、raster_processing_dialog_base 120/11、gui_job_adapter 29/3、active_view_host_viewport 12/2、log_panel 7/4、batch_processing_dialog 35/5、layout_designer 48/9、workspace_browser_wiring 39/4、data_manager_panel 101/16、m2_batch_b_dialogs 193/19 | 同上 | 输出在案 | 通过 | 分包提交
