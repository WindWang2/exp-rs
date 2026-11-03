# tests/CMakeLists.txt 撕裂修复记录（master@10652fd003 无法 configure）

## 损坏事实
- `cmake --preset dev-default` 于 tests/CMakeLists.txt:17426 报 "Function missing ending ')'"。
- 根因：R25/R26 八轨 append-only 块在系列合并（"Merge origin/master into X" / PR #1724–#1731）中被互相嵌入对方函数调用内部；
  pre_wb_master(10b1a971c9) 已含 r25-A/B/C 互嵌；f26b573fab 再嵌入 workbench；master 尾部 617 行为 8 轨文本的撕裂交错。
- 额外发现：撕裂合并还**丢行**（重建含 31 行 master 缺失内容：r25-B OpenCV 门体内注册、r25-D 4fceb2354c 恢复的 toolbox 声明等）。

## 重建方法（可复验）
1. base = 608c1bd1c2:tests/CMakeLists.txt（17278 行，尾部干净 r23-B 块，零 r25/r26 引用）。
2. 每轨块 = `git diff <轨基> <该轨最后一个 own file-commit> -- tests/CMakeLists.txt` 的净增行：
   r25-D(4fceb2354c, 基 608c1)、r25-E(6003006f37)、r25-B(dd88176144)、r25-C(a90bb64691)、r25-A(8bb3b44de1)、
   r26-harness(f433fc88bf, 基 2610cb)、r26-workbench(fb01ad7979)、r26-science(9fdf8d8aaa)。
3. 顺序 = PR 合并序 #1724→#1731。new = base + Σ blocks。

## 证明
- 行多重集（非空）：master-only = 0（master 无内容丢失）；recon-only = 31（master 撕裂时丢的行，全部找回）。
- 注册目标：master 文件 969 个 test 目标，重建后 969 个，缺 0、重 0。
- configure（dev-default）通过 = 结构有效。
- 本轨自己的新测试块继续 append-only 追加。
