// src/workflow/workflow_limits.h — shared durability/input bounds (workflow kernel)
#pragma once

#include <QtGlobal>

namespace sicnu::workflow {

/// Upper bound for any persisted workflow document a reader may buffer
/// (Engine-2.0 checkpoints, D17 pipeline checkpoints, provenance records).
/// A checkpoint is a small JSON sidecar; a larger file is planted or corrupt
/// and must be refused BEFORE readAll() buffers it unbounded (mirrors
/// kMaxLedgerBytes / kMaxManifestBytes in the geospatial leaf layer).
inline constexpr qint64 kMaxCheckpointDocumentBytes = 16 * 1024 * 1024;

} // namespace sicnu::workflow
