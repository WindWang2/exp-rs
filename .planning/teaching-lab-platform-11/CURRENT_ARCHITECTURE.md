# CURRENT_ARCHITECTURE — lab/teaching authority & seam map (Phase 0)

See BASELINE.md "Architecture authorities". Summary:

- Grading: OutputVerifier::gradeArtifact (ADR 0150) — single scoring authority.
- Batch: LabBatchRunner (D7) — streaming CSV over the grade seam.
- Report: LabReportBuilder → sicnu.labreport.v1 (projection of recorded truth;
  ExperimentStore + RSOperationLogger + ReplayReadiness + deepRedactSecretKeys).
- Copilot: labAsk/labReference + harness_actions role gate + LabSpecCatalog.
- Offline: sicnu::data::offline gate (3 enforcement depths) + OFFLINE_BUNDLE contract.
- LabSpec: data/labs/*.lab.json|.labspec.json; loader src/app/widgets/lab_spec_loader.h
  (GUI, D18); LabSpecCatalog (agent); drift guards tests/test_labspec.cpp.
- Registries: ProcessingRegistry/AtomicAlgorithmRegistry (operator ids) — only
  authority for operator_id validation.
