// src/operators/rs/rs_temporal_collection_input.h
// Shared parameter parsing for every temporal operator: ONE canonical entry
// point that turns operator parameters into a validated TemporalCollection.
//
// Accepted inputs ("collection" is authoritative when both are present):
//   "collection": workspace UUID or descriptor JSON path
//   "scenes": [ {"path": "...", "time": "2025-04-03", "bands": {"nir": 4},
//                "quality_band": 9, "mask_band": 8, "modality": "sar",
//                "polarizations": ["VV","VH"], "band_roles": ["vv","vh"], ...}, ... ]
//   "scenes": ["path1.tif", "path2.tif"]          // bare-path shorthand
//   "times":  ["2025-01-01", "2025-02-01", ...]    // parallel to bare paths
//   "bands":  {"nir": 4, "red": 3}                 // global role overrides
//   "duplicate_policy": "keep_all" | "reject"
//
// Missing acquisition times stay visible (time.valid == false); preflight is
// the component that rejects them — never a silent guess.
#pragma once

#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/algorithms/temporal/temporal_preflight.h"

#include <json/json.h>

#include <QString>

namespace sicnu::operators {
class RSOperatorContext;
}

namespace sicnu::operators::rs::temporal_input
{

/// Parses and assembles the collection from @a params. Throws RSOperatorError
/// on malformed input (wrong types, unreadable descriptor, non-existent paths).
temporal::TemporalCollection parseCollection( const Json::Value &params );

/// Parses the duplicate policy token.
temporal::DuplicatePolicy parseDuplicatePolicy( const Json::Value &params );

/// Everything an operator needs after the scientific gate passed. Band
/// numbers are resolved per scene through TemporalTileReader::bandForRole
/// (the reader owns the open dataset handles).
struct PreparedTemporalRun
{
  temporal::TemporalCollection collection;
  temporal::TemporalPreflightReport preflight;
};

/// Canonical pipeline prologue shared by every temporal operator:
/// parse collection → preflight (with the operator's required roles) →
/// resolve per-scene analysis bands. Throws RSOperatorError with the first
/// blocking preflight issue (plus counts) when the science gate fails.
///
/// @param requiredRoles      roles every scene must resolve (index operators).
/// @param analysisRole       role of the single analysis band ("" = none).
/// @param analysisBandOverride explicit "band" parameter value (0 = none).
PreparedTemporalRun prepareTemporalRun( const Json::Value &params, RSOperatorContext &context,
                                        const std::vector<QString> &requiredRoles,
                                        const QString &analysisRole, int analysisBandOverride );

/// #1167: optional per-scene provenance channel consumed by the downstream
/// statistics operators (rs:temporal_smooth / trend / sen_trend /
/// phenology). `provenance` is an array of raster paths, one per scene,
/// given in the collection's ACQUISITION-TIME-SORTED order (the order
/// TemporalCollection::scenes() reports). Pixel codes follow
/// temporal::SampleProvenance: 1 (observed) keeps the sample; 0/2
/// (unavailable/interpolated) EXCLUDES it from the statistics — synthetic
/// samples must not inflate n, tighten Student-t/Sen CIs, or enter the MK S
/// pairs. Grid identity with the scenes is enforced (typed refusal).
class ProvenanceChannel
{
public:
  /// Returns nullptr when the operator was given no `provenance` parameter;
  /// throws RSOperatorError on a malformed/mismatched declaration.
  static std::unique_ptr<ProvenanceChannel> parse( const Json::Value &params,
                                                   const temporal::TemporalCollection &collection,
                                                   int referenceWidth, int referenceHeight );
  ~ProvenanceChannel();

  /// Fills @a keep (w*h bytes) with 1 = keep, 0 = exclude for @a scene.
  bool readKeepMask( int sceneIndex, int x, int y, int w, int h, std::uint8_t *keep );

private:
  ProvenanceChannel() = default;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace sicnu::operators::rs::temporal_input
