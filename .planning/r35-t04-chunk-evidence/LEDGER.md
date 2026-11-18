# LEDGER — R35-T04 goal-loop账本
第1轮 | 刷新仓库+建worktree(120677e6)+读Phase A源码, 10项静态候选全部在当前HEAD确认 | 源码证据见PLAN.md | 通过(调研) | 下一步: WP01-05实现
第2轮 | Phase A实现: WP01(tile_spec checked+validateTileSpec+pipeline/graph钩子) WP02(planner溢出可识别+三处饱和拒绝) WP03(cancel丢item/close drain) WP04(线程启动guard+fault点) WP05(graph typed入口) WP06(读上限1GiB+几何验证+stream EOF) WP07(journal eof判别+行长/总量上界) WP08(bitmap cap 2^27) WP10(严格key解析) | 源码完成, 构建排队(T05占host锁) | 未验证(待构建) | 下一步: WP13/14/16/18
第3轮 | Phase B/C实现: WP13(verify exact integer eq/ordering) WP14(composePacks累计预算) WP16(promotion错型数组typed拒绝) WP18(StudyReport严格数值准入) + 5个新测试文件注册 | 源码完成, 构建仍在排队 | 未验证(待构建) | 下一步: 构建绿灯后ctest
第4轮 | 构建链修复(OpenCV_DIR)+3轮修复: 测试编译错3处/vexing parse(函数指针→bool进jsoncpp!)/2处旧fixture int溢出与空spec/2处新测试字面量算术/spill分支漏设标志/tile_spec 2^64恰好回绕0改饱和 | 19套件直跑全绿(2次重复exit=0) | 通过 | 下一步: R1结果处置+R2+PR
