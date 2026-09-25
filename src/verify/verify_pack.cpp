// src/verify/verify_pack.cpp — pack validation, strict serde and
// deterministic composition for the Unified Scientific Verifier.
#include "verify_pack.h"

#include "verify_error_codes.h"
#include "verify_locale.h"
#include "verify_sha256.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sicnu::verify
{

namespace
{

bool isString( const Json::Value &value )
{
    return value.isString();
}

bool hasUnknownField( const Json::Value &object, const std::set<std::string> &allowed, std::string &unknown )
{
    for ( const std::string &member : object.getMemberNames() )
    {
        if ( !allowed.count( member ) )
        {
            unknown = member;
            return true;
        }
    }
    return false;
}

const std::set<std::string> &packTopFields()
{
    static const std::set<std::string> fields = { "schema", "packId", "specs" };
    return fields;
}

} // namespace

std::vector<std::string> validatePack( const VerifierPack &pack )
{
    std::vector<std::string> errors;
    if ( pack.packId.empty() )
        errors.push_back( "packId must be non-empty" );
    if ( pack.specs.empty() )
        errors.push_back( "pack has no specs: an empty verification must not exist" );
    if ( pack.specs.size() > kMaxSpecsPerPack )
        errors.push_back( "too many specs (budget " + std::to_string( kMaxSpecsPerPack ) + ")" );

    std::set<std::string> ids;
    for ( std::size_t index = 0; index < pack.specs.size() && index <= kMaxSpecsPerPack; ++index )
    {
        const VerificationSpec &spec = pack.specs[index];
        const std::string where = "specs[" + std::to_string( index ) + "] '" + spec.specId + "'";
        if ( spec.specId.empty() )
        {
            errors.push_back( where + ": specId must be non-empty" );
            continue;
        }
        if ( !ids.insert( spec.specId ).second )
        {
            errors.push_back( where + ": duplicate specId" );
            continue;
        }
        for ( const std::string &error : validateSpec( spec ) )
            errors.push_back( where + ": " + error );
    }
    return errors;
}

Json::Value packToJson( const VerifierPack &pack )
{
    Json::Value doc( Json::objectValue );
    doc["schema"] = kPackSchema;
    doc["packId"] = pack.packId;
    Json::Value specs( Json::arrayValue );
    for ( const VerificationSpec &spec : pack.specs )
        specs.append( specToJson( spec ) );
    doc["specs"] = specs;
    return doc;
}

bool packFromJson( const Json::Value &json, VerifierPack &out, std::string &error )
{
    const auto fail = [ &error ]( const std::string &reason ) {
        error = reason;
        return false;
    };
    if ( !json.isObject() )
        return fail( "pack document must be a JSON object" );
    std::string unknown;
    if ( hasUnknownField( json, packTopFields(), unknown ) )
        return fail( "unknown pack field: '" + unknown + "'" );
    if ( !isString( json["schema"] ) || json["schema"].asString() != kPackSchema )
        return fail( std::string( "schema marker must be " ) + kPackSchema );
    if ( !isString( json["packId"] ) || json["packId"].asString().empty() )
        return fail( "'packId' must be a non-empty string" );
    if ( !json["specs"].isArray() )
        return fail( "'specs' must be an array" );

    VerifierPack parsed;
    parsed.schemaVersion = 1;
    parsed.packId = json["packId"].asString();
    std::set<std::string> ids;
    for ( const Json::Value &specJson : json["specs"] )
    {
        VerificationSpec spec;
        std::string specError;
        if ( !specFromJson( specJson, spec, specError ) )
            return fail( "spec rejected: " + specError );
        if ( !ids.insert( spec.specId ).second )
            return fail( "duplicate specId: '" + spec.specId + "'" );
        parsed.specs.push_back( std::move( spec ) );
    }

    out = std::move( parsed );
    error.clear();
    return true;
}

bool parsePack( const std::string &text, VerifierPack &out, std::string &error,
                std::vector<std::string> &validationErrors )
{
    Json::CharReaderBuilder builder;
    builder[ "allowComments" ] = false;
    // A pack nests full specs — the same untrusted-content discipline as
    // parseSpec: bound parser recursion explicitly.
    builder[ "stackLimit" ] = 128;
    Json::Value root;
    std::string parseError;
    try
    {
        const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
        const ClassicNumericLocale pin; // same locale discipline as parseSpec
        if ( !reader->parse( text.data(), text.data() + text.size(), &root, &parseError ) )
        {
            error = "invalid JSON: " + parseError;
            return false;
        }
    }
    catch ( const Json::Exception &exception )
    {
        error = std::string( "invalid JSON: " ) + exception.what();
        return false;
    }
    if ( !packFromJson( root, out, error ) )
        return false;
    validationErrors = validatePack( out );
    if ( !validationErrors.empty() )
    {
        out = VerifierPack{};
        error = "pack rejected by structural validation";
        return false;
    }
    error.clear();
    return true;
}

std::string packDigest( const VerifierPack &pack )
{
    const std::string text = canonicalJsonText( packToJson( pack ) );
    return text.empty() ? std::string{} : sha256Hex( text );
}

bool composePacks( const std::string &packId, const std::vector<VerifierPack> &parts,
                   VerifierPack &out, std::string &error )
{
    const auto fail = [ &error ]( const std::string &reason ) {
        error = reason;
        return false;
    };
    if ( packId.empty() )
        return fail( "packId must be non-empty" );
    if ( parts.empty() )
        return fail( "composing zero packs would forge an empty verification" );

    VerifierPack merged;
    merged.schemaVersion = 1;
    merged.packId = packId;
    std::map<std::string, std::string> sources; // specId -> packId that contributed it
    for ( const VerifierPack &part : parts )
    {
        for ( const VerificationSpec &spec : part.specs )
        {
            // ANY duplicate specId across parts is a conflict — even a pack
            // listed twice: composition never picks a winner silently.
            const auto existing = sources.find( spec.specId );
            if ( existing != sources.end() )
            {
                return fail( "specId conflict: '" + spec.specId + "' contributed by both '" +
                             existing->second + "' and '" + part.packId + "'" );
            }
            sources.emplace( spec.specId, part.packId );
            merged.specs.push_back( spec );
        }
    }
    std::stable_sort( merged.specs.begin(), merged.specs.end(),
                      []( const VerificationSpec &a, const VerificationSpec &b ) {
                          return a.specId < b.specId;
                      } );

    const std::vector<std::string> errors = validatePack( merged );
    if ( !errors.empty() )
        return fail( "composed pack rejected: " + errors.front() );

    out = std::move( merged );
    error.clear();
    return true;
}

} // namespace sicnu::verify
