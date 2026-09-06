// src/agent/harness/recipe_catalog.cpp
#include "recipe_catalog.h"

#include "entity_resolver.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcessEnvironment>

#include <json/json.h>

#include <memory>

#include <algorithm>
#include <cmath>
#include <map>

namespace sicnu::agent::harness {

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
      continue;
    if ( parsed.isObject() && parsed.get( "kind", "" ).asString() == "harness_recipe" &&
         parsed.isMember( "recipe_id" ) )
      mRecipes[parsed["recipe_id"].asString()] = parsed;
  }
  mLoaded = true;
  return static_cast<int>( mRecipes.size() );
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
    Json::Value slots( Json::arrayValue );
    for ( const auto &slot : recipe.get( "slots", Json::Value( Json::arrayValue ) ) )
      slots.append( slot.get( "name", "" ).asString() );
    summary["slots"] = slots;
    summaries.append( summary );
  }
  return summaries;
}

Json::Value RecipeCatalog::recipe( const std::string &recipeId ) const
{
  if ( !mLoaded )
    const_cast<RecipeCatalog *>( this )->reload();
  return mRecipes.get( recipeId, Json::Value() );
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
    if ( paramBindings.isMember( key ) && paramBindings[key].isString() )
      return paramBindings[key].asString();
    return paramBindings.get( key, "" ).asString();
  }
  if ( value.rfind( "$", 0 ) == 0 )
  {
    const std::string slot = value.substr( 1 );
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

  // Resolve slots through the authoritative resolver — a bound slot that does
  // not resolve stops instantiation with a typed error.
  Json::Value slotPaths( Json::objectValue );
  Json::Value planInputs( Json::arrayValue );
  for ( const auto &slot : recipe.get( "slots", Json::Value( Json::arrayValue ) ) )
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
  for ( const auto &output : recipe.get( "outputs", Json::Value( Json::arrayValue ) ) )
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
  for ( const auto &step : recipe.get( "steps", Json::Value( Json::arrayValue ) ) )
  {
    const Json::Value params = step.get( "params", Json::Value( Json::objectValue ) );
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

  // Build the plan. Gate semantics are orthogonal and deterministic:
  //   * when_slot/when_param gate a step; a closed gate DROPS the step unless
  //     the step declares "params_when_skipped", in which case it runs with
  //     the skipped template,
  //   * any closed gate anywhere also routes ungated downstream steps to
  //     their params_when_skipped template (an upstream artifact the gate
  //     would have produced no longer exists).
  const Json::Value stepTemplates = recipe.get( "steps", Json::Value( Json::arrayValue ) );
  std::map<std::string, bool> paramGateOpen;
  for ( const auto &step : stepTemplates )
  {
    const std::string gateKey = step.get( "when_param", "" ).asString();
    if ( gateKey.empty() || paramGateOpen.count( gateKey ) )
      continue;
    const Json::Value &binding = paramBindings.get( gateKey, Json::Value() );
    const bool open = binding.isBool() ? binding.asBool()
                                       : ( binding.isString() && !binding.asString().empty() );
    paramGateOpen[gateKey] = open;
  }
  bool anyGateClosed = false;
  for ( const auto &step : stepTemplates )
  {
    const std::string whenSlot = step.get( "when_slot", "" ).asString();
    if ( !whenSlot.empty() && !slotPaths.isMember( whenSlot ) )
      anyGateClosed = true;
    const std::string whenParam = step.get( "when_param", "" ).asString();
    if ( !whenParam.empty() && !paramGateOpen[whenParam] )
      anyGateClosed = true;
  }

  Json::Value planSteps( Json::arrayValue );
  for ( const auto &step : stepTemplates )
  {
    const std::string whenSlot = step.get( "when_slot", "" ).asString();
    const std::string whenParam = step.get( "when_param", "" ).asString();
    const bool hasSkipped = step.isMember( "params_when_skipped" );

    bool ownGateOpen = true;
    if ( !whenSlot.empty() )
      ownGateOpen = slotPaths.isMember( whenSlot );
    if ( !whenParam.empty() )
      ownGateOpen = ownGateOpen && paramGateOpen[whenParam];

    if ( !ownGateOpen && !hasSkipped )
      continue; // gate closed and nothing to fall back to — step drops out

    Json::Value planStep( Json::objectValue );
    planStep["id"] = step.get( "id", "" ).asString();
    planStep["operator_id"] = step.get( "operator_id", "" ).asString();
    const bool useSkipped = !ownGateOpen || ( anyGateClosed && hasSkipped );
    const Json::Value &templateParams =
      useSkipped ? step.get( "params_when_skipped", Json::Value( Json::objectValue ) )
                 : step.get( "params", Json::Value( Json::objectValue ) );
    planStep["params"] =
      substituteParams( templateParams, slotPaths, outputPaths, paramBindings );
    if ( step.isMember( "verification" ) && step["verification"].isString() )
      planStep["verification"] = step["verification"];
    planSteps.append( planStep );
  }

  Json::Value plan( Json::objectValue );
  plan["schema_version"] = kAgentPlanSchemaVersion;
  plan["kind"] = "execution_plan";
  plan["plan_id"] = "plan-" + recipeId;
  plan["goal"] = recipe.get( "title", recipeId ).asString();
  plan["intent"] = recipe.get( "intent", "" ).asString();
  plan["inputs"] = planInputs;
  plan["steps"] = planSteps;
  plan["outputs"] = recipe.get( "outputs", Json::Value( Json::arrayValue ) );
  plan["verification"] = recipe.get( "verification", Json::Value( Json::objectValue ) );
  if ( recipe.isMember( "map_output" ) )
  {
    Json::Value mapOutput = recipe["map_output"];
    if ( mapOutput.isObject() && bindings.isMember( "layout_name" ) )
      mapOutput["layout_name"] = bindings["layout_name"];
    plan["map_output"] = mapOutput;
  }
  return plan;
}

} // namespace sicnu::agent::harness
