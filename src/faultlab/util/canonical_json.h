// util/canonical_json.h — deterministic canonical JSON serialization.
//
// Digesting a fixture (or a report) requires a byte-stable serialization:
// object keys sorted, array order preserved, numbers pinned to round-trip
// formats, no whitespace variance, no timestamps. jsoncpp's writers do not
// guarantee key order or float formatting, so the fault lab owns its own
// canonical form. NaN is serialized as the literal `NaN` (jsoncpp does the
// same): the canonical form is for digesting and evidence, not interchange.
#pragma once

#include <json/json.h>

#include <string>

namespace sicnu::faultlab::canonical
{

/// Canonical compact JSON text of a value.
std::string toCanonicalString( const Json::Value &value );

/// Lowercase hex SHA-256 of the canonical serialization (64 chars).
std::string sha256HexOf( const Json::Value &value );

} // namespace sicnu::faultlab::canonical
