// src/planner/sha256_util.h
#pragma once

//
// RS14-09 Scientific Task Planner — self-contained SHA-256 (FIPS 180-4).
//
// The repo's fingerprint convention (planFingerprint, workflowIrFingerprint,
// repair_plan fingerprint, preflight) is SHA-256 truncated to 16 lowercase
// hex chars over the compact canonical JSON serialization. Qt-linked layers
// use QCryptographicHash; this module is a Qt-free leaf, so it carries the
// standard algorithm instead of linking Qt for one digest — the same
// discipline (and precedent) as src/preflight/sha256.* and
// src/repair_planner/repair_sha256.*.
//
// Deterministic and endian-correct on little- and big-endian hosts.
//

#include <cstdint>
#include <string>

namespace sicnu::planner {

/// Lowercase hex SHA-256 of `data` (64 chars).
std::string sha256Hex( const std::string &data );

/// The repo fingerprint form: first 16 hex chars of sha256Hex (lowercase).
std::string fingerprint16( const std::string &data );

} // namespace sicnu::planner
