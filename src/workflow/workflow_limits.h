// src/workflow/workflow_limits.h — shared workflow numeric limits.
#pragma once

//
// Dependency-free constants shared across workflow translation units
// (workflow_checkpoint.cpp, pipeline_run_coordinator.cpp and their standalone
// test targets). Kept out of workflow_checkpoint.h so consumers of one
// constant do not pull that header's serialization include cone.
//

#include <QtGlobal>

namespace sicnu::workflow {

/// Upper bound for whole-file checkpoint reads (#1056): a corrupt or hostile
/// checkpoint must fail typed, never allocate without limit. Checkpoint
/// documents are engine-written JSON; real payloads stay orders of magnitude
/// below this. Same value as the manifest / stage-ledger caps.
inline constexpr qint64 kMaxCheckpointReadBytes = 16ll * 1024ll * 1024ll;

} // namespace sicnu::workflow
