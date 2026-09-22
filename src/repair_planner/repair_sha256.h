// src/repair_planner/repair_sha256.h
#pragma once

//
// RS14-03 Scientific Repair Planner — self-contained SHA-256 (FIPS 180-4).
//
// The repo's fingerprint convention (agent_plan planFingerprint,
// workflow_ir, …) is SHA-256 truncated to 16 lowercase hex chars over the
// compact canonical JSON serialization. The convention lives in Qt-linked
// layers via QCryptographicHash; this module is a zero-dependency leaf, so
// it carries the standard algorithm instead of linking Qt for one digest.
//
// Deterministic and endian-correct on little- and big-endian hosts.

#include <cstdint>
#include <string>

namespace sicnu::repair {

/// Lowercase hex SHA-256 of `data` (64 chars).
std::string sha256Hex( const std::string &data );

} // namespace sicnu::repair
