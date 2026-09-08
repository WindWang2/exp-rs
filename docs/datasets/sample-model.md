# Sample model

`SampleRecord` (src/dataset/sample.h) = envelope + typed payload.

Envelope: `SampleId`, dataset version, kind, `groupId` (split/leakage
group), weight (>0), observation time, CRS, quality prior, source asset
pins, provenance.

Payloads (`std::variant`, kind must match - validated):
point, pixel, **window** (half-open `[x,x+w)`), **patch** (window + border
policy + NoData policy + valid fraction + generator-config hash + validity
flag), polygon (WKT), object (segment ref), pair (pre/post member refs),
temporal (observations with explicit `missing` flags + target time + mask),
multimodal (modality members with required/optional + missing policy).

Spatial contract: windows are half-open; ground footprints derive from
north-up geotransforms only (`dataset.transform_rotated` otherwise); the
layer NEVER resamples or reprojects - harmonize via grid-contract adapters
upstream. `gridWindows()` enumerates deterministic grids (overhang kept;
BorderPolicy decides).

Patches are SPECS, not pixel buffers: rasters are read by callers through
bounded window IO. `PatchGenerator` records the config hash on every patch
(`PatchGenerator::configHash`) so a patch proves its own origin.
