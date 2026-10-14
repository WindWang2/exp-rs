# R12-E Reproducers — minimal failing inputs per defect

All reproducers are Catch2 cases committed in this branch; commands assume:
`cd <worktree> && export QT_QPA_PLATFORM=offscreen CTEST_PARALLEL_LEVEL=1`

| ID | Repro (pre-fix RED) | Test host | Post-fix command |
|---|---|---|---|
| D1 | scenario file = 1200×'[' + 1200×']' → `loadFaultScenarioFile` threw `Json::Exception("Exceeded stackLimit in readValue().")`; trailing-garbage/oversized files accepted/slurp | tests/test_faultlab.cpp "fault lab: scenario file loader refuses hostile documents" | `ctest -R 'scenario file loader refuses hostile'` |
| D2 | `applyFault(wrong_scale_offset, {role:"red",gain:2.0,offset:"abc"})` silently coerced offset→0.0; `{metadata:1}` → pixel path | tests/test_faultlab_params_r8.cpp "wrong_scale_offset refuses mistyped optional params" | `ctest -R 'wrong_scale_offset refuses mistyped'` |
| D3 | `parseJsonBounded` 500-deep nesting bomb threw across `readCheckpoint`/`FsArtifactProbe::readJson`/sidecar "Never throws" contracts | tests/test_verify_adapters.cpp "a JSON nesting bomb is refused typed on every adapter surface" | `ctest -R 'nesting bomb is refused typed'` |
| D4 | passport JSON `bands[0].index=3000000000` → `assetStateFromJson` threw `Json::LogicError: LargestInt out of Int range` | tests/test_scientific_state_core.cpp "integral int fields outside int range…" + boundary case | `ctest -R 'integral int fields outside int range'` |
| D5 | facts `{"sample_count":"100"}` parsed as -1-as-absent; `-5` accepted | tests/test_suitability_adversarial.cpp "wrong-typed or negative facts counts…" | `ctest -R 'wrong-typed or negative facts counts'` |
| D6 | scene `{"gsd_m":"10"}` silently dropped → Unknown instead of refusal | tests/test_suitability_adversarial.cpp "wrong-typed scene candidate numerics…" | `ctest -R 'wrong-typed scene candidate numerics'` |
| D7 | 6-entry extends chain loaded child-first across 2 documents → mirror stayed healthy, merge truncated at 4 hops, `band_roles` vanished → bandRoleEvaluate pass | tests/test_preflight_provider.cpp "mirror refuses over-deep…" | `ctest -R 'mirror refuses over-deep'` |
| D8 | report with 15-significant-digit evidence double digested at precision 12 — unreproducible via RFC 8785 authority | tests/test_preflight_report_schema.cpp "report and request digests ride…" | `ctest -R 'digests ride'` |
| D9 | stored report verdict=ok + warn finding → agent can_proceed=true vs teaching all_clear=false | tests/test_preflight_report_schema.cpp "agent projection aligns…" | `ctest -R 'agent projection aligns'` |
| D10 | spec c0(pass)+c1(fail), nodeChecks={n1:[c0]} → report.status==Pass with counters.fail==1 | tests/test_verifier_adversarial_14.cpp G-14 | `ctest -R 'adversarial G-14'` |
| D11 | missing required evidence → budgetUsage.exceeded==true (no budget hit) | tests/test_verifier_adversarial_14.cpp G-15 | `ctest -R 'adversarial G-15'` |
| D12 | rubric stage orderedAfter itself / 2-cycle → validated and graded permissively | tests/test_grader_engine.cpp "rubric validation refuses self-referential or cyclic orderedAfter graphs" | `ctest -R 'refuses self-referential or cyclic'` |
| D13 | provenance record with empty `state` → builder attaches ExecutionFacts{status:""}; validator ok; own strict fromJson refuses the document | tests/test_explain_validator.cpp "empty status…typed issue"; tests/test_explain_builder.cpp "empty status is refused…round trip" | `ctest -R 'empty status'` |
| D14 | skipped-step trust note claimed authored skipConsequence IS the run outcome | tests/test_explain_builder.cpp "skipped-step trust note attributes…" | `ctest -R 'trust note attributes'` |
| D16 | four SHA-256 forks unpinned (drill: flipping one KAT hex char fails the gate, reverted) | tests/test_verify_chain_locale_matrix.cpp "the four sha256 forks agree byte-for-byte…"; tests/test_grader_schema.cpp streaming pin | `ctest -R 'sha256 forks agree|streaming pins update-after'` |
| D17 | E12 behavior benchmark: baseline passes; leakage/cloud-mask/scale faults localize to family observables; grader single-fault variants flip exact criteria with precise slugs; seed isolation | tests/test_fault_matrix.cpp + tests/test_grader_adapters.cpp "teaching benchmark…" ×6 | `ctest -R 'teaching benchmark'` |
