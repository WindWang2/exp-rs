#include "explain/guidance_store.h"

#include "explain/explain_provenance.h"
#include "explain/state_vocabulary.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include <json/json.h>

namespace sicnu::explain
{
namespace
{

constexpr const char *kGuidanceSchemaV1 = "exp.step_guidance.v1";
constexpr size_t kMaxEntriesPerFile = 64;
constexpr size_t kMaxTotalEntries = 1024;

std::string slurpFile( const std::filesystem::path &path, bool &ok )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in )
  {
    ok = false;
    return {};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  ok = true;
  return buffer.str();
}

// jsoncpp safe-reader pattern (see plugin_manifest.cpp): a depth bound makes
// the reader THROW on nested bombs, so the refusal is typed instead of a
// stack overflow.
bool parseStrict( const std::string &text, Json::Value &root, std::string &error )
{
  Json::CharReaderBuilder builder;
  builder["allowComments"] = true;
  builder["stackLimit"] = 128;
  const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  try
  {
    return reader->parse( text.data(), text.data() + text.size(), &root, &error );
  }
  catch ( const Json::Exception &exception )
  {
    error = exception.what();
    return false;
  }
}

bool readStringField( const Json::Value &object, const char *key, std::string &out,
                      std::string &error, bool required = true )
{
  if ( !object.isMember( key ) )
  {
    if ( required )
      error = std::string( "missing field '" ) + key + "'";
    return !required;
  }
  if ( !object[key].isString() )
  {
    error = std::string( "field '" ) + key + "' must be a string";
    return false;
  }
  out = object[key].asString();
  return true;
}

bool readStringArray( const Json::Value &object, const char *key, std::vector<std::string> &out,
                      std::string &error )
{
  if ( !object.isMember( key ) )
    return true;
  if ( !object[key].isArray() )
  {
    error = std::string( "field '" ) + key + "' must be an array";
    return false;
  }
  for ( const Json::Value &entry : object[key] )
  {
    if ( !entry.isString() || entry.asString().empty() )
    {
      error = std::string( "field '" ) + key + "' entries must be non-empty strings";
      return false;
    }
    out.push_back( entry.asString() );
  }
  return true;
}

bool readAllowedKeys( const Json::Value &object, const std::vector<const char *> &allowed,
                      std::string &error )
{
  for ( const std::string &member : object.getMemberNames() )
  {
    if ( std::none_of( allowed.begin(), allowed.end(),
                       [ &member ]( const char *k ) { return member == k; } ) )
    {
      error = "unknown key '" + member + "'";
      return false;
    }
  }
  return true;
}

bool parseRationaleEntry( const Json::Value &value, AuthoredParameterRationale &out,
                          std::string &error )
{
  if ( !value.isObject() )
  {
    error = "parameterRationale entries must be objects";
    return false;
  }
  if ( !readAllowedKeys( value, { "parameter", "rationale", "misconfigurationConsequence" }, error ) )
    return false;
  if ( !readStringField( value, "parameter", out.parameter, error ) || out.parameter.empty() )
  {
    if ( error.empty() )
      error = "field 'parameter' must not be empty";
    return false;
  }
  if ( !readStringField( value, "rationale", out.rationale, error ) || out.rationale.empty() )
  {
    if ( error.empty() )
      error = "field 'rationale' must not be empty";
    return false;
  }
  return readStringField( value, "misconfigurationConsequence", out.misconfigurationConsequence,
                          error, false );
}

bool parseStateNarrative( const Json::Value &value, AuthoredStateNarrative &out,
                          std::string &error )
{
  if ( !value.isObject() )
  {
    error = "stateNarrative must be an object";
    return false;
  }
  if ( !readAllowedKeys( value, { "before", "after" }, error ) )
    return false;
  if ( !readStringField( value, "before", out.before, error ) )
    return false;
  if ( !readStringField( value, "after", out.after, error ) )
    return false;
  // Teaching expectations must speak the projected state vocabulary; empty
  // means "deliberately unspecified", anything else unknown is a typo.
  for ( const std::string *token : { &out.before, &out.after } )
  {
    if ( !token->empty() && !isKnownStateToken( *token ) )
    {
      error = "stateNarrative token '" + *token + "' is not in the state vocabulary";
      return false;
    }
  }
  return true;
}

bool parseSkipConsequence( const Json::Value &value, AuthoredSkipConsequence &out,
                           std::string &error )
{
  if ( !value.isObject() )
  {
    error = "skipConsequence must be an object";
    return false;
  }
  if ( !readAllowedKeys( value, { "summary", "detail", "downstreamRoles" }, error ) )
    return false;
  if ( !readStringField( value, "summary", out.summary, error ) || out.summary.empty() )
  {
    if ( error.empty() )
      error = "field 'summary' must not be empty";
    return false;
  }
  if ( !readStringField( value, "detail", out.detail, error, false ) )
    return false;
  return readStringArray( value, "downstreamRoles", out.downstreamRoles, error );
}

bool parseReferenceEntry( const Json::Value &value, AuthoredReference &out, std::string &error )
{
  if ( !value.isObject() )
  {
    error = "references entries must be objects";
    return false;
  }
  if ( !readAllowedKeys( value, { "title", "kind", "locator", "note" }, error ) )
    return false;
  if ( !readStringField( value, "title", out.title, error ) || out.title.empty() )
  {
    if ( error.empty() )
      error = "field 'title' must not be empty";
    return false;
  }
  std::string kind;
  if ( !readStringField( value, "kind", kind, error ) )
    return false;
  if ( !isKnownReferenceKind( kind ) )
  {
    error = "unknown reference kind '" + kind + "'";
    return false;
  }
  out.kind = kind;
  if ( !readStringField( value, "locator", out.locator, error ) || out.locator.empty() )
  {
    if ( error.empty() )
      error = "field 'locator' must not be empty";
    return false;
  }
  return readStringField( value, "note", out.note, error, false );
}

std::optional<StepGuidance> parseEntry( const Json::Value &root, std::string &error )
{
  if ( !root.isObject() )
  {
    error = "document must be an object";
    return std::nullopt;
  }
  if ( !readAllowedKeys( root,
                         { "schema", "operatorId", "role", "purpose", "whenToUse",
                           "prerequisitesNote", "assumptions", "parameterRationale",
                           "stateNarrative", "skipConsequence", "references", "teachingNote" },
                         error ) )
    return std::nullopt;

  std::string schema;
  if ( !readStringField( root, "schema", schema, error ) )
    return std::nullopt;
  if ( schema != kGuidanceSchemaV1 )
  {
    error = "unsupported schema '" + schema + "'";
    return std::nullopt;
  }

  StepGuidance guidance;
  if ( !readStringField( root, "operatorId", guidance.operatorId, error ) ||
       guidance.operatorId.empty() || containsWhitespace( guidance.operatorId ) )
  {
    if ( error.empty() )
      error = "field 'operatorId' must be a non-empty id without whitespace";
    return std::nullopt;
  }
  if ( !readStringField( root, "role", guidance.role, error, false ) )
    return std::nullopt;
  if ( !guidance.role.empty() && containsWhitespace( guidance.role ) )
  {
    error = "field 'role' must not contain whitespace";
    return std::nullopt;
  }
  if ( !readStringField( root, "purpose", guidance.purpose, error ) || guidance.purpose.empty() )
  {
    if ( error.empty() )
      error = "field 'purpose' must not be empty";
    return std::nullopt;
  }
  if ( !readStringField( root, "whenToUse", guidance.whenToUse, error, false ) )
    return std::nullopt;
  if ( !readStringArray( root, "prerequisitesNote", guidance.prerequisitesNote, error ) )
    return std::nullopt;
  if ( !readStringArray( root, "assumptions", guidance.assumptions, error ) )
    return std::nullopt;

  if ( root.isMember( "parameterRationale" ) )
  {
    if ( !root["parameterRationale"].isArray() )
    {
      error = "field 'parameterRationale' must be an array";
      return std::nullopt;
    }
    if ( root["parameterRationale"].size() > kMaxEntriesPerFile )
    {
      error = "parameterRationale exceeds the per-file entry bound";
      return std::nullopt;
    }
    for ( const Json::Value &entry : root["parameterRationale"] )
    {
      AuthoredParameterRationale rationale;
      if ( !parseRationaleEntry( entry, rationale, error ) )
        return std::nullopt;
      guidance.parameterRationale.push_back( rationale );
    }
  }
  if ( root.isMember( "stateNarrative" ) )
  {
    AuthoredStateNarrative narrative;
    if ( !parseStateNarrative( root["stateNarrative"], narrative, error ) )
      return std::nullopt;
    guidance.stateNarrative = narrative;
  }
  if ( root.isMember( "skipConsequence" ) )
  {
    AuthoredSkipConsequence consequence;
    if ( !parseSkipConsequence( root["skipConsequence"], consequence, error ) )
      return std::nullopt;
    guidance.skipConsequence = consequence;
  }
  if ( root.isMember( "references" ) )
  {
    if ( !root["references"].isArray() )
    {
      error = "field 'references' must be an array";
      return std::nullopt;
    }
    for ( const Json::Value &entry : root["references"] )
    {
      AuthoredReference reference;
      if ( !parseReferenceEntry( entry, reference, error ) )
        return std::nullopt;
      guidance.references.push_back( reference );
    }
  }
  if ( !readStringField( root, "teachingNote", guidance.teachingNote, error, false ) )
    return std::nullopt;

  return guidance;
}

} // namespace

std::unique_ptr<GuidanceStore> GuidanceStore::loadFromDirectory(
  const std::string &directory, std::vector<GuidanceLoadProblem> &problems )
{
  auto store = std::unique_ptr<GuidanceStore>( new GuidanceStore() );

  std::error_code ec;
  if ( !std::filesystem::exists( directory, ec ) || !std::filesystem::is_directory( directory, ec ) )
  {
    problems.push_back( { directory, "directory_missing", "guidance directory does not exist" } );
    return store;
  }

  std::vector<std::filesystem::path> files;
  for ( const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator( directory, ec ) )
  {
    if ( ec )
      break;
    if ( entry.is_regular_file() && entry.path().extension() == ".json" )
      files.push_back( entry.path() );
  }
  std::sort( files.begin(), files.end() );

  for ( const std::filesystem::path &file : files )
  {
    const std::string fileName = file.filename().string();
    if ( store->entries_.size() >= kMaxTotalEntries )
    {
      problems.push_back( { fileName, "entry_limit_exceeded",
                            "total entry bound reached; remaining files skipped" } );
      break;
    }

    bool readable = false;
    const std::string text = slurpFile( file, readable );
    if ( !readable )
    {
      problems.push_back( { fileName, "file_unreadable", "cannot read file" } );
      continue;
    }

    Json::Value root;
    std::string error;
    if ( !parseStrict( text, root, error ) )
    {
      problems.push_back( { fileName, "parse_failed", error } );
      continue;
    }

    std::optional<StepGuidance> guidance = parseEntry( root, error );
    if ( !guidance.has_value() )
    {
      const std::string code = error.rfind( "unknown key '", 0 ) == 0 ? "unknown_key"
                               : error.rfind( "unsupported schema", 0 ) == 0
                                 ? "schema_version_unsupported"
                                 : error.rfind( "missing field", 0 ) == 0 ? "missing_field"
                                                                          : "invalid_value";
      problems.push_back( { fileName, code, error } );
      continue;
    }

    const GuidanceStore::Key key{ guidance->operatorId, guidance->role };
    if ( store->entries_.count( key ) > 0 )
    {
      problems.push_back( { fileName, "duplicate_entry",
                            "entry for " + guidance->operatorId + "[" + guidance->role +
                              "] already loaded; first wins" } );
      continue;
    }
    store->entries_.emplace( key, *guidance );
  }

  store->problems_ = problems;
  return store;
}

std::optional<StepGuidance> GuidanceStore::guidanceFor( const std::string &operatorId,
                                                        const std::string &role ) const
{
  if ( !role.empty() )
  {
    const auto roleIt = entries_.find( Key{ operatorId, role } );
    if ( roleIt != entries_.end() )
      return roleIt->second;
  }
  const auto genericIt = entries_.find( Key{ operatorId, std::string() } );
  if ( genericIt != entries_.end() )
    return genericIt->second;
  return std::nullopt;
}

} // namespace sicnu::explain
