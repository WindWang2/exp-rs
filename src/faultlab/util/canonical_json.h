// util/canonical_json.h — deterministic canonical JSON serialization.
//
// Digesting a fixture (or a report) requires a byte-stable serialization:
// object keys sorted, array order preserved, numbers pinned to round-trip
// formats, no whitespace variance, no timestamps. jsoncpp's writers do not
// guarantee key order or float formatting, so the fault lab owns its own
// canonical form.
//
// Number contract (aligned with the other two canonical families —
// verification's roundSignificant and grader's canonicalNumber — on every
// point where the families meet):
//   * NaN serializes as the literal `NaN`: it is the framework's documented
//     no-data marker (fault_types.h), so grid digests must hash it — but
//     deterministically, with the sign bit normalized away.
//   * ±Infinity is REFUSED. It has no feature meaning in this framework
//     (noData is NaN; every transform operand is finite-gated) and cannot
//     be spelled as JSON at all: an unhashable value must not reach a
//     digest. jsoncpp parses "1e999" to +Inf with no parse error, so the
//     refusal lives here as the digest floor (the scenario loader refuses
//     non-finite operands at the door as well).
//   * -0.0 normalizes onto `0`, like both sibling canonicalizers.
//
// The walk is budgeted (depth/nodes/output): a programmatically constructed
// deep or huge Json::Value is a typed refusal, never a stack overflow. The
// value-returning forms spell that refusal as the EMPTY string (no legal
// value serializes to zero bytes); callers that need the reason use the
// try* forms.
#pragma once

#include <json/json.h>

#include <string>

namespace sicnu::faultlab::canonical
{

/// Canonical compact JSON text of a value; empty when the value is refused
/// (non-finite ±Infinity, or a budget overrun).
std::string toCanonicalString( const Json::Value &value );

/// Lowercase hex SHA-256 of the canonical serialization (64 chars); empty
/// when the value is refused — an unhashable value gets no digest.
std::string sha256HexOf( const Json::Value &value );

/// Typed form of toCanonicalString: fills @p out and returns true, or fills
/// @p error with the refusal reason and returns false (@p out untouched).
bool tryToCanonicalString( const Json::Value &value, std::string &out, std::string &error );

/// Typed form of sha256HexOf: fills @p out (64 hex chars) and returns true,
/// or fills @p error and returns false (@p out untouched).
bool trySha256HexOf( const Json::Value &value, std::string &out, std::string &error );

} // namespace sicnu::faultlab::canonical
