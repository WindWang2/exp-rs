// src/agentbench/json_numbers.h
#pragma once

//
// Shared JSON helpers for the agentbench lane.
//

#include <json/json.h>

#include <string>

namespace sicnu::agentbench
{

/// Resolves a dotted path ("stats.mean") inside a JSON object; nullptr when
/// any segment is missing or the walk leaves object territory.
inline const Json::Value *resolveDottedPath( const Json::Value &fields, const std::string &dotted )
{
	const Json::Value *current = &fields;
	size_t start = 0;
	while ( start <= dotted.size() )
	{
		const size_t dot = dotted.find( '.', start );
		const std::string segment = dot == std::string::npos ? dotted.substr( start ) : dotted.substr( start, dot - start );
		if ( segment.empty() || !current->isObject() || !current->isMember( segment ) )
			return nullptr;
		current = &( *current )[segment];
		if ( dot == std::string::npos )
			break;
		start = dot + 1;
	}
	return current;
}

/// True when the value is a signed 64-bit integer within [lo, hi]. Gates on
/// isInt64() so out-of-int64-range uint values are rejected instead of
/// throwing from asInt()/asInt64().
inline bool isIntInRange( const Json::Value &value, long long lo, long long hi )
{
	return value.isInt64() && value.asInt64() >= lo && value.asInt64() <= hi;
}

} // namespace sicnu::agentbench
