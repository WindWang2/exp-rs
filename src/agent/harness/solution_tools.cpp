// src/agent/harness/solution_tools.cpp
#include "solution_tools.h"

#include "../cartography/design_tokens.h"
#include "../cartography/registry.h"
#include "../cartography/solution_registry.h"
#include "../cartography/style_spec.h"
#include "../contracts/spatial_contracts.h"
#include "agent_plan.h"
#include "harness_error.h"
#include "recipe_catalog.h"

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;
namespace cart = sicnu::agent::cartography;

namespace {

Json::Value objectSchema( Json::Value properties, Json::Value required )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = std::move( properties );
  if ( required.isArray() && !required.empty() )
    schema["required"] = std::move( required );
  return schema;
}

Json::Value stringProp( const char *description )
{
  Json::Value prop( Json::objectValue );
  prop["type"] = "string";
  if ( description )
    prop["description"] = description;
  return prop;
}

/// Resolvers wiring the solution validator to the authoritative catalogs.
cart::RefResolvers wiredResolvers()
{
  auto &catalog = RecipeCatalog::instance();
  if ( !catalog.loaded() )
    catalog.reload();
  cart::RefResolvers resolvers;
  resolvers.recipe = []( const std::string &id ) {
    return !RecipeCatalog::instance().recipe( id ).isNull();
  };
  resolvers.mapTemplate = []( const std::string &id ) {
    return !cart::TemplateRegistry::instance().find( QString::fromStdString( id ) ).isNull();
  };
  resolvers.reportTemplate = resolvers.mapTemplate;
  resolvers.style = []( const std::string &id ) {
    return !cart::StyleRegistry::instance().find( QString::fromStdString( id ) ).isNull();
  };
  return resolvers;
}

Json::Value runSearch( const Json::Value &input )
{
  cart::SolutionQuery query;
  query.task = input.get( "task", "" ).asString();
  query.modality = input.get( "modality", "" ).asString();
  query.sensor = input.get( "sensor", "" ).asString();
  query.keyword = input.get( "keyword", "" ).asString();
  query.quality = input.get( "quality", "" ).asString();
  query.family = input.get( "family", "" ).asString();
  if ( input.isMember( "page" ) && input["page"].isIntegral() )
    query.page = input["page"].asInt();
  if ( input.isMember( "limit" ) && input["limit"].isIntegral() )
    query.pageSize = input["limit"].asInt();
  return cart::searchSolutions( cart::SolutionRegistry::instance().solutions(), query );
}

class SearchSolutionsTool final : public SpatialTool
{
  public:
    std::string name() const override { return "solution:search"; }
    std::string displayName() const override { return "Search solution templates"; }
    std::string description() const override
    {
      return "Faceted search over validated task-level solution templates "
             "(task, modality, sensor, keyword, quality, family). Returns "
             "compact summaries with input contracts; page with {page, limit}. "
             "Solutions bind an analysis recipe + map template + styles into "
             "one verified package.";
    }
    std::vector<std::string> tags() const override { return { "solution", "search", "catalog" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      for ( const char *facet : { "task", "modality", "sensor", "keyword", "quality", "family" } )
        props[facet] = stringProp( nullptr );
      Json::Value page( Json::objectValue );
      page["type"] = "integer";
      props["page"] = page;
      Json::Value limit( Json::objectValue );
      limit["type"] = "integer";
      props["limit"] = limit;
      return objectSchema( std::move( props ), Json::Value() );
    }
    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["items"] = Json::Value( Json::arrayValue );
      props["total"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      return SpatialToolResult::ok( runSearch( input ) );
    }
};

class DescribeSolutionTool final : public SpatialTool
{
  public:
    std::string name() const override { return "solution:describe"; }
    std::string displayName() const override { return "Describe solution template"; }
    std::string description() const override
    {
      return "Full solution document: input contracts (what to bind and how it "
             "is accepted), the analysis recipe, map template, style "
             "references, verification policy and expected artifacts. Read "
             "before instantiating; aliases resolve to the canonical id.";
    }
    std::vector<std::string> tags() const override { return { "solution", "describe" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["id"] = stringProp( "Solution id or alias" );
      Json::Value required( Json::arrayValue );
      required.append( "id" );
      return objectSchema( std::move( props ), std::move( required ) );
    }
    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["solution"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string id = input.get( "id", "" ).asString();
      if ( id.empty() )
        return SpatialToolResult::failure( "missing string parameter 'id'",
                                           error_codes::kInvalidParameter, "validation" );
      const Json::Value solution = cart::SolutionRegistry::instance().find( QString::fromStdString( id ) );
      if ( solution.isNull() )
        return SpatialToolResult::failure( "Unknown solution: " + id, error_codes::kToolNotFound,
                                           "validation" );
      Json::Value out( Json::objectValue );
      out["solution"] = solution;
      // Reference summaries so the agent can drill into the recipe/template
      // without a second round-trip.
      const std::string recipeId = solution.get( "analysis_recipe", "" ).asString();
      const Json::Value recipe = RecipeCatalog::instance().recipe( recipeId );
      if ( !recipe.isNull() )
      {
        Json::Value recipeSummary( Json::objectValue );
        recipeSummary["recipe_id"] = recipe.get( "recipe_id", "" );
        recipeSummary["intent"] = recipe.get( "intent", "" );
        recipeSummary["slots"] = recipe.get( "slots", Json::Value( Json::arrayValue ) );
        out["recipe_summary"] = recipeSummary;
      }
      const std::string templateId = solution.get( "map_template", "" ).asString();
      const Json::Value tmpl =
        cart::TemplateRegistry::instance().find( QString::fromStdString( templateId ) );
      if ( !tmpl.isNull() )
      {
        Json::Value templateSummary( Json::objectValue );
        templateSummary["id"] = tmpl.get( "id", "" );
        templateSummary["description"] = tmpl.get( "description", "" );
        templateSummary["page"] = tmpl.get( "page", Json::Value() );
        templateSummary["style"] = tmpl.get( "style", Json::Value() );
        out["map_template_summary"] = templateSummary;
      }
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class ValidateSolutionTool final : public SpatialTool
{
  public:
    std::string name() const override { return "solution:validate"; }
    std::string displayName() const override { return "Validate solution template"; }
    std::string description() const override
    {
      return "Validates a solution template (intrinsic schema + reference "
             "resolution against the recipe/template/style catalogs). Also "
             "reports catalog load problems when present.";
    }
    std::vector<std::string> tags() const override { return { "solution", "validate" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["solution"] = Json::Value( Json::objectValue );
      props["solution"]["type"] = "object";
      props["solution"]["description"] = "Full solution document (or omit and pass id).";
      props["id"] = stringProp( "Validate a catalog solution by id/alias" );
      return objectSchema( std::move( props ), Json::Value() );
    }
    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["valid"] = Json::Value( Json::objectValue );
      props["problems"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      Json::Value doc = input.get( "solution", Json::Value() );
      if ( doc.isNull() && input.isMember( "id" ) )
        doc = cart::SolutionRegistry::instance().find( QString::fromStdString( input["id"].asString() ) );
      if ( doc.isNull() )
        return SpatialToolResult::failure( "provide 'solution' document or a known 'id'",
                                           error_codes::kInvalidParameter, "validation" );
      const cart::RefResolvers resolvers = wiredResolvers();
      Json::Value out( Json::objectValue );
      Json::Value problems( Json::arrayValue );
      for ( const auto &problem : cart::validateSolutionTemplate( doc, &resolvers ) )
        problems.append( problem );
      out["problems"] = problems;
      out["valid"] = problems.empty();
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class InstantiateSolutionTool final : public SpatialTool
{
  public:
    std::string name() const override { return "solution:instantiate"; }
    std::string displayName() const override { return "Instantiate solution"; }
    std::string description() const override
    {
      return "Turns a solution + bindings into a ready-to-run package: the "
             "analysis recipe compiles to an AgentPlan v2 document (execute "
             "with harness:execute_plan), the map template instantiates to a "
             "MapSpec draft (compile with cartography:compose), and style "
             "references resolve for post-run application (style:apply). "
             "Input: {id, bindings: {slots: {name: ref}, params: {...}, "
             "output_dir: \"...\"}, title?, condition_context?}.";
    }
    std::vector<std::string> tags() const override
    { return { "solution", "instantiate", "plan", "mapspec" }; }
    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["id"] = stringProp( "Solution id or alias" );
      Json::Value bindings( Json::objectValue );
      bindings["type"] = "object";
      bindings["description"] = "{slots: {name: ref}, params: {}, output_dir: \"\"}.";
      props["bindings"] = bindings;
      props["title"] = stringProp( "Map title override" );
      Json::Value context( Json::objectValue );
      context["type"] = "object";
      context["description"] = "Materialized context for visible_if/content_if/page_if conditions.";
      props["condition_context"] = context;
      Json::Value required( Json::arrayValue );
      required.append( "id" );
      required.append( "bindings" );
      return objectSchema( std::move( props ), std::move( required ) );
    }
    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["plan"] = Json::Value( Json::objectValue );
      props["mapspec"] = Json::Value( Json::objectValue );
      props["styles"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string id = input.get( "id", "" ).asString();
      if ( id.empty() )
        return SpatialToolResult::failure( "missing string parameter 'id'",
                                           error_codes::kInvalidParameter, "validation" );
      if ( !input.isMember( "bindings" ) || !input["bindings"].isObject() )
        return SpatialToolResult::failure( "missing object parameter 'bindings'",
                                           error_codes::kInvalidParameter, "validation" );
      const Json::Value solution = cart::SolutionRegistry::instance().find( QString::fromStdString( id ) );
      if ( solution.isNull() )
        return SpatialToolResult::failure( "Unknown solution: " + id, error_codes::kToolNotFound,
                                           "validation" );
      const Json::Value bindings = input["bindings"];

      // 1. Contract check: every required slot must be bound.
      Json::Value missing( Json::arrayValue );
      if ( solution.isMember( "input_contracts" ) && solution["input_contracts"].isArray() )
      {
        // NB: no local named `slots` — Qt's moc keyword macro would eat it.
        const Json::Value slotBindings = bindings.get( "slots", Json::Value() );
        for ( const auto &contract : solution["input_contracts"] )
        {
          if ( !contract.get( "required", false ).asBool() )
            continue;
          const std::string name = contract.get( "name", "" ).asString();
          if ( !slotBindings.isObject() || !slotBindings.isMember( name ) ||
               ( slotBindings[name].isString() && slotBindings[name].asString().empty() ) )
            missing.append( name );
        }
      }
      if ( !missing.empty() )
      {
        std::string names;
        for ( const auto &name : missing )
        {
          if ( !names.empty() )
            names += ", ";
          names += name.asString();
        }
        return SpatialToolResult::failure( "required input slots are not bound: " + names,
                                           error_codes::kInvalidParameter, "validation" );
      }

      // 2. Recipe -> AgentPlan v2 (authoritative plan compiler does the rest).
      const std::string recipeId = solution.get( "analysis_recipe", "" ).asString();
      Json::Value recipeBindings( Json::objectValue );
      recipeBindings["slots"] = bindings.get( "slots", Json::Value( Json::objectValue ) );
      recipeBindings["params"] = bindings.get( "params", Json::Value( Json::objectValue ) );
      if ( bindings.isMember( "output_dir" ) )
        recipeBindings["output_dir"] = bindings["output_dir"];
      if ( bindings.isMember( "outputs" ) )
        recipeBindings["outputs"] = bindings["outputs"];
      // Recipe parameter presets from the solution win only where the caller
      // did not provide their own.
      if ( solution.isMember( "recipe_params" ) && solution["recipe_params"].isObject() )
      {
        Json::Value merged = solution["recipe_params"];
        const Json::Value caller = bindings.get( "params", Json::Value() );
        if ( caller.isObject() )
          for ( const auto &key : caller.getMemberNames() )
            merged[key] = caller[key];
        recipeBindings["params"] = merged;
      }
      if ( recipeBindings["params"].isObject() && recipeBindings["params"].empty() )
        recipeBindings.removeMember( "params" );
      if ( recipeBindings["slots"].isObject() && recipeBindings["slots"].empty() )
        recipeBindings.removeMember( "slots" );
      HarnessError planError;
      const Json::Value plan = RecipeCatalog::instance().instantiateRecipe( recipeId, recipeBindings, planError );
      if ( plan.isNull() )
        return SpatialToolResult::failure( planError.summary, planError.code, "validation" );

      // 3. Map template -> MapSpec draft (+ condition context stamping).
      const std::string templateId = solution.get( "map_template", "" ).asString();
      Json::Value templateParams( Json::objectValue );
      templateParams["layout_name"] = solution.get( "id", "" ).asString() + "-map";
      if ( input.isMember( "title" ) && input["title"].isString() )
        templateParams["title"] = input["title"];
      QString templateError;
      Json::Value mapspec =
        cart::TemplateRegistry::instance().instantiateTemplate( QString::fromStdString( templateId ),
                                                                templateParams, &templateError );
      if ( mapspec.isNull() )
        return SpatialToolResult::failure( templateError.toStdString(),
                                           error_codes::kInvalidParameter, "validation" );
      Json::Value stylesOut( Json::arrayValue );
      Json::Value styleRefs( Json::objectValue );
      if ( solution.isMember( "style_spec" ) && solution["style_spec"].isObject() )
      {
        for ( const auto &role : solution["style_spec"].getMemberNames() )
        {
          const std::string styleId = solution["style_spec"][role].asString();
          const Json::Value styleDoc =
            cart::StyleRegistry::instance().find( QString::fromStdString( styleId ) );
          if ( styleDoc.isNull() )
            continue;
          styleRefs[role] = styleId;
          // Token-resolution now, so post-run application cannot fail on a
          // dangling token reference.
          std::vector<std::string> tokenProblems;
          const Json::Value resolved = cart::resolveStyleTokens( styleDoc, cart::resolveTokenSet( styleDoc ),
                                                                 &tokenProblems );
          Json::Value styledEntry( Json::objectValue );
          styledEntry["role"] = role;
          styledEntry["style_id"] = styleId;
          styledEntry["style"] = resolved;
          if ( !tokenProblems.empty() )
          {
            Json::Value problemsJson( Json::arrayValue );
            for ( const auto &problem : tokenProblems )
              problemsJson.append( problem );
            styledEntry["token_problems"] = problemsJson;
          }
          stylesOut.append( styledEntry );
        }
      }

      Json::Value out( Json::objectValue );
      out["solution"] = solution.get( "id", "" );
      out["plan"] = plan;
      out["mapspec"] = mapspec;
      out["styles"] = stylesOut;
      out["style_refs"] = styleRefs;
      out["expected_artifacts"] = solution.get( "expected_artifacts", Json::Value( Json::arrayValue ) );
      out["verification"] = solution.get( "verification", Json::Value() );
      out["limitations"] = solution.get( "limitations", Json::Value( Json::arrayValue ) );
      if ( input.isMember( "condition_context" ) && input["condition_context"].isObject() )
        out["mapspec"]["condition_context"] = input["condition_context"];
      out["next"] = "harness:execute_plan {plan}; then cartography:compose {mapspec}; "
                    "then style:apply per style entry";
      return SpatialToolResult::ok( std::move( out ) );
    }
};

} // namespace

void registerSolutionTools()
{
  auto &registry = SpatialToolRegistry::instance();
  registry.registerTool( std::make_shared<SearchSolutionsTool>() );
  registry.registerTool( std::make_shared<DescribeSolutionTool>() );
  registry.registerTool( std::make_shared<ValidateSolutionTool>() );
  registry.registerTool( std::make_shared<InstantiateSolutionTool>() );
}

} // namespace sicnu::agent::harness
