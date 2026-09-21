// src/agentbench/json_writer.h
#pragma once

//
// Deterministic JSON serialization for agentbench digests and reports.
//
// Sorted object keys, compact separators, stable number formatting. This is
// NOT an RFC 8785 canonicalization and must never be used for run identity:
// identity fingerprints stay owned by the Qt-side experiment stack
// (canonicalizeJsonRfc8785). These bytes serve replay determinism, suite
// pinning, and report digests inside agentbench only.
//

#include <json/json.h>
#include <string>

namespace sicnu::agentbench
{

/// Serializes `value` to deterministic bytes (sorted object keys, no
/// whitespace). Array order is preserved. Non-finite doubles serialize as
/// `null` — agentbench data must not carry NaN/inf; graders treat
/// non-finite metric values as "unavailable" before serialization.
std::string deterministicSerialize( const Json::Value &value );

} // namespace sicnu::agentbench
