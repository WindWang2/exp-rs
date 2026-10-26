# PR_COLLISION_MATRIX — R23-E 与并轨/基线的文件交集

基线：origin/master @ ad98a7a70（Open PR=0，执行期间复核无新增）。

## 本轨变更文件 vs 共享面

| 文件 | 变更性质 | 冲突风险与防线 |
|---|---|---|
| `tests/CMakeLists.txt` | 尾部 append-only 4 块（WP1/WP2/WP3/WP5 注册） | 五轨共同追加面。防线：全部位于文件尾 R23-E 注释界标内；push 前重 fetch + `scripts/check_merge_registration_integrity.py`（本轨 WP3）验证 HEAD⊇origin/master 注册并集 + 冲突标记 0 + `ctest -N` 证明目标在 |
| `tests/test_r23_e_*.cpp/.py` | 新增文件 ×3 | 零交集（新命名空间 r23_e_） |
| `scripts/check_merge_registration_integrity.py` | 新增文件 | 零交集 |
| `.planning/zcode-r23-E/**` | 轨私有 | 零交集 |

## 生产源码零改动声明

src/**、CMakeLists.txt（根）、CMakePresets.json、.github/workflows/**、packaging/** 均未触碰（对照 `git diff --name-only origin/master...HEAD` 于 push 前复核）。

## 与 R22 修复的防回滚核对

- #1708（RPATH/worker install/bundle guard/CI lint 顺序）：本轨零触碰其文件；WP4 将实机复跑其安装冒烟断言（worker 在树、RPATH $ORIGIN、bundle guard 套件）。
- #1714（daemon 协议/py 注册）：WP2 测试只驱动不改实现；worker_daemon.py 未修改（对照 diff 复核）。
- #1715（tests/CMakeLists 冲突标记）：WP3 门即为此类事故的 pre-push 防线（模拟合并验证见 TEST_RESULTS T1）。
