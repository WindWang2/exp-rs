// src/agentbench/wire_parse.h — one copy of the agentbench wire helpers.
//
// EnumTable / lookupEnum / enumToWire and the evidence-kind / verdict
// predicates used to be private copies in trace.cpp, case_schema.cpp,
// fake_agent.cpp and suite.cpp. A new wire value had to be added in every
// copy or the schemas drifted silently (#1630). The per-file TABLES stay
// next to the types they describe; only the lookup itself is shared.
#pragma once

#include <json/json.h>

#include <string>

namespace sicnu::agentbench::wire
{

struct EnumTable
{
	const char *wire;
	int value;
};

inline bool lookupEnum( const EnumTable *table, size_t size, const std::string &wire, int &out )
{
	for ( size_t i = 0; i < size; ++i )
	{
		if ( wire == table[i].wire )
		{
			out = table[i].value;
			return true;
		}
	}
	return false;
}

inline const char *enumToWire( const EnumTable *table, size_t size, int value )
{
	for ( size_t i = 0; i < size; ++i )
	{
		if ( table[i].value == value )
			return table[i].wire;
	}
	return "";
}

inline bool isNonEmptyString( const Json::Value &value )
{
	return value.isString() && !value.asString().empty();
}

inline constexpr const char *kEvidenceKinds[] = { "raster", "vector", "map", "table", "report" };
inline constexpr const char *kVerdicts[] = { "PASS", "PASS_WITH_WARNINGS", "FAIL" };

inline bool isKnownEvidenceKind( const std::string &kind )
{
	for ( const char *candidate : kEvidenceKinds )
		if ( kind == candidate )
			return true;
	return false;
}

inline bool isKnownVerdict( const std::string &verdict )
{
	for ( const char *candidate : kVerdicts )
		if ( verdict == candidate )
			return true;
	return false;
}

} // namespace sicnu::agentbench::wire
