// grader_json.h — deterministic JSON helpers for the grader leaf library.
//
// The grader's reports must be byte-reproducible: double-grading the same
// (rubric, evidence) pair produces identical bytes. jsoncpp's writer is
// deterministic for sorted members but we need one canonical form owned by
// this module (house rule: ONE serializer per doctrine — the experiment side
// has canonicalizeJsonRfc8785, the Qt side; this is the jsoncpp-side leaf
// form: RFC 8785-shaped — object members sorted by code unit, shortest
// round-trip numbers, no insignificant whitespace — without importing Qt).
//
// Non-finite numbers (NaN/±Inf) are refused everywhere: a report that cannot
// be serialized deterministically must not exist.
#pragma once

#include "grader_error.h"

#include <json/json.h>

#include <optional>
#include <string>

namespace sicnu::grader {

/// Canonical serialization: sorted object members, shortest round-trip
/// number form, compact separators. Fails (nullopt + error) on non-finite
/// numbers.
std::optional<std::string> canonicalizeJson( const Json::Value &value, GraderError &error );

/// Strict parse: rejects trailing garbage; comments are refused (grading
/// documents are produced by tools, not edited prose).
std::optional<Json::Value> parseJsonStrict( const std::string &text, GraderError &error );

/// Shortest decimal form that round-trips back to the same double
/// ("%.15g" → "%.16g" → "%.17g" probe). Integral values within int64 range
/// print without a decimal point. -0.0 normalizes to 0. Non-finite input is
/// a GraderErrorCode::Internal error (callers must never store non-finite
/// numbers in grading documents).
std::optional<std::string> canonicalNumber( double value, GraderError &error );

} // namespace sicnu::grader
