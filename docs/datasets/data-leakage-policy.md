# Data leakage policy

**Rule: no audit, no claim.** `LeakageAuditor::audit()` records exactly
which checks ran (`auditedChecks`); reports may only speak about those.
A clean report says "these checks found nothing", never "no leakage".

Checks (each typed in `LeakageKind`): exact_duplicate (content digest),
overlapping_patch (ground overlap fraction), same_parent_polygon,
same_source_object, same_source_scene, distance_below_threshold,
buffer_overlap, augmentation_parent_leakage, pseudo_label_parent_leakage,
temporal_future_leakage (train observed at/after test in the same series),
same_event_crossing, same_temporal_group_crossing, pre_post_pair_leakage.

Cross-split = different roles; for fold manifests = different folds, and
temporal_future_leakage is NOT claimed there (it is defined against final
roles; audit per materialization instead).

Scale: hash maps for identity keys, x-grid bucketing for spatial pairs -
roughly linear in samples (benchmarked at 100k, see performance.md).
Findings embed evidence (digests, distances, fold ids). Split manifests
embed the summary; the store keeps full reports.
