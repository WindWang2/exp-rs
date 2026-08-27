// src/operators/framework/model_readiness.h
#pragma once

#include <string>

namespace sicnu::operators {

/**
 * Real availability state of a catalog model (manifest v2 runtime contract).
 *
 * The first four states are computed by ModelCatalog from the manifest and the
 * local file system alone (no inference backend knowledge required). The last
 * two are runtime-layer verdicts — ModelCatalog never sets them itself; the
 * model runtime evaluates them against the actual hardware/backends (see
 * operators/runtime/model_runtime.h).
 */
enum class ModelReadiness
{
  Ready,                 ///< Manifest parsed, artifact present, checksum verified
  MissingArtifact,       ///< Artifact path empty or the weight file does not exist
  InvalidManifest,       ///< Manifest unparseable or contract internally inconsistent
  ChecksumMismatch,      ///< Artifact present but its digest/size contradicts the manifest
  UnsupportedRuntime,    ///< Framework has no available provider in this build
  IncompatibleHardware   ///< GPU required, unavailable, and CPU fallback disabled
};

inline const char *modelReadinessName( ModelReadiness state )
{
  switch ( state )
  {
    case ModelReadiness::Ready: return "ready";
    case ModelReadiness::MissingArtifact: return "missing_artifact";
    case ModelReadiness::InvalidManifest: return "invalid_manifest";
    case ModelReadiness::ChecksumMismatch: return "checksum_mismatch";
    case ModelReadiness::UnsupportedRuntime: return "unsupported_runtime";
    case ModelReadiness::IncompatibleHardware: return "incompatible_hardware";
  }
  return "unknown";
}

} // namespace sicnu::operators
