// diagnostic_report.cpp — see diagnostic_report.h for the contract.
#include "diagnostic_report.h"

#include <cstdio>

namespace sicnu::runtime::observability::diagnostics
{
namespace
{
std::string jsonEscape( const std::string &text )
{
    std::string out;
    out.reserve( text.size() + 8 );
    for ( const char ch : text )
    {
        switch ( ch )
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ( static_cast<unsigned char>( ch ) < 0x20 )
            {
                char buf[8];
                std::snprintf( buf, sizeof( buf ), "\\u%04x", static_cast<unsigned char>( ch ) );
                out += buf;
            }
            else
            {
                out += ch;
            }
        }
    }
    return out;
}

void appendField( std::string &out, bool &first, const char *key, const std::string &value )
{
    if ( value.empty() )
        return;
    if ( !first )
        out += ",";
    first = false;
    out += "\"";
    out += key;
    out += "\":\"";
    out += jsonEscape( value );
    out += "\"";
}

void appendStringArray( std::string &out, bool &first, const char *key,
                        const std::vector<std::string> &items )
{
    if ( items.empty() )
        return;
    if ( !first )
        out += ",";
    first = false;
    out += "\"";
    out += key;
    out += "\":[";
    for ( size_t i = 0; i < items.size(); ++i )
    {
        if ( i )
            out += ",";
        out += "\"";
        out += jsonEscape( items[i] );
        out += "\"";
    }
    out += "]";
}
} // namespace

std::string DiagnosticReport::toJson() const
{
    bool first = true;
    std::string out = "{\"schema\":\"exp.diag.v1\"";
    first = false;
    appendField( out, first, "code", code );
    appendField( out, first, "component", component );
    appendField( out, first, "run", run );
    appendField( out, first, "task", task );
    appendField( out, first, "job", job );
    if ( !first )
        out += ",";
    first = false;
    out += "\"recoverability\":\"";
    out += recoverabilityName( recoverability );
    out += "\"";
    // suggestedAction is emitted even when empty only if a recoverability of
    // manual/transient implies guidance exists; keep the honest rule: emit
    // when non-empty.
    appendField( out, first, "suggested_action", suggestedAction );
    appendStringArray( out, first, "cause_chain", causeChain );
    appendStringArray( out, first, "artifacts", artifacts );
    out += "}";
    return out;
}

} // namespace sicnu::runtime::observability::diagnostics
