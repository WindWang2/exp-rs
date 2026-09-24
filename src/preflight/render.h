// render.h — teaching/agent projections of a PreflightReport (RS14-02).
//
// One report, two read-only projections — a single truth source:
//   renderTeaching (sicnu.preflight.teaching/1): every finding quoted
//     verbatim (human explanation + evidence), next-step semantics split by
//     severity (blocks can never offer acknowledgement; require_ack names
//     the code to accept; acknowledged findings say so), and an explicit
//     all-clear skeleton on clean runs.
//   renderAgent (sicnu.preflight.agent/1): verdict + judgments +
//     required_actions ONLY. The agent surface exposes no repair, execution
//     or fabrication capability — actions name what the agent may do, never
//     something the projection does.
// Both projections are deterministic; canonicalProjectionJson pins bytes.

#pragma once

#include "preflight/report.h"

#include <json/json.h>

#include <string>

namespace sicnu::preflight {

Json::Value renderTeaching( const PreflightReport &report );

Json::Value renderAgent( const PreflightReport &report );

/// Byte-deterministic serialization for projections (same canonical writer
/// settings as the report itself).
std::string canonicalProjectionJson( const Json::Value &projection );

} // namespace sicnu::preflight
