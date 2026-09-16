# TEST_MATRIX — capability → independent oracle → command → exit → evidence

| capability | oracle independence | command | exit | evidence |
|---|---|---|---|---|
| pack contract load/verify | test computes digests itself; corrupt-byte negative | ctest -R test_lab_data_pack | 0 | (pending run) |
| committed pack drift | packs == regeneration; fixture checksums match tree | ctest -R test_lab_data_pack (drift cases) | 0 | (pending run) |
| grader 2.0 kernels | in-test arithmetic over raw-GDAL scenes | ctest -R test_lab_grader_kernels | 0 | (pending run) |
| corpus known-answer (incl. labs 8-11) | reference=100; wrong bands declared per entry | ctest -R test_lab_grading | 0 | (pending run) |
| batch v2 identity/roster/caps/cancel/summaries | fixed bytes; deterministic JSON rerun | ctest -R test_lab_batch_v2 | 0 | (pending run) |
| report CLI + recorded grade + redaction | planted secret greps; transcript digest declared in test | ctest -R test_lab_report_cli | 0 | (pending run) |
| injection/leak corpus | corpus vs fixture spec solution values | ctest -R test_harness_lab_injection | 0 | (pending run) |
| self-check diagnostics | corrupted fixture flips overall; deterministic doc | ctest -R test_lab_self_check | 0 | (pending run) |
| offline classroom chain | process gate + GDAL deny; whole chain runs | ctest -R test_lab_offline_e2e | 0 | (pending run) |
| classroom scale | 100 deterministic; 1000 opt-in evidence run | ctest -R test_lab_scale; SICNU_LAB_SCALE_1000=1 ctest -R test_lab_scale | 0 | (pending run) |
| baseline lab suites (regression) | existing suites stay green | ctest -R "test_lab_batch$|test_lab_grading$|test_lab_report$|test_labspec$|test_harness_lab_evals$" | 0 | (pending run) |
