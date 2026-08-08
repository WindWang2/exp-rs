# ADR 0118: Batch-Processing QGIS Algorithm Parameters

## Context

The batch dialog's parameter overrides (ADR 0113) covered RS operators only;
QGIS provider algorithms still ran with bare INPUT/OUTPUT substitution and no
way to set e.g. a reprojection target CRS or a resampling method once for an
entire file list.

## Decision

- `runBatchItem`'s override argument changed from `QJsonObject` to
  `QVariantMap` (the natural container for QGIS parameter values); the RS
  branch converts it to the operator JSON (bool/double/int/string) with the
  same main-input/output guards, and the QGIS branch merges it into the
  `QVariantMap` parameters with INPUT/INPUT_LAYER/OUTPUT/OUTPUT_LAYER pinned by
  the batch item.
- Selecting a QGIS algorithm now rebuilds the shared "参数覆盖" section through
  the QGIS parameter-widget registry (`createParameterWidgetWrapper` +
  `createWrappedWidget`, the same seam `SicnuAlgorithmDialog` uses), excluding
  INPUT/INPUT_LAYER/OUTPUT/OUTPUT_LAYER. `collectParamOverrides()` reads the
  wrappers' `parameterValue()`; for RS operators it keeps reading the typed
  editors. Both paths share `clearParamForm()`.
- The QGIS wrappers live on the dialog for their lifetime and are deleted on
  rebuild/close.

## Consequences

- Batch workflows can now tune QGIS provider parameters (CRS, resampling,
  nodata, …) for a whole file list, closing the last UI/backend alignment gap
  in the batch dialog. The Processing Registry remains the execution seam; the
  RS path is unchanged in behavior.
- Pinned by `test_batch_processing_dialog`: reprojectlayer's TARGET_CRS
  wrapper appears and its value is collected, while INPUT/OUTPUT are not
  offered; the existing RS override cases (form defaults, edit reflection,
  input-hijack rejection, generic-bitmask run) still pass on the new
  QVariantMap seam.
