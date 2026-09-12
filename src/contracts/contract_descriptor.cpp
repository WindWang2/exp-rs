/***************************************************************************
 * contract_descriptor.cpp — canonical typed contract descriptor (M1)
 ***************************************************************************/
#include "contract_descriptor.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace sicnu::contracts {

namespace {

const char *const kCanonicalSchema = "exp.contract.descriptor.v1";

std::string jsonToString( const Json::Value &v )
{
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString( b, v );
}

bool parseParam( const std::string &key, const Json::Value &p,
                 ContractParam &out, std::string &error )
{
    if ( !p.isObject() )
    {
        error = "param '" + key + "' is not an object";
        return false;
    }
    // The object key and the embedded "name" field are two spellings of the
    // same fact — a mismatch is a contract bug (consumers index by either).
    if ( p.isMember( "name" ) && p["name"].asString() != key )
    {
        error = "param key '" + key + "' != declared name '" +
                p["name"].asString() + "'";
        return false;
    }
    out.name = key;
    out.type = p.get( "type", "object" ).asString();
    out.description = p.get( "description", "" ).asString();
    if ( p.isMember( "default" ) && !p["default"].isNull() )
    {
        out.hasDefault = true;
        out.defaultValue = jsonToString( p["default"] );
    }
    if ( p.isMember( "enum" ) && p["enum"].isArray() )
        for ( const auto &e : p["enum"] )
            out.enumValues.push_back( e.asString() );
    if ( p.isMember( "minimum" ) || p.isMember( "maximum" ) )
    {
        out.hasRange = true;
        out.minimum = p.get( "minimum", 0.0 ).asDouble();
        out.maximum = p.get( "maximum", 0.0 ).asDouble();
    }
    return true;
}

Json::Value paramToJson( const ContractParam &p )
{
    Json::Value o( Json::objectValue );
    o["name"] = p.name;
    o["type"] = p.type;
    if ( !p.description.empty() )
        o["description"] = p.description;
    if ( p.required )
        o["required"] = true;
    if ( p.hasDefault )
    {
        Json::CharReaderBuilder rb;
        std::string errs;
        Json::Value dv;
        std::istringstream is( p.defaultValue );
        if ( Json::parseFromStream( rb, is, &dv, &errs ) )
            o["default"] = dv;
        else
            o["default"] = p.defaultValue;
    }
    if ( !p.enumValues.empty() )
    {
        Json::Value arr( Json::arrayValue );
        for ( const auto &e : p.enumValues )
            arr.append( e );
        o["enum"] = arr;
    }
    if ( p.hasRange )
    {
        o["minimum"] = p.minimum;
        o["maximum"] = p.maximum;
    }
    return o;
}

void sortParams( std::vector<ContractParam> &params )
{
    std::sort( params.begin(), params.end(),
               []( const ContractParam &a, const ContractParam &b ) {
                   return a.name < b.name;
               } );
}

} // namespace

bool ContractDescriptor::fromOperatorSchema( const std::string &id,
                                             const Json::Value &schemaRoot,
                                             ContractDescriptor &out,
                                             std::string &error )
{
    out = ContractDescriptor{};
    out.id = id;
    if ( !schemaRoot.isObject() )
    {
        error = "schema root is not an object";
        return false;
    }
    out.title = schemaRoot.get( "title", "" ).asString();
    out.description = schemaRoot.get( "description", "" ).asString();
    out.determinismGrade = schemaRoot.get( "determinismGrade", "" ).asString();

    if ( !schemaRoot.isMember( "properties" ) ||
         !schemaRoot["properties"].isObject() )
    {
        error = "schema root has no properties object";
        return false;
    }
    for ( const auto &key : schemaRoot["properties"].getMemberNames() )
    {
        ContractParam p;
        if ( !parseParam( key, schemaRoot["properties"][key], p, error ) )
            return false;
        out.params.push_back( std::move( p ) );
    }
    if ( schemaRoot.isMember( "required" ) && schemaRoot["required"].isArray() )
        for ( const auto &r : schemaRoot["required"] )
            out.requiredParams.push_back( r.asString() );

    if ( schemaRoot.isMember( "outputs" ) && schemaRoot["outputs"].isObject() )
        for ( const auto &key : schemaRoot["outputs"].getMemberNames() )
        {
            ContractParam p;
            if ( !parseParam( key, schemaRoot["outputs"][key], p, error ) )
                return false;
            out.outputs.push_back( std::move( p ) );
        }

    // Everything outside the canonical set is preserved verbatim.
    static const std::set<std::string> kCanonicalKeys = {
        "$schema",    "title", "description", "type",
        "properties", "required", "outputs",  "determinismGrade",
    };
    for ( const auto &key : schemaRoot.getMemberNames() )
        if ( !kCanonicalKeys.count( key ) )
            out.extensions[key] = schemaRoot[key];

    // Required must reference declared params (dangling = bug).
    for ( const auto &r : out.requiredParams )
    {
        const bool declared = std::any_of(
            out.params.begin(), out.params.end(),
            [&]( const ContractParam &p ) { return p.name == r; } );
        if ( !declared )
        {
            error = "required param '" + r + "' not declared in properties";
            return false;
        }
    }
    // Params listed in required carry the flag (projection consistency).
    for ( auto &p : out.params )
        p.required =
            std::any_of( out.requiredParams.begin(),
                         out.requiredParams.end(),
                         [&]( const std::string &r ) { return r == p.name; } );

    sortParams( out.params );
    sortParams( out.outputs );
    std::sort( out.requiredParams.begin(), out.requiredParams.end() );
    out.requiredParams.erase(
        std::unique( out.requiredParams.begin(), out.requiredParams.end() ),
        out.requiredParams.end() );
    return true;
}

Json::Value ContractDescriptor::toJson() const
{
    Json::Value root( Json::objectValue );
    root["schema"] = kCanonicalSchema;
    root["id"] = id;
    root["kind"] = kind;
    root["title"] = title;
    if ( !description.empty() )
        root["description"] = description;
    if ( !determinismGrade.empty() )
        root["determinismGrade"] = determinismGrade;

    Json::Value paramsJson( Json::arrayValue );
    for ( const auto &p : params )
        paramsJson.append( paramToJson( p ) );
    root["params"] = paramsJson;

    Json::Value outputsJson( Json::arrayValue );
    for ( const auto &p : outputs )
        outputsJson.append( paramToJson( p ) );
    root["outputs"] = outputsJson;

    Json::Value req( Json::arrayValue );
    for ( const auto &r : requiredParams )
        req.append( r );
    root["required"] = req;

    if ( extensions.isObject() && !extensions.empty() )
        root["extensions"] = extensions;
    return root;
}

bool ContractDescriptor::fromCanonicalJson( const Json::Value &json,
                                            ContractDescriptor &out,
                                            std::string &error )
{
    out = ContractDescriptor{};
    if ( !json.isObject() || !json.isMember( "schema" ) ||
         json["schema"].asString() != kCanonicalSchema )
    {
        error = "not an " + std::string( kCanonicalSchema ) + " document";
        return false;
    }
    out.id = json.get( "id", "" ).asString();
    out.kind = json.get( "kind", "operator" ).asString();
    out.title = json.get( "title", "" ).asString();
    out.description = json.get( "description", "" ).asString();
    out.determinismGrade = json.get( "determinismGrade", "" ).asString();

    if ( json.isMember( "params" ) && json["params"].isArray() )
        for ( const auto &p : json["params"] )
        {
            ContractParam cp;
            cp.name = p.get( "name", "" ).asString();
            cp.type = p.get( "type", "" ).asString();
            cp.description = p.get( "description", "" ).asString();
            cp.required = p.get( "required", false ).asBool();
            if ( p.isMember( "default" ) )
            {
                cp.hasDefault = true;
                cp.defaultValue = jsonToString( p["default"] );
            }
            if ( p.isMember( "enum" ) && p["enum"].isArray() )
                for ( const auto &e : p["enum"] )
                    cp.enumValues.push_back( e.asString() );
            if ( p.isMember( "minimum" ) || p.isMember( "maximum" ) )
            {
                cp.hasRange = true;
                cp.minimum = p.get( "minimum", 0.0 ).asDouble();
                cp.maximum = p.get( "maximum", 0.0 ).asDouble();
            }
            out.params.push_back( std::move( cp ) );
        }
    if ( json.isMember( "outputs" ) && json["outputs"].isArray() )
        for ( const auto &p : json["outputs"] )
        {
            ContractParam cp;
            cp.name = p.get( "name", "" ).asString();
            cp.type = p.get( "type", "" ).asString();
            cp.description = p.get( "description", "" ).asString();
            cp.required = p.get( "required", false ).asBool();
            out.outputs.push_back( std::move( cp ) );
        }
    if ( json.isMember( "required" ) && json["required"].isArray() )
        for ( const auto &r : json["required"] )
            out.requiredParams.push_back( r.asString() );
    if ( json.isMember( "extensions" ) )
        out.extensions = json["extensions"];
    return true;
}

std::vector<std::string> ContractDescriptor::paramNames() const
{
    std::vector<std::string> names;
    names.reserve( params.size() );
    for ( const auto &p : params )
        names.push_back( p.name );
    return names;
}

} // namespace sicnu::contracts
