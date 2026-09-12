// src/operators/runtime/provenance_verify.h — Platform 9.0 consumer-side
// provenance verification. 8.0 shipped the `exp-rs-prov/1` WRITER with a
// publish ordering that can only ever leave a DETECTABLE absence (product
// without sidecar) after a crash — but nothing on the consumer side looked.
// This module is that consumer: one typed verdict for "can I trust this
// product's provenance", usable by CLI/MCP checks, the Experiment/MLOps
// evidence seam and tests. It never re-runs inference and never fabricates
// verdicts: an unreadable or absent sidecar is exactly as loud as a
// mismatched one.
#pragma once

#include <json/json.h>

#include <string>

namespace sicnu::operators::runtime {

/// What the consumer expects the product's provenance to say. Empty fields
/// are not checked (verify-what-you-know, never invent constraints).
struct ProvenanceExpectation
{
  std::string modelIdentityTag;  ///< "id@version" — exact match when set
  std::string modelContentDigest; ///< weights digest — exact match when set
  std::string backend;           ///< backend name — exact match when set
};

/// Typed verification outcome. `detail` always names the offending evidence.
struct ProvenanceVerdict
{
  enum class State
  {
    Ok,                 ///< sidecar present, well-formed, matches expectations and product
    ProductMissing,     ///< the output file itself does not exist
    MissingSidecar,     ///< product exists, sidecar absent (the 8.0 crash window)
    MalformedSidecar,   ///< sidecar present but not parseable JSON
    UnsupportedSchema,  ///< parseable but not an exp-rs-prov/1 document
    ModelMismatch,      ///< model identity/digest/backend contradicts the expectation
    GridMismatch,       ///< recorded output geometry contradicts the actual raster
    StaleProduct        ///< product was written AFTER the sidecar (sidecar stale)
  };

  State state = State::ProductMissing;
  std::string detail;      ///< human-readable evidence for the verdict
  Json::Value provenance;  ///< parsed sidecar document (when readable)
};

/// Verify the provenance of one published raster product
/// (`<outputPath>.prov.json` sidecar contract).
/// Reads: the product (existence, mtime, GDAL grid), the sidecar (JSON).
/// Never writes. Never throws.
ProvenanceVerdict verifyProductProvenance( const std::string &outputPath,
                                           const ProvenanceExpectation &expectation = {} );

/// Convenience: the sidecar path for a product path.
std::string provenanceSidecarPath( const std::string &outputPath );

} // namespace sicnu::operators::runtime
