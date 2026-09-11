// src/agent/harness/recipe_catalog.cpp
#include "recipe_catalog.h"

#include "agent_plan.h"
#include "entity_resolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcessEnvironment>

#include <json/json.h>

#include <memory>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace sicnu::agent::harness {

namespace {

/// Case-insensitive parameter-key comparison: shipped recipes emit
/// intermediates via "output" or "OUTPUT" (review finding F12).
std::string loweredKey( std::string key )
{
  std::transform( key.begin(), key.end(), key.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return key;
}

/// Issue #867 truth semantics for `when_param` gates. A gate is open iff the
/// binding CARRIES A VALUE:
///   bool         -> its own value (a flag; explicit false closes),
///   numeric      -> bound (any number, including 0 — a bound zero threshold
///                   is a real threshold, not an unbound parameter),
///   string       -> non-empty,
///   array/object -> non-empty,
///   null/absent  -> closed.
bool paramBindingCarriesValue( const Json::Value &binding )
{
  if ( binding.isNull() )
    return false;
  if ( binding.isBool() )
    return binding.asBool();
  if ( binding.isNumeric() )
    return true;
  if ( binding.isString() )
    return !binding.asString().empty();
  if ( binding.isArray() || binding.isObject() )
    return !binding.empty();
  return false;
}

/// Issue #867 substitution fidelity: Json::Value::asString() AND the JSON
/// writer both render doubles in their 17-digit binary expansion ("0.4" ->
/// "0.40000000000000002"), which leaked into operator params as garbage
/// thresholds. Floating values use std::to_chars' shortest round-trip form
/// ("0.4"); other scalars keep their wire spelling.
std::string bindingToString( const Json::Value &value )
{
  if ( value.isString() )
    return value.asString();
  if ( value.isDouble() && !value.isIntegral() )
  {
#ifdef __cpp_lib_to_chars
    // Shortest round-trip decimal form; fallback below keeps older standard
    // libraries compiling (they render the 17-digit expansion instead).
    char buffer[64];
    const std::to_chars_result result =
      std::to_chars( buffer, buffer + sizeof( buffer ), value.asDouble() );
    if ( result.ec == std::errc() )
      return std::string( buffer, result.ptr );
#endif
  }
  Json::StreamWriterBuilder builder;
  builder[ "indentation" ] = "";
  return Json::writeString( builder, value );
}

} // namespace

RecipeCatalog &RecipeCatalog::instance()
{
  static RecipeCatalog catalog;
  return catalog;
}

void RecipeCatalog::setDirectory( const std::string &directory )
{
  mDirectory = directory;
  mLoaded = false;
}

std::string RecipeCatalog::defaultDirectory() const
{
  const QString envDir = QProcessEnvironment::systemEnvironment().value(
    QStringLiteral( "SICNU_RECIPES_DIR" ) );
  if ( !envDir.isEmpty() )
    return envDir.toStdString();

  const QDir cwdRecipes( QDir::current().filePath( QStringLiteral( "data/agent/recipes" ) ) );
  if ( cwdRecipes.exists() )
    return cwdRecipes.absolutePath().toStdString();

  if ( QCoreApplication::instance() )
  {
    const QDir appRecipes( QCoreApplication::applicationDirPath()
                           + QStringLiteral( "/../data/agent/recipes" ) );
    if ( appRecipes.exists() )
      return appRecipes.absolutePath().toStdString();
  }

#ifdef SICNU_SOURCE_DIR
  {
    const QDir sourceRecipes(
      QDir( QString::fromUtf8( SICNU_SOURCE_DIR ) ).filePath( QStringLiteral( "data/agent/recipes" ) ) );
    if ( sourceRecipes.exists() )
      return sourceRecipes.absolutePath().toStdString();
  }
#endif

  return QDir::current().filePath( QStringLiteral( "data/agent/recipes" ) ).toStdString();
}

int RecipeCatalog::reload()
{
  const std::string dir = mDirectory.empty() ? defaultDirectory() : mDirectory;
  mRecipes = Json::Value( Json::objectValue );
  mAliases = Json::Value( Json::objectValue );
  mLoadProblems.clear();
  QDirIterator it( QString::fromStdString( dir ), { QStringLiteral( "*.json" ) }, QDir::Files );
  while ( it.hasNext() )
  {
    it.next();
    QFile file( it.filePath() );
    if ( !file.open( QIODevice::ReadOnly ) )
      continue;
    const QByteArray raw = file.readAll();
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &parsed, &errors ) )
    {
      mLoadProblems.push_back( it.fileName().toStdString() + ": " + errors );
      continue;
    }
    if ( parsed.isObject() && parsed.get( "kind", "" ).asString() == "harness_recipe" &&
         parsed.isMember( "recipe_id" ) )
    {
      // Platform 6.0: decisionable-knowledge metadata is optional but
      // validated — a malformed capability/preset block must not silently
      // load as authoritative knowledge.
      const auto problems = validateRecipeMetadata( parsed );
      if ( !problems.empty() )
      {
        mLoadProblems.push_back( it.fileName().toStdString() + ": " + problems.front() );
        continue;
      }
      const std::string id = parsed["recipe_id"].asString();
      mRecipes[id] = parsed;
      // Harness 7.0: deleted near-clone ids resolve to this canonical doc.
      for ( const Json::Value &alias : parsed.get( "aliases", Json::Value( Json::arrayValue ) ) )
      {
        if ( alias.isString() && !alias.asString().empty() )
          mAliases[alias.asString()] = id;
      }
    }
  }
  mLoaded = true;
  return static_cast<int>( mRecipes.size() );
}

std::vector<std::string> RecipeCatalog::validateRecipeMetadata( const Json::Value &recipe )
{
  std::vector<std::string> problems;
  if ( !recipe.isObject() )
    return { "recipe must be an object" };
  const std::string id = recipe.get( "recipe_id", "" ).asString();

  // Harness 9.0 (M4): fail-closed schema versioning. A future producer
  // version must not silently load with semantics this catalog does not
  // implement; unknown versions are a load problem like any other drift.
  {
    const std::string schemaVersion = recipe.get( "schema_version", "1.0" ).asString();
    static const char *kKnownVersions[] = { "1.0", "1.1", "2.0" };
    const bool known = std::any_of( std::begin( kKnownVersions ), std::end( kKnownVersions ),
                                    [ &schemaVersion ]( const char *v ) {
                                      return schemaVersion == v;
                                    } );
    if ( !known )
      problems.push_back( id + ": unsupported recipe schema_version '" + schemaVersion +
                          "' (known: 1.0, 1.1, 2.0)" );
  }

  // Harness 9.0 (review): the steps array gets an explicit budget — the
  // degradation fixpoint is O(steps^2) and every documented budget in this
  // validator is a backstop against hostile documents.
  if ( recipe.isMember( "steps" ) )
  {
    if ( !recipe["steps"].isArray() )
      problems.push_back( id + ": steps must be an array" );
    else if ( static_cast<int>( recipe["steps"].size() ) > 64 )
      problems.push_back( id + ": steps exceed the 64 entry budget" );
  }
  auto checkStringArray = [ &problems, &id ]( const Json::Value &parent, const char *field,
                                              int budget ) {
    if ( !parent.isMember( field ) )
      return;
    const Json::Value &array = parent[field];
    if ( !array.isArray() )
    {
      problems.push_back( id + ": " + field + " must be an array" );
      return;
    }
    if ( static_cast<int>( array.size() ) > budget )
      problems.push_back( id + ": " + field + " exceeds the " + std::to_string( budget ) +
                          " entry budget" );
    for ( const auto &entry : array )
      if ( !entry.isString() )
        problems.push_back( id + ": " + field + " entries must be strings" );
  };

  checkStringArray( recipe, "capabilities", 16 );
  checkStringArray( recipe, "limitations", 16 );
  if ( recipe.isMember( "applicability" ) )
  {
    const Json::Value &applicability = recipe["applicability"];
    if ( !applicability.isObject() )
      problems.push_back( id + ": applicability must be an object" );
    else
    {
      checkStringArray( applicability, "modalities", 16 );
      checkStringArray( applicability, "sensors", 32 );
      if ( applicability.isMember( "resolution_range" ) )
      {
        const Json::Value &range = applicability["resolution_range"];
        if ( !range.isObject() || !range.isMember( "min_m" ) || !range["min_m"].isNumeric() ||
             !range.isMember( "max_m" ) || !range["max_m"].isNumeric() ||
             range["min_m"].asDouble() > range["max_m"].asDouble() )
          problems.push_back( id + ": applicability.resolution_range needs numeric min_m <= max_m" );
      }
    }
  }
  if ( recipe.isMember( "presets" ) )
  {
    const Json::Value &presets = recipe["presets"];
    if ( !presets.isObject() )
    {
      problems.push_back( id + ": presets must be an object" );
    }
    else
    {
      const auto names = presets.getMemberNames();
      if ( static_cast<int>( names.size() ) > 16 )
        problems.push_back( id + ": presets exceed the 16 entry budget" );
      // Harness 7.0: preset internals are validated against the recipe so a
      // preset can never reference a step/output that does not exist.
      std::set<std::string> stepIds;
      for ( const Json::Value &step : recipe.get( "steps", Json::Value( Json::arrayValue ) ) )
        stepIds.insert( step.get( "id", "" ).asString() );
      std::set<std::string> outputNames;
      for ( const Json::Value &output : recipe.get( "outputs", Json::Value( Json::arrayValue ) ) )
        outputNames.insert( output.get( "name", "" ).asString() );
      for ( const auto &name : names )
      {
        const Json::Value &preset = presets[name];
        if ( !preset.isObject() )
        {
          problems.push_back( id + ": preset '" + name + "' must be an object" );
          continue;
        }
        if ( static_cast<int>( preset.getMemberNames().size() ) > 32 )
          problems.push_back( id + ": preset '" + name + "' exceeds the 32 parameter budget" );
        if ( preset.isMember( "step_params" ) )
        {
          const Json::Value &stepParams = preset["step_params"];
          if ( !stepParams.isObject() )
            problems.push_back( id + ": preset '" + name + "' step_params must be an object" );
          else
            for ( const std::string &stepId : stepParams.getMemberNames() )
              if ( !stepIds.count( stepId ) )
                problems.push_back( id + ": preset '" + name +
                                    "' overrides unknown step '" + stepId + "'" );
        }
        if ( preset.isMember( "keep_outputs" ) )
        {
          const Json::Value &keep = preset["keep_outputs"];
          if ( !keep.isArray() )
            problems.push_back( id + ": preset '" + name + "' keep_outputs must be an array" );
          else
            for ( const Json::Value &outputName : keep )
              if ( !outputName.isString() || !outputNames.count( outputName.asString() ) )
                problems.push_back( id + ": preset '" + name + "' keeps unknown output '" +
                                    outputName.asString() + "'" );
        }
      }
    }
  }
  // Harness 7.0: auto-derived wiring treats the FIRST mention of an
  // "$outputs.<name>" intermediate as its producer. Enforce that convention
  // at load so a consumer written before its producer cannot be wired
  // backwards: the first mention of an intermediate must be via a param key
  // named "output" (the production convention every shipped recipe uses).
  {
    std::set<std::string> declaredOutputs;
    for ( const Json::Value &output : recipe.get( "outputs", Json::Value( Json::arrayValue ) ) )
      if ( output.get( "name", "" ).isString() )
        declaredOutputs.insert( output["name"].asString() );
    std::set<std::string> emitted;
    for ( const Json::Value &step : recipe.get( "steps", Json::Value( Json::arrayValue ) ) )
    {
      const std::string stepId = step.get( "id", "" ).asString();
      for ( const std::string &key : step.get( "params", Json::Value() ).getMemberNames() )
      {
        const Json::Value &value = step["params"][ key ];
        if ( !value.isString() || value.asString().rfind( "$outputs.", 0 ) != 0 )
          continue;
        const std::string name = value.asString().substr( 9 );
        if ( declaredOutputs.count( name ) || emitted.count( name ) )
          continue;
        if ( loweredKey( key ) != "output" )
          problems.push_back( id + ": step '" + stepId + "' consumes intermediate '" + name +
                              "' before any step produces it (auto-wiring convention)" );
        else
          emitted.insert( name );
      }
    }
  }
  if ( recipe.isMember( "aliases" ) )
  {
    const Json::Value &aliases = recipe["aliases"];
    if ( !aliases.isArray() )
      problems.push_back( id + ": aliases must be an array" );
    else if ( static_cast<int>( aliases.size() ) > 16 )
      problems.push_back( id + ": aliases exceed the 16 entry budget" );
    else
      for ( const Json::Value &alias : aliases )
        if ( !alias.isString() || alias.asString().empty() )
          problems.push_back( id + ": aliases entries must be non-empty strings" );
  }
  if ( recipe.isMember( "expected_artifacts" ) )
  {
    const Json::Value &artifacts = recipe["expected_artifacts"];
    if ( !artifacts.isArray() )
      problems.push_back( id + ": expected_artifacts must be an array" );
    else if ( static_cast<int>( artifacts.size() ) > 32 )
      problems.push_back( id + ": expected_artifacts exceed the 32 entry budget" );
    else
      for ( const auto &artifact : artifacts )
        if ( !artifact.isObject() || !artifact.isMember( "name" ) || !artifact["name"].isString() )
          problems.push_back( id + ": every expected_artifact needs a string name" );
  }
  if ( recipe.isMember( "quality_gates" ) )
  {
    const Json::Value &gates = recipe["quality_gates"];
    if ( !gates.isArray() )
      problems.push_back( id + ": quality_gates must be an array" );
    else if ( static_cast<int>( gates.size() ) > 8 )
      problems.push_back( id + ": quality_gates exceed the 8 entry budget" );
    else
      for ( const auto &gate : gates )
        if ( !gate.isObject() || !gate.isMember( "id" ) || !gate["id"].isString() )
          problems.push_back( id + ": every quality_gate needs a string id" );
  }
  return problems;
}

std::vector<std::string> RecipeCatalog::loadProblems() const
{
  return mLoadProblems;
}

Json::Value RecipeCatalog::listRecipes() const
{
  Json::Value summaries( Json::arrayValue );
  std::vector<std::string> ids = mRecipes.getMemberNames();
  std::sort( ids.begin(), ids.end() );
  for ( const std::string &id : ids )
  {
    const Json::Value &recipe = mRecipes[id];
    Json::Value summary( Json::objectValue );
    summary["recipe_id"] = recipe["recipe_id"].asString();
    summary["title"] = recipe.get( "title", "" ).asString();
    summary["intent"] = recipe.get( "intent", "" ).asString();
    summary["step_count"] = static_cast<Json::Int>( recipe.get( "steps", Json::Value( Json::arrayValue ) ).size() );
    Json::Value slotNames( Json::arrayValue );
    for ( const auto &slot : recipe.get( "slots", Json::Value( Json::arrayValue ) ) )
      slotNames.append( slot.get( "name", "" ).asString() );
    summary["slots"] = slotNames;
    // Platform 6.0: decisionable-knowledge metadata in compact summaries so
    // agents can match recipes without pulling full documents.
    if ( recipe.isMember( "capabilities" ) && recipe["capabilities"].isArray() )
      summary["capabilities"] = recipe["capabilities"];
    if ( recipe.isMember( "applicability" ) && recipe["applicability"].isObject() &&
         recipe["applicability"].isMember( "modalities" ) )
      summary["modalities"] = recipe["applicability"]["modalities"];
    else if ( recipe.isMember( "modality" ) && recipe["modality"].isString() )
    {
      Json::Value modalities( Json::arrayValue );
      modalities.append( recipe["modality"] );
      summary["modalities"] = modalities;
    }
    summaries.append( summary );
  }
  return summaries;
}

Json::Value RecipeCatalog::recipe( const std::string &recipeId ) const
{
  if ( !mLoaded )
    const_cast<RecipeCatalog *>( this )->reload();
  // Harness 7.0: alias ids (deleted near-clone recipes) resolve to the
  // canonical document.
  const std::string canonical =
    mAliases.isMember( recipeId ) ? mAliases[recipeId].asString() : recipeId;
  return mRecipes.get( canonical, Json::Value() );
}

namespace {

/// Replaces the recipe mini-language tokens in one parameter value.
std::string substituteToken( const std::string &value, const Json::Value &slotPaths,
                             const Json::Value &outputPaths, const Json::Value &paramBindings )
{
  if ( value.rfind( "$outputs.", 0 ) == 0 )
  {
    const std::string name = value.substr( 9 );
    return outputPaths.get( name, "" ).asString();
  }
  if ( value.rfind( "$params.", 0 ) == 0 )
  {
    const std::string key = value.substr( 8 );
    if ( paramBindings.isMember( key ) )
      return bindingToString( paramBindings[key] );
    return "";
  }
  if ( value.rfind( "$", 0 ) == 0 )
  {
    // "$<slot>.path" — the only slot field; the slot key itself has no dot.
    std::string slot = value.substr( 1 );
    const size_t dot = slot.find( '.' );
    if ( dot != std::string::npos )
      slot = slot.substr( 0, dot );
    return slotPaths.get( slot, "" ).asString();
  }
  return value;
}

Json::Value substituteParams( const Json::Value &params, const Json::Value &slotPaths,
                              const Json::Value &outputPaths, const Json::Value &paramBindings )
{
  Json::Value out( Json::objectValue );
  for ( const std::string &key : params.getMemberNames() )
  {
    const Json::Value &value = params[key];
    if ( value.isString() )
      out[key] = substituteToken( value.asString(), slotPaths, outputPaths, paramBindings );
    else
      out[key] = value;
  }
  return out;
}

} // namespace

Json::Value RecipeCatalog::instantiateRecipe( const std::string &recipeId,
                                              const Json::Value &bindings,
                                              HarnessError &error ) const
{
  const Json::Value recipe = this->recipe( recipeId );
  if ( recipe.isNull() )
  {
    error = HarnessError::makeWithAction( error_codes::kDatasetNotFound,
                                          "Unknown recipe: " + recipeId,
                                          "harness:search_recipes", Json::Value() );
    error.code = error_codes::kToolNotFound;
    return {};
  }

  const Json::Value slotBindings = bindings.get( "slots", Json::Value( Json::objectValue ) );
  const Json::Value paramBindings = bindings.get( "params", Json::Value( Json::objectValue ) );
  const Json::Value outputBindings = bindings.get( "outputs", Json::Value( Json::objectValue ) );
  const std::string outputDir = bindings.get( "output_dir", "" ).asString();

  // Harness 7.0 (Area G): preset application. Deterministic: step_params
  // override one named step's params; flat entries override every step param
  // carrying the same key; keep_outputs filters declared outputs. Presets
  // tune the PRIMARY path only — params_when_skipped templates are
  // override-immune by design (a degraded branch documents its own fixed
  // fallback). The effective document copy keeps the catalog entry untouched.
  Json::Value effective = recipe;
  const std::string presetName = bindings.get( "preset", "" ).asString();
  if ( !presetName.empty() )
  {
    const Json::Value &preset = recipe.get( "presets", Json::Value() ).get( presetName,
                                                                            Json::Value() );
    if ( !preset.isObject() )
    {
      Json::Value details( Json::objectValue );
      details["recipe"] = recipeId;
      details["preset"] = presetName;
      error = HarnessError::make( error_codes::kInvalidParameter,
                                  "Unknown preset '" + presetName + "' for recipe " + recipeId,
                                  details );
      error.recoverable = true;
      return {};
    }
    const Json::Value &stepParams = preset.get( "step_params", Json::Value() );
    Json::Value steps( Json::arrayValue );
    for ( Json::Value step : recipe.get( "steps", Json::Value( Json::arrayValue ) ) )
    {
      const std::string stepId = step.get( "id", "" ).asString();
      if ( stepParams.isObject() && stepParams.isMember( stepId ) )
      {
        Json::Value params = step.get( "params", Json::Value( Json::objectValue ) );
        for ( const std::string &key : stepParams[stepId].getMemberNames() )
          params[key] = stepParams[stepId][key];
        step["params"] = params;
      }
      // Flat entries: override any step param with the same key.
      for ( const std::string &key : preset.getMemberNames() )
      {
        if ( key == "step_params" || key == "keep_outputs" || key == "description" )
          continue;
        Json::Value params = step.get( "params", Json::Value( Json::objectValue ) );
        if ( params.isMember( key ) )
          params[key] = preset[key];
        step["params"] = params;
      }
      steps.append( step );
    }
    effective["steps"] = steps;
    if ( preset.isMember( "keep_outputs" ) && preset["keep_outputs"].isArray() )
    {
      std::set<std::string> keep;
      for ( const Json::Value &outputName : preset["keep_outputs"] )
        keep.insert( outputName.asString() );
      Json::Value outputs( Json::arrayValue );
      for ( const Json::Value &output : recipe.get( "outputs", Json::Value( Json::arrayValue ) ) )
        if ( keep.count( output.get( "name", "" ).asString() ) )
          outputs.append( output );
      effective["outputs"] = outputs;
    }
  }

  // Harness 7.0 (Area G): auto-derive step wiring from data flow. Recipes
  // written before declared "inputs" existed rely on document order; the
  // engine does not guarantee ordering for steps without declared wiring,
  // which is a race (the flood NDWI eval caught the threshold step reading an
  // intermediate before the index step wrote it). A step whose params consume
  // "$outputs.<name>" gains an input on the producing step (declared outputs
  // first, then the first step emitting the intermediate). This composes with
  // the gate semantics: degradation propagation now sees real dependencies.
  // Harness 9.0 (#867): per-edge origin tracking — a producer consumed only
  // in "params_when_skipped" is a degraded-path dependency, not a normal-path
  // one, so its degradation must not flip the consumer's healthy normal
  // template (same branch-independence principle as issue #784).
  std::map<std::string, std::map<std::string, bool>> edgeRequiredForNormal;
  if ( effective.isMember( "steps" ) && effective["steps"].isArray() )
  {
    Json::Value steps = effective["steps"];
    const int wiringCount = static_cast<int>( steps.size() );
    std::map<std::string, int> indexById;
    for ( int i = 0; i < wiringCount; ++i )
      indexById[ steps[i].get( "id", "" ).asString() ] = i;

    std::map<std::string, std::string> producerOfOutput; // output name -> step id
    for ( const Json::Value &output :
          effective.get( "outputs", Json::Value( Json::arrayValue ) ) )
    {
      const std::string name = output.get( "name", "" ).asString();
      const std::string from = output.get( "from_step", "" ).asString();
      if ( !name.empty() && !from.empty() )
        producerOfOutput[ name ] = from;
    }
    std::map<std::string, std::string> emitsOutput; // intermediate name -> first emitting step
    for ( int i = 0; i < wiringCount; ++i )
    {
      const Json::Value &params = steps[i].get( "params", Json::Value( Json::objectValue ) );
      const std::string stepId = steps[i].get( "id", "" ).asString();
      for ( const std::string &key : params.getMemberNames() )
      {
        const Json::Value &value = params[ key ];
        if ( !value.isString() || value.asString().rfind( "$outputs.", 0 ) != 0 )
          continue;
        const std::string name = value.asString().substr( 9 );
        if ( emitsOutput.count( name ) )
          continue;
        emitsOutput[ name ] = stepId;
      }
    }
    for ( int i = 0; i < wiringCount; ++i )
    {
      std::set<std::string> upstream;
      std::set<std::string> normalProducers;
      for ( const char *paramsKey : { "params", "params_when_skipped" } )
      {
        const Json::Value params =
          steps[i].get( paramsKey, Json::Value( Json::objectValue ) );
        for ( const std::string &key : params.getMemberNames() )
        {
          const Json::Value &value = params[ key ];
          if ( !value.isString() || value.asString().rfind( "$outputs.", 0 ) != 0 )
            continue;
          const std::string name = value.asString().substr( 9 );
          std::string producer;
          const auto declared = producerOfOutput.find( name );
          if ( declared != producerOfOutput.end() )
            producer = declared->second;
          else
          {
            const auto emitted = emitsOutput.find( name );
            if ( emitted != emitsOutput.end() )
              producer = emitted->second;
          }
          if ( !producer.empty() && producer != steps[i].get( "id", "" ).asString() )
          {
            upstream.insert( producer );
            if ( std::string( paramsKey ) == "params" )
              normalProducers.insert( producer );
          }
        }
      }
      if ( upstream.empty() )
        continue;
      Json::Value wiring( Json::arrayValue );
      if ( steps[i].isMember( "inputs" ) && steps[i]["inputs"].isArray() )
        wiring = steps[i]["inputs"];
      const std::string consumerId = steps[i].get( "id", "" ).asString();
      for ( const std::string &producer : upstream )
      {
        bool already = false;
        for ( const Json::Value &conn : wiring )
          if ( conn.get( "step", "" ).asString() == producer )
            already = true;
        if ( already )
          continue;
        Json::Value conn( Json::objectValue );
        conn["step"] = producer;
        wiring.append( conn );
        edgeRequiredForNormal[consumerId][producer] = normalProducers.count( producer ) > 0;
      }
      steps[i]["inputs"] = wiring;
    }
    effective["steps"] = steps;
  }

  // Resolve slots through the authoritative resolver — a bound slot that does
  // not resolve stops instantiation with a typed error.
  Json::Value slotPaths( Json::objectValue );
  Json::Value planInputs( Json::arrayValue );
  for ( const auto &slot : effective.get( "slots", Json::Value( Json::arrayValue ) ) )
  {
    const std::string slotName = slot.get( "name", "" ).asString();
    const bool required = slot.get( "required", true ).asBool();
    const bool bound = slotBindings.isMember( slotName ) && slotBindings[slotName].isString() &&
                       !slotBindings[slotName].asString().empty();
    if ( !bound )
    {
      if ( required )
      {
        Json::Value details( Json::objectValue );
        details["slot"] = slotName;
        error = HarnessError::make( error_codes::kInvalidParameter,
                                    "Required recipe slot not bound: " + slotName, details );
        error.recoverable = true;
        return {};
      }
      continue; // optional slot left unbound — its gated steps drop out
    }
    const std::string reference = slotBindings[slotName].asString();
    HarnessError resolveError;
    const auto resolved = resolveDatasetRef( QString::fromStdString( reference ), &resolveError );
    if ( !resolved )
    {
      Json::Value details = resolveError.details;
      details["slot"] = slotName;
      error = HarnessError::make( resolveError.code.empty()
                                    ? std::string( error_codes::kDatasetNotFound )
                                    : resolveError.code,
                                  "Slot '" + slotName + "' did not resolve: " +
                                    resolveError.summary,
                                  details );
      return {};
    }
    slotPaths[slotName] = resolved->path.toStdString();
    Json::Value planInput( Json::objectValue );
    planInput["name"] = slotName;
    planInput["ref"] = reference;
    planInputs.append( planInput );
  }

  // Deterministic output paths: explicit bindings win; otherwise
  // <output_dir>/<recipe>_<name>.tif (relative paths stay workspace-relative
  // under the MCP path policy).
  Json::Value outputPaths( Json::objectValue );
  for ( const auto &output : effective.get( "outputs", Json::Value( Json::arrayValue ) ) )
  {
    const std::string name = output.get( "name", "" ).asString();
    if ( outputBindings.isMember( name ) && outputBindings[name].isString() )
      outputPaths[name] = outputBindings[name].asString();
    else if ( !outputDir.empty() )
      outputPaths[name] = outputDir + "/" + recipeId + "_" + name + ".tif";
    else
      outputPaths[name] = recipeId + "_" + name + ".tif";
  }
  // Gated steps may reference intermediate outputs; give those derived paths.
  // params_when_skipped templates are scanned as well — a degraded step still
  // consumes real files.
  for ( const auto &step : effective.get( "steps", Json::Value( Json::arrayValue ) ) )
  {
    for ( const char *paramsKey : { "params", "params_when_skipped" } )
    {
      const Json::Value params = step.get( paramsKey, Json::Value( Json::objectValue ) );
      for ( const std::string &key : params.getMemberNames() )
      {
        const Json::Value &value = params[key];
        if ( !value.isString() || value.asString().rfind( "$outputs.", 0 ) != 0 )
          continue;
        const std::string name = value.asString().substr( 9 );
        if ( outputPaths.isMember( name ) )
          continue;
        if ( !outputDir.empty() )
          outputPaths[name] = outputDir + "/" + recipeId + "_" + name + ".tif";
        else
          outputPaths[name] = recipeId + "_" + name + ".tif";
      }
    }
  }

  // Build the plan. Gate semantics are orthogonal and deterministic:
  //   * when_slot/when_param gate a step; a closed gate DROPS the step unless
  //     the step declares "params_when_skipped", in which case it runs with
  //     the skipped template,
  //   * degradation propagates along the declared step "inputs" wiring: a
  //     step whose own gate is closed — or that depends (transitively) on a
  //     dropped or skipped step — runs with its params_when_skipped template.
  //     Parallel branches stay independent (issue #784): an unrelated closed
  //     gate elsewhere in the recipe never flips this branch's templates.
  const Json::Value stepTemplates = effective.get( "steps", Json::Value( Json::arrayValue ) );
  std::map<std::string, bool> paramGateOpen;
  for ( const auto &step : stepTemplates )
  {
    const std::string gateKey = step.get( "when_param", "" ).asString();
    if ( gateKey.empty() || paramGateOpen.count( gateKey ) )
      continue;
    paramGateOpen[gateKey] = paramBindingCarriesValue( paramBindings.get( gateKey, Json::Value() ) );
  }
  const int stepCount = static_cast<int>( stepTemplates.size() );
  struct StepGateState
  {
      std::string id;
      bool ownGateOpen = true;
      bool hasSkipped = false;
      bool usesSkipped = false;
      bool dropped = false;
      /// Indices of steps this one declares as inputs, with whether the edge
      /// feeds this step's NORMAL template (explicit wiring or consumption in
      /// "params") — degraded-path-only edges never force the fallback (#867).
      std::vector<std::pair<int, bool>> upstream;
  };
  std::vector<StepGateState> states( stepCount );
  std::map<std::string, int> idToIndex;
  for ( int i = 0; i < stepCount; ++i )
  {
    const Json::Value &step = stepTemplates[i];
    StepGateState &state = states[i];
    state.id = step.get( "id", "" ).asString();
    if ( !state.id.empty() )
      idToIndex[state.id] = i;
    const std::string whenSlot = step.get( "when_slot", "" ).asString();
    const std::string whenParam = step.get( "when_param", "" ).asString();
    if ( !whenSlot.empty() )
      state.ownGateOpen = slotPaths.isMember( whenSlot );
    // Platform 6.0: conjunction gate — ALL named slots must be bound (the
    // fusion branch of a multi-modal workflow).
    const Json::Value whenSlots = step["when_slots"];
    if ( whenSlots.isArray() )
    {
      for ( const auto &slotName : whenSlots )
        if ( slotName.isString() && !slotPaths.isMember( slotName.asString() ) )
          state.ownGateOpen = false;
    }
    if ( !whenParam.empty() )
    {
      const auto it = paramGateOpen.find( whenParam );
      state.ownGateOpen = state.ownGateOpen && ( it != paramGateOpen.end() && it->second );
    }
    state.hasSkipped = step.isMember( "params_when_skipped" );
    state.dropped = !state.ownGateOpen && !state.hasSkipped;
  }
  for ( int i = 0; i < stepCount; ++i )
  {
    const Json::Value &inputs = stepTemplates[i]["inputs"];
    if ( !inputs.isArray() )
      continue;
    for ( const Json::Value &conn : inputs )
    {
      const auto it = idToIndex.find( conn.get( "step", "" ).asString() );
      if ( it == idToIndex.end() || it->second == i )
        continue;
      // Explicit declarations are normal-path dependencies; an auto-wired
      // degraded-path-only edge recorded false above wins over the default.
      bool requiredForNormal = true;
      const auto origins = edgeRequiredForNormal.find( states[i].id );
      if ( origins != edgeRequiredForNormal.end() )
      {
        const auto origin = origins->second.find( conn.get( "step", "" ).asString() );
        if ( origin != origins->second.end() )
          requiredForNormal = origin->second;
      }
      states[i].upstream.emplace_back( it->second, requiredForNormal );
    }
  }
  // Degradation is monotone (every flip enters a terminal state), so the
  // propagation fixpoint needs at most stepCount passes — bounded, and the
  // document item cap bounds stepCount itself.
  //
  // Harness 9.0 (#867) semantics, on top of the 8.0 contract:
  //   * own gate closed  -> fallback template when declared, else DROPPED;
  //   * a DROPPED upstream produces nothing — a normal-path dependency on it
  //     blocks this step's normal template (fallback, else DROPPED). Letting
  //     a fallback-less step survive here used to emit orphan steps that
  //     consumed intermediates no surviving step would ever produce;
  //   * a SKIPPED upstream still RUNS its fallback and its output exists, so
  //     it never blocks a fallback-less consumer (the optical_change chain:
  //     align drops -> difference falls back to raw slots -> threshold keeps
  //     running on difference's product); only consumers WITH a fallback
  //     degrade with it (the 8.0 #784 branch-conservatism contract);
  //   * an edge arising only from "params_when_skipped" is a degraded-path
  //     dependency and never blocks the healthy normal template.
  bool changed = true;
  int pass = 0;
  while ( changed && pass <= stepCount )
  {
    changed = false;
    ++pass;
    for ( StepGateState &state : states )
    {
      if ( state.dropped || state.usesSkipped )
        continue; // already terminal
      bool normalUpstreamDropped = false;
      bool anyDegradedNormalUpstream = false;
      for ( const auto &[ upstreamIndex, requiredForNormal ] : state.upstream )
      {
        const StepGateState &upstream = states[upstreamIndex];
        if ( !requiredForNormal )
          continue;
        anyDegradedNormalUpstream |= upstream.dropped || upstream.usesSkipped;
        normalUpstreamDropped |= upstream.dropped;
      }
      if ( state.ownGateOpen && !normalUpstreamDropped )
      {
        if ( !( anyDegradedNormalUpstream && state.hasSkipped ) )
          continue;
      }
      if ( state.hasSkipped )
        state.usesSkipped = true;
      else
        state.dropped = true;
      changed = true;
    }
  }

  // Harness 9.0: bounded degradation record — WHY each step left its normal
  // template. Consumed by harness:explain and the agent (an honest plan
  // states what was dropped/skipped and why); never consulted by the engine.
  Json::Value degradations( Json::arrayValue );
  Json::Value planSteps( Json::arrayValue );
  std::set<std::string> emittedIds;
  for ( int i = 0; i < stepCount; ++i )
  {
    const Json::Value &step = stepTemplates[i];
    const StepGateState &state = states[i];
    if ( state.dropped )
    {
      Json::Value degradation( Json::objectValue );
      degradation["step"] = state.id;
      degradation["mode"] = "dropped";
      degradation["reason"] = state.ownGateOpen ? "upstream_degraded" : "gate_closed";
      degradations.append( degradation );
      continue; // gate closed and nothing to fall back to — step drops out
    }
    if ( state.usesSkipped )
    {
      Json::Value degradation( Json::objectValue );
      degradation["step"] = state.id;
      degradation["mode"] = "skipped";
      degradation["reason"] = state.ownGateOpen ? "upstream_degraded" : "gate_closed";
      degradations.append( degradation );
    }
    emittedIds.insert( step.get( "id", "" ).asString() );

    Json::Value planStep( Json::objectValue );
    planStep["id"] = step.get( "id", "" ).asString();
    planStep["operator_id"] = step.get( "operator_id", "" ).asString();
    const Json::Value &templateParams =
      state.usesSkipped ? step.get( "params_when_skipped", Json::Value( Json::objectValue ) )
                        : step.get( "params", Json::Value( Json::objectValue ) );
    planStep["params"] =
      substituteParams( templateParams, slotPaths, outputPaths, paramBindings );
    // Declared dependencies gate execution order in the engine — carry them.
    if ( step.isMember( "inputs" ) && step["inputs"].isArray() && !step["inputs"].empty() )
      planStep["inputs"] = step["inputs"];
    if ( step.isMember( "verification" ) && step["verification"].isString() )
      planStep["verification"] = step["verification"];
    planSteps.append( planStep );
  }

  // Drop wiring that references steps the gates removed (the surviving step's
  // concrete params already carry its real inputs), and rewrite surviving
  // inputs whose upstream switched to concrete paths.
  for ( Json::Value &emitted : planSteps )
  {
    if ( !emitted.isMember( "inputs" ) )
      continue;
    Json::Value filtered( Json::arrayValue );
    for ( const Json::Value &conn : emitted["inputs"] )
    {
      if ( emittedIds.count( conn.get( "step", "" ).asString() ) )
        filtered.append( conn );
    }
    if ( filtered.empty() )
      emitted.removeMember( "inputs" );
    else
      emitted["inputs"] = filtered;
  }

  Json::Value plan( Json::objectValue );
  plan["schema_version"] = kAgentPlanSchemaVersion;
  plan["kind"] = "execution_plan";
  plan["plan_id"] = "plan-" + recipeId;
  plan["goal"] = effective.get( "title", recipeId ).asString();
  plan["intent"] = effective.get( "intent", "" ).asString();
  // Harness 9.0 (M4): the plan records which recipe (and schema version)
  // produced it — reproducibility bookkeeping alongside the fingerprint.
  plan["recipe_id"] = recipeId;
  plan["recipe_version"] = effective.get( "schema_version", "1.0" ).asString();
  plan["inputs"] = planInputs;
  plan["steps"] = planSteps;
  if ( !degradations.empty() )
    plan["degradations"] = degradations;
  // Declared outputs whose producing step was gate-dropped must not poison
  // the plan (agent_plan validation rejects from_step references to missing
  // steps). Outputs without from_step always survive.
  Json::Value survivingOutputs( Json::arrayValue );
  for ( const auto &output : effective.get( "outputs", Json::Value( Json::arrayValue ) ) )
  {
    const std::string fromStep = output.get( "from_step", "" ).asString();
    if ( !fromStep.empty() && !emittedIds.count( fromStep ) )
      continue;
    survivingOutputs.append( output );
  }
  plan["outputs"] = survivingOutputs;
  plan["verification"] = effective.get( "verification", Json::Value( Json::objectValue ) );
  if ( effective.isMember( "map_output" ) )
  {
    Json::Value mapOutput = effective["map_output"];
    if ( mapOutput.isObject() && bindings.isMember( "layout_name" ) )
      mapOutput["layout_name"] = bindings["layout_name"];
    plan["map_output"] = mapOutput;
    // The map output carries the same hazard as declared outputs: a
    // from_step pointing at a gate-dropped step would only surface as a
    // confusing map-compile failure after execution. Drop the map_output
    // entirely when its step is gone (an empty from_step survives).
    const std::string mapFromStep = mapOutput.get( "from_step", "" ).asString();
    if ( !mapFromStep.empty() && !emittedIds.count( mapFromStep ) )
      plan.removeMember( "map_output" );
  }
  return plan;
}

} // namespace sicnu::agent::harness
