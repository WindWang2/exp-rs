# ADR 0061: Consolidate Classification Operator Helper Duplications; Replace the KMeans Magic String with a Backend Virtual

## Status
Accepted

## Context
Backend construction existed in four copies (three byte-identical `makeBackend` bodies in the operators plus the pipeline's predict-only "bayes" sniff); the `(classId * 47) % 360` color formula and the `std::mt19937(42)` subsampling policy were re-implemented per adapter; per-band NoData discovery was duplicated between training extraction and the pipeline. The pipeline branched on `config.methodName == "KMeans"` to gate the Hungarian cluster→class remap, forcing the K-Means operator to pass a lowercase "kmeans" workaround.

## Decision
1. **`RsClassifierBackendFactory`** (analysis layer, beside `RsClassifierBackend`): one `create(methodName)` mapping — case-insensitive "bayes" → NormalBayes, "kmeans" → K-Means, fallback SVM — plus `createKMeans(k)`; all three operators and the pipeline predict-only path construct here.
2. **`rs_classification_utils.h`** owns `rsSynthesizedClassColor` (exact formula) and `rsShuffleAndKeep` (the mt19937(42) subsample policy, shared by training extraction and the K-Means operator).
3. **`rsCollectBandNodata`** (beside `RsPixelIgnoreOptions`) owns per-band GDAL NoData discovery (training extraction + pipeline tile path).
4. **`RsClassifierBackend::needsLabelRemap()`** virtual — K-Means returns true only when fitted with real (non-zero) labels; the pipeline gates the Hungarian remap on it: the "KMeans" string branches and the lowercase workaround are deleted.
5. **New remap test** (permuted labels 5/9, lowercase "kmeans" methodName) asserts predictions map to the training labels in both accuracy and the written class map.

## Consequences
- One construction path, color formula, sampling policy, NoData discovery.
- Remap semantics observably unchanged (identity when no table; the operator's all-zero dummy trainY keeps raw 1..K cluster ids).
- "kmeans" strings now construct K-Means (was the SVM fallback) — reachable only via sidecar predict-only, which still fails cleanly (K-Means has no load()).
- Dead `canonicalMethod` / `readLegacyMethodFromMeta` helpers in the supervised operator deleted.
