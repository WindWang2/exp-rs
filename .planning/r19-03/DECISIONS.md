# R19-03 DECISIONS（无人值守：选项 + 所取默认）

- **D-1 workspace_commands 扩界**：初判 #1571 需扩界写 `src/agent/commands/`；实读发现 master 已修（undo/redo 锁外回调 + 部分失败不入 redo 栈）→ 撤销扩界，零改动。取默认：不越任务书写集。
- **D-2 教学边界从严**：#1664 文件集虽零 src/app 文件，仍将 `src/app/teaching/**` 划只读（#1565 跳过）。理由：任务书"不触碰教学业务逻辑"从宽解释，避免语义冲突。
- **D-3 不新增 editingSessionMenuActions() 访问器**：r18-D 为 digitizing 加访问器是因为 toolbar/menu 双胞胎共享 objectName、editingAction 偏向 toolbar 致菜单侧回归不可见；本轨三命令无工具栏孪生，objectName 唯一，findChildren 扫描即完整固定。取 YAGNI 默认。
- **D-4 SpectralLibraryDialog 构造期初始化按钮态**：match/save 按钮默认 enabled 直到首次 showEvent。两个选项：(a) 测试里 show() 规避；(b) 构造尾补 updateSpectrumSummary() 使未显示状态也诚实。取 (b)：一行、与"操作成功却无反馈/按钮状态诚实"的专项目标一致，且让新断言天然可红。
- **D-5 RED 阶段测试不引用尚不存在的 API**：访问器/签名类改动会让 RED 变成编译失败（非行为证据）。RED 断言只落在既有公共面；新增测试面随修复提交（GREEN 阶段断言扩展）。
- **D-6 #1560 不做实现**：取消通道需要 task lambda 能触达取消探针（改 seam 签名 + 全部对话框调用点）；与最小变更原则冲突，记录设计于 CROSS_TRACK_FINDINGS，本轮不做。
- **D-7 全局锁目录取 git common dir 下 r19-global-j2-build**：任务书指定；与 r19-02 轨实测一致（同一锁目录已在互斥）。
