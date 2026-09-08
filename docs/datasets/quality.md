# Quality & composition

`computeComposition()` (src/dataset/dataset_quality.h) folds flat rows into
by-class/sensor/region/season/modality/resolution/year distributions with
honest anomaly counts (unknown class, missing time, zero weight).
`imbalanceFindings()` flags dimensions whose max/min non-zero ratio crosses
a threshold - composition reporting for dataset builders, deliberately not
promoted to a general "fairness" system.

`labelQualityAudit()` runs the label QA checks: empty labels, unknown
classes, unparsable/multipart geometry, tiny polygons, out-of-raster
geometry, conflicting tip labels, duplicate annotations - typed findings
with severity. `recommendQualityLevel()` is the gate arithmetic:
errors -> Draft, warnings -> Valid, clean -> Certified. The human decision
stays with the caller.
