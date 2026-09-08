# ADR 0140: Export / Conversion Policy & Fidelity Negotiation (5.0)

- Status: Accepted (Remote Sensing I/O Foundation 5.0 goal)
- Context: raster/vector writes existed in three generations (the foundation's
  atomic writers, `io:translate`/`convert_format` operators, OutputCommitter's
  task-output pipeline) with no shared *request* schema and no systematic
  loss reporting. Silent CRS/NoData/scale-offset/band-metadata loss on format
  conversion was possible (and is a 5.0 hard-gate violation).
- Decision:
  1. **`convert/convert_service.h`**: one `ConversionRequest` schema (source,
     target format/profile, CRS behavior, datatype, resampling, NoData policy,
     compression/creation options, overwrite, metadata propagation toggles) and
     one `ConversionReport` (what was preserved, what degraded, why).
     Raster/vector paths share the schema; STAC/metadata export is a profile
     of the same service.
  2. **Negotiation before write**: the service runs the ADR 0136 capability
     comparison and refuses — with a typed `FidelityLoss` error — when the
     request would silently drop declared metadata (float→narrow integer
     without explicit opt-in, CRS drop, NoData unrepresentable, band-role
     loss). An explicit `acknowledgeLoss` list converts refusals into
     recorded warnings.
  3. **Atomicity is not optional**: every export stages through the atomic
     writer path (temp → fsync → validate → publish). Overwrite requires an
     explicit flag; failure leaves the previous target byte-identical.
  4. **Defaults are per-profile, documented**: COG presets carry documented
     tile size/compression/predictor/overview policy per data class
     (lossless-scientific, visualization, categorical, continuous-float, SAR);
     plain GTiff keeps the house LZW-tiled default. No single hard-coded
     creation-option set claims to fit all data.
  5. **Round-trip proof**: conversion tests assert reopen-and-compare
     (CRS/size/bands/NoData/scale-offset/roles) — an export that cannot be
     re-read with declared metadata intact fails the test, not the user.
- Consequences: one service backs the `io:*` operators, the CLI convert
  command and future UI export dialogs; "export produced corrupt output" and
  "silent metadata loss" become test-enforced impossibilities on certified
  paths.
