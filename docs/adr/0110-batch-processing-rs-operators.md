# ADR 0110: Batch Processing Supports RS Operators

## Context

The DoD contract states every remote-sensing operation must stay callable from
batch processing, not only from its dedicated dialog. The batch dialog
(`app/dialogs/batch_processing_dialog.cpp`) enumerated algorithms solely from
`QgsProcessingRegistry`, so the native `rs:` operator family (qa_mask,
rx_anomaly, spectral_index, mnf, …) was invisible to it even though those
operators already carry full JSON schemas and are mirrored into the
`AtomicAlgorithmRegistry` for the CLI / MCP / Agent surfaces.

## Decision

- The batch dialog appends a second group of entries — **RS operators**
  (`… (RS)`), enumerated from `AtomicAlgorithmRegistry::listDescriptors()` and
  filtered by `isBatchableRsOperator()`: id prefix `rs:`, exactly one required
  raster/vector input (`findMainInputName`), an `output` raster port, and every
  other required parameter either defaulted or an output-role path (name
  starts with `output`). Multi-input operators (change_detection, apply_mask,
  image_fusion, …) and operators with required non-default parameters
  (spectral_resample `wavelengths`, band_math `expression`, …) are deliberately
  not offered — they cannot be parameterized per-file with defaults.
- Per-file execution was extracted into a public, UI-free seam
  `runBatchItem(id, input, output, errorMessage)`; `onRun()` drives it in a
  loop exactly like the QGIS path. The RS branch builds JSON via
  `buildRsParams()` (main input → file, defaults typed by `DataType`, output
  ports → output path) and runs the `RsOperatorAdapter::execute()` synchronously,
  consistent with the dialog's existing QGIS `alg->run()` behavior.
- `setInputFiles()` / `setOutputDir()` become programmatic setters (also used
  by tests); the status label is guarded against the null state during
  construction (the first `addItem` fires `currentIndexChanged` before the
  label exists — a latent null-deref hazard the RS branch exposed).

## Consequences

- Batch-parameterizable single-input RS operators (rx_anomaly, qa_mask,
  pca, mnf, atmospheric_correction, continuum_removal, spectral_index,
  endmember_extraction, …) now batch over many files with declared defaults;
  multi-input ones stay on their dedicated dialogs / DAGs.
- The seam is pinned by `test_batch_processing_dialog` (19 assertions):
  batchable operators appear and non-batchable ones do not; a real
  `rs:rx_anomaly` batch item produces its output file; unknown and
  multi-input ids fail cleanly with a message.
