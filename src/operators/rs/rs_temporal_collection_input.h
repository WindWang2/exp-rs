// src/operators/rs/rs_temporal_collection_input.h
// Shared parameter parsing for every temporal operator: ONE canonical entry
// point that turns operator parameters into a validated TemporalCollection.
//
// Accepted inputs (either, not both required — "scenes" wins when present):
//   "collection": "<path to a temporal collection descriptor JSON>"
//   "scenes": [ {"path": "...", "time": "2025-04-03", "bands": {"nir": 4},
//                "quality_band": 9, "mask_band": 8}, ... ]
//   "scenes": ["path1.tif", "path2.tif"]          // bare-path shorthand
//   "times":  ["2025-01-01", "2025-02-01", ...]    // parallel to bare paths
//   "bands":  {"nir": 4, "red": 3}                 // global role overrides
//   "duplicate_policy": "keep_all" | "reject"
//
// Missing acquisition times stay visible (time.valid == false); preflight is
// the component that rejects them — never a silent guess.
#pragma once

#include "processing/algorithms/temporal/temporal_collection.h"

#include <json/json.h>

#include <QString>

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

} // namespace sicnu::operators::rs::temporal_input
