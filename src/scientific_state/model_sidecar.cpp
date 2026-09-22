/***************************************************************************
  scientific_state/model_sidecar.cpp
  RS14-01 Scientific Data Passport — classifier model sidecar parser.

  Projection rules (see tests/test_scientific_state_provenance.cpp):
    version        — required, integral, 1 or 2 (the versions the pipeline
                     writes/reads); anything else is InvalidField.
    method         — required non-empty string → modelKind (verbatim).
    classes[]      — objects with an integral "id" → labels as decimal
                     strings, sorted and deduplicated.
    validation     — optional; "overallAccuracy" numeric → hasAccuracy.
    featureSchema  — optional; descriptor names joined in schema order
                     ("name1,name2") as the presence summary.
  Absence of an optional section is a v1 fact (accuracy simply absent), not
  an error; structural violations inside a present section are InvalidField.
 ***************************************************************************/

#include "scientific_state/model_sidecar.h"

#include <json/json.h>

#include <algorithm>

namespace sicnu::state
{

namespace
{

bool fail( AssetStateError &error, StateErrorCode code, const std::string &message )
{
    error.code = code;
    error.message = message;
    return false;
}

} // namespace

bool parseClassifierSidecarJson( const std::string &text, const std::string &sidecarPath,
                                 ModelSidecarFacts &out, AssetStateError &error )
{
    error = AssetStateError{};

    Json::Value root;
    Json::CharReaderBuilder builder;
    // Untrusted-input discipline: bound the recursion stack so depth bombs
    // fail as a parse error instead of exhausting the machine.
    builder[ "stackLimit" ] = 128;
    Json::CharReader *reader = builder.newCharReader();
    bool parsed = false;
    try
    {
        parsed = reader->parse( text.data(), text.data() + text.size(), &root, nullptr );
    }
    catch ( const Json::Exception & )
    {
        parsed = false;
    }
    delete reader;
    if ( !parsed )
        return fail( error, StateErrorCode::MalformedJson, "sidecar is not valid JSON" );
    if ( !root.isObject() )
        return fail( error, StateErrorCode::MalformedJson, "sidecar is not a JSON object" );

    // ---- version (required, supported) ----
    if ( !root.isMember( "version" ) || !root[ "version" ].isIntegral() )
        return fail( error, StateErrorCode::InvalidField,
                     "sidecar must declare an integral 'version'" );
    const Json::Int version = root[ "version" ].asInt();
    if ( version != 1 && version != 2 )
        return fail( error, StateErrorCode::InvalidField,
                     "unsupported sidecar version " + std::to_string( version ) );

    // ---- method (required, projected verbatim) ----
    if ( !root.isMember( "method" ) || !root[ "method" ].isString() ||
         root[ "method" ].asString().empty() )
        return fail( error, StateErrorCode::InvalidField,
                     "sidecar must declare a non-empty string 'method'" );

    ModelSidecarFacts facts;
    facts.modelKind = root[ "method" ].asString();
    facts.sidecarPath = sidecarPath;

    // ---- classes → labels (sorted, unique decimal strings) ----
    if ( root.isMember( "classes" ) )
    {
        const Json::Value &classes = root[ "classes" ];
        if ( !classes.isArray() )
            return fail( error, StateErrorCode::InvalidField,
                         "sidecar field 'classes' must be an array" );
        for ( const Json::Value &entry : classes )
        {
            if ( !entry.isObject() || !entry.isMember( "id" ) || !entry[ "id" ].isIntegral() )
                return fail( error, StateErrorCode::InvalidField,
                             "sidecar field 'classes[]' must be objects with an integral 'id'" );
            facts.labels.push_back( std::to_string( entry[ "id" ].asInt() ) );
        }
    }
    std::sort( facts.labels.begin(), facts.labels.end() );
    facts.labels.erase( std::unique( facts.labels.begin(), facts.labels.end() ),
                        facts.labels.end() );

    // ---- validation → accuracy (optional; absent in v1) ----
    if ( root.isMember( "validation" ) )
    {
        const Json::Value &validation = root[ "validation" ];
        if ( !validation.isObject() )
            return fail( error, StateErrorCode::InvalidField,
                         "sidecar field 'validation' must be an object" );
        if ( validation.isMember( "overallAccuracy" ) )
        {
            if ( !validation[ "overallAccuracy" ].isNumeric() )
                return fail( error, StateErrorCode::InvalidField,
                             "sidecar field 'validation.overallAccuracy' must be a number" );
            facts.hasAccuracy = true;
            facts.accuracy = validation[ "overallAccuracy" ].asDouble();
        }
    }

    // ---- featureSchema → presence summary (optional; absent in v1) ----
    if ( root.isMember( "featureSchema" ) )
    {
        const Json::Value &schema = root[ "featureSchema" ];
        if ( !schema.isObject() )
            return fail( error, StateErrorCode::InvalidField,
                         "sidecar field 'featureSchema' must be an object" );
        if ( schema.isMember( "features" ) )
        {
            const Json::Value &features = schema[ "features" ];
            if ( !features.isArray() )
                return fail( error, StateErrorCode::InvalidField,
                             "sidecar field 'featureSchema.features' must be an array" );
            std::string summary;
            for ( const Json::Value &entry : features )
            {
                if ( !entry.isObject() || !entry.isMember( "name" ) ||
                     !entry[ "name" ].isString() )
                    return fail( error, StateErrorCode::InvalidField,
                                 "sidecar field 'featureSchema.features[]' must be objects "
                                 "with a string 'name'" );
                if ( !summary.empty() )
                    summary += ",";
                summary += entry[ "name" ].asString();
            }
            facts.featureSchema = summary;
        }
    }

    out = std::move( facts );
    return true;
}

} // namespace sicnu::state
