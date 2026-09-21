// src/agentbench/json_writer.cpp
#include "json_writer.h"

#include <cmath>
#include <sstream>

namespace sicnu::agentbench
{
namespace
{

// Json::Value stores object members in a std::map keyed by name, so member
// iteration is already lexicographically sorted; determinism therefore only
// requires fixed separators and fixed number formatting.

void writeString( std::ostream &out, const std::string &text )
{
	out << '"';
	for ( char c : text )
	{
		switch ( c )
		{
			case '"': out << "\\\""; break;
			case '\\': out << "\\\\"; break;
			case '\b': out << "\\b"; break;
			case '\f': out << "\\f"; break;
			case '\n': out << "\\n"; break;
			case '\r': out << "\\r"; break;
			case '\t': out << "\\t"; break;
			default:
				if ( static_cast<unsigned char>( c ) < 0x20 )
				{
					static const char *kHex = "0123456789abcdef";
					out << "\\u00" << kHex[( c >> 4 ) & 0xF] << kHex[c & 0xF];
				}
				else
				{
					out << c;
				}
		}
	}
	out << '"';
}

void writeValue( std::ostream &out, const Json::Value &value )
{
	switch ( value.type() )
	{
		case Json::nullValue:
			out << "null";
			break;
		case Json::intValue:
			out << value.asInt64();
			break;
		case Json::uintValue:
			out << value.asUInt64();
			break;
		case Json::realValue:
		{
			const double number = value.asDouble();
			if ( !std::isfinite( number ) )
			{
				out << "null";
				break;
			}
			// 17 significant digits round-trip every double; fixed locale
			// independent formatting keeps bytes stable across hosts.
			char buffer[40];
			std::snprintf( buffer, sizeof( buffer ), "%.17g", number );
			out << buffer;
			break;
		}
		case Json::stringValue:
			writeString( out, value.asString() );
			break;
		case Json::booleanValue:
			out << ( value.asBool() ? "true" : "false" );
			break;
		case Json::arrayValue:
		{
			out << '[';
			for ( Json::ArrayIndex i = 0; i < value.size(); ++i )
			{
				if ( i != 0 )
					out << ',';
				writeValue( out, value[i] );
			}
			out << ']';
			break;
		}
		case Json::objectValue:
		{
			out << '{';
			bool first = true;
			for ( const auto &name : value.getMemberNames() )
			{
				if ( !first )
					out << ',';
				first = false;
				writeString( out, name );
				out << ':';
				writeValue( out, value[name] );
			}
			out << '}';
			break;
		}
		default:
			out << "null";
			break;
	}
}

} // namespace

std::string deterministicSerialize( const Json::Value &value )
{
	std::ostringstream out;
	writeValue( out, value );
	return out.str();
}

} // namespace sicnu::agentbench
