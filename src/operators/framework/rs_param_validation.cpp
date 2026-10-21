/***************************************************************************
 * rs_param_validation.cpp — see rs_param_validation.h for the contract.
 ***************************************************************************/
#include "rs_param_validation.h"

namespace sicnu::operators
{

// #1578: setRange declarations used to be display-only metadata — 72
// declarations across the operator families were never checked at runtime,
// so an out-of-range knob reached the algorithm (or an otbcli subprocess)
// as an unbounded value instead of a typed InvalidParameter refusal.
void validateDeclaredRanges(const Json::Value &schemaRoot, const Json::Value &params)
{
    if ( !params.isObject() )
        return;
    const Json::Value &properties = schemaRoot["properties"];
    if ( !properties.isObject() )
        return;
    for ( const std::string &name : params.getMemberNames() )
    {
        if ( !properties.isMember( name ) )
            continue;
        const Json::Value &declaration = properties[name];
        if ( !declaration.isMember( "minimum" ) || !declaration.isMember( "maximum" ) )
            continue;
        const Json::Value &minimum = declaration["minimum"];
        const Json::Value &maximum = declaration["maximum"];
        // A hand-written non-numeric bound is a schema defect, not a value
        // refusal — skip rather than letting asDouble() escape as a
        // Json::LogicError from this gate.
        if ( !minimum.isNumeric() || !maximum.isNumeric() )
            continue;
        const Json::Value &value = params[name];
        if ( !value.isNumeric() )
            continue;
        const double numeric = value.asDouble();
        if ( numeric < minimum.asDouble() || numeric > maximum.asDouble() )
        {
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "parameter '" + name + "' value " + std::to_string( numeric ) +
                                       " is outside the declared [" + std::to_string( minimum.asDouble() ) +
                                       "," + std::to_string( maximum.asDouble() ) + "] range" );
        }
    }
}

} // namespace sicnu::operators
