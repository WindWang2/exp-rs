# TEST_MATRIX — D19

| Suite | Focus | Status this box |
|-------|-------|-----------------|
| test_d19_dataset_foundry | role, feature join, catalog (+100k scale), QA, leakage Fail, FoundryService, version evolution | authored; **not-executed** (no cmake/g++) |
| test_d19_benchmark | definition, runner metrics, pseudo gate, compare, ExperimentRun pins, persist | authored; **not-executed** |
| test_d19_foundry_benchmark_chain | hermetic E2E + LeaveOne*/Temporal benchmark-mode pins | authored; **not-executed** |
| Existing dataset/split/leakage/mlops9 suites | regression authority | not re-run (no toolchain) |

Re-run on a build host:

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=2 CTEST_PARALLEL_LEVEL=1
cmake --build <build> --target test_d19_dataset_foundry test_d19_benchmark test_d19_foundry_benchmark_chain -j2
ctest -R 'test_d19_' -j1 --output-on-failure
```
