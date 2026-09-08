# ADR 0136: Probe Contract & Format Capability Negotiation (5.0)

- Status: Accepted (Remote Sensing I/O Foundation 5.0 goal)
- Context: format recognition was scattered — extension checks in the data
  providers, `detectProductKind` in the product adapters, `inspectAny`'s
  raster→vector→multidim try-chain, "ends with .tif = COG" style guesses in
  UI code. Nothing stated what a format *can do* beyond
  `FormatProfile`'s certification flags, so upper layers guessed at windowed
  access, subdatasets, masks or overview availability.
- Decision:
  1. **`probe.h`**: one `probeResource(path, ProbeOptions)` pipeline:
     `ResourceUri classify → lightweight signature read (bounded ≤ 64 KiB,
     no full-file scan) → GDAL driver identify → product adapter detect →
     metadata inspect (lazy) → ProbeResult`. Each stage is skippable and the
     result records which stage decided. A wrong extension never wins against
     content: signature/GDAL identification outrank the file name.
  2. `ProbeResult` carries `FormatDescriptor` (profile id, driver(s),
     certification level from `FormatRegistry`) and `ProductDescriptor`
     (product kind/family, confidence) plus per-stage diagnostics. Probing is
     read-only, cheap and quiet (scoped CPL error silencing).
  3. **Capability model**: `FormatProfile` gains explicit capability flags —
     windowRead, blockRead, randomAccess, multiband, multidim, subdataset,
     georeferencing, crs, noData, mask, overviews, remoteRange, streaming —
     and `capabilitiesFor(path)` resolves profile × runtime driver presence ×
     dataset introspection. "Driver exists" and "this dataset supports it" are
     distinct answers; capability queries never lie about the loaded GDAL.
  4. **Negotiation**: conversion/export paths compare source capability against
     target capability and produce a typed loss report (CRS/NoData/scale-offset/
     band metadata/timestamps/field width/64-bit values). Lossless-when-declared
     requires the report to be empty or explicitly acknowledged.
- Consequences: one bounded, cancellable probe answers "what is this and what
  can I do with it"; COG detection is structural (`validateCog`), not
  name-based; import dialogs and CLI/Pi tools consume one contract instead of
  four heuristics.
