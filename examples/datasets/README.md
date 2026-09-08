# Foundation 5.0 end-to-end examples

The four goal examples live as executable tests in
`tests/test_dataset_e2e_examples.cpp` (small data, whole chain):

- **Example A — optical classification**: asset → dataset version → patches
  → grouped spatial split → experiment run → metrics → reproduction bundle.
- **Example B — SAR pair change detection**: pre/post pair samples; the test
  demonstrates that an ungrouped split produces typed `pre_post_pair_leakage`
  findings while a grouped-by-event split audits clean.
- **Example C — optical+SAR time series**: temporal sample with a KNOWN
  missing observation + multimodal member composition with explicit
  per-modality missing policies.
- **Example D — segmentation**: polygon label samples, annotation with
  schema binding, label QA gate, known-answer IoU/accuracy.

Run: `ctest -R test_dataset_e2e_examples` (or the executable directly).
