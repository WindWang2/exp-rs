# R25-A TEST_RESULTS（实测回填）

## 构建命令
flock $HOME/.cache/exp-rs-build.lock cmake --build build-r25-A --parallel 2 --target <16 targets>
EXIT=<回填>

## RED 证据（master 基线）
<在各缺陷测试上以 master 代码运行的失败断言 —— 回填>

## GREEN×2（HEAD）
ctest --test-dir build-r25-A -j1 --output-on-failure -R "test_r25_a_.*" 
run1: <回填>  run2: <回填>

## 邻域回归
ctest -R "test_terrain_|test_spectral_classification|test_sar_foundation5|test_r24_a_" 
<回填>

## 性能基线
ctest -R test_r25_a_perf_baseline → PERF_BASELINE.md 回填
