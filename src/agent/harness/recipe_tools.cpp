// src/agent/harness/recipe_tools.cpp
#include "recipe_tools.h"

#include "agent_plan.h"
#include "contracts/spatial_contracts.h"
#include "harness_error.h"
#include "recipe_catalog.h"

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

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

class SearchRecipesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:search_recipes"; }
    std::string displayName() const override { return "Search Scientific Recipes"; }
    std::string description() const override
    {
      return "Lists the metadata-driven scientific recipes (bounded summaries): "
             "optical vegetation, optical change, SAR change, land-cover, "
             "phenology. Recipes orchestrate existing operators only — they "
             "are the sanctioned starting points for the plan lifecycle.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "recipe", "search" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value intent( Json::objectValue );
      intent["type"] = "string";
      intent["description"] = "Optional intent filter (ndvi|change|sar_change|classify|phenology).";
      props["intent"] = intent;
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      auto &catalog = RecipeCatalog::instance();
      if ( !catalog.loaded() )
        catalog.reload();
      Json::Value summaries = catalog.listRecipes();
      const std::string intent = input.get( "intent", "" ).asString();
      if ( !intent.empty() )
      {
        Json::Value filtered( Json::arrayValue );
        for ( const auto &entry : summaries )
        {
          if ( entry.get( "intent", "" ).asString() == intent )
            filtered.append( entry );
        }
        summaries = filtered;
      }
      Json::Value out( Json::objectValue );
      out["recipes"] = summaries;
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class DescribeRecipeTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:describe_recipe"; }
    std::string displayName() const override { return "Describe Recipe"; }
    std::string description() const override
    {
      return "Returns the full recipe document: slots (what to bind), the "
             "operator steps with parameters and gates, declared outputs, and "
             "the map-output contract. Read before instantiating.";
    }
    std::vector<std::string> tags() const override { return { "harness", "recipe" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value recipeId( Json::objectValue );
      recipeId["type"] = "string";
      props["recipe_id"] = recipeId;
      Json::Value required( Json::arrayValue );
      required.append( "recipe_id" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string recipeId = input.get( "recipe_id", "" ).asString();
      if ( recipeId.empty() )
        return SpatialToolResult::failure( "missing string parameter 'recipe_id'",
                                           error_codes::kInvalidParameter, "validation" );
      auto &catalog = RecipeCatalog::instance();
      if ( !catalog.loaded() )
        catalog.reload();
      const Json::Value recipe = catalog.recipe( recipeId );
      if ( recipe.isNull() )
        return SpatialToolResult::failure( "Unknown recipe: " + recipeId,
                                           error_codes::kToolNotFound, "validation" );
      Json::Value out( Json::objectValue );
      out["recipe"] = recipe;
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class InstantiateRecipeTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:instantiate_recipe"; }
    std::string displayName() const override { return "Instantiate Recipe"; }
    std::string description() const override
    {
      return "Binds a recipe's slots to real datasets (resolved through the "
             "authoritative entity resolver — an unresolvable slot fails "
             "typed) and returns a ready AgentPlan v2 document for "
             "harness:execute_plan. Bindings: {slots: {primary: \"asset-1\"}, "
             "params: {...}, output_dir: \"...\"}.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "recipe", "plan" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value recipeId( Json::objectValue );
      recipeId["type"] = "string";
      props["recipe_id"] = recipeId;
      Json::Value bindings( Json::objectValue );
      bindings["type"] = "object";
      bindings["description"] = "{slots: {name: ref}, params: {}, outputs: {}, output_dir: \"\"}.";
      props["bindings"] = bindings;
      Json::Value required( Json::arrayValue );
      required.append( "recipe_id" );
      required.append( "bindings" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string recipeId = input.get( "recipe_id", "" ).asString();
      if ( recipeId.empty() )
        return SpatialToolResult::failure( "missing string parameter 'recipe_id'",
                                           error_codes::kInvalidParameter, "validation" );
      if ( !input.isMember( "bindings" ) || !input["bindings"].isObject() )
        return SpatialToolResult::failure( "missing object parameter 'bindings'",
                                           error_codes::kInvalidParameter, "validation" );
      auto &catalog = RecipeCatalog::instance();
      if ( !catalog.loaded() )
        catalog.reload();
      HarnessError error;
      const Json::Value plan = catalog.instantiateRecipe( recipeId, input["bindings"], error );
      if ( plan.isNull() )
        return SpatialToolResult::failure( error.summary, error.code, "validation" );

      Json::Value out( Json::objectValue );
      out["plan"] = plan;
      // Validate compile-ability immediately so the agent sees typed issues
      // now rather than at execution time.
      AgentPlan parsed;
      HarnessError readError;
      if ( readAgentPlan( plan, parsed, readError ) )
      {
        const Json::Value estimates = estimatePlanResources( parsed );
        out["estimates"] = estimates;
        out["next"] = "harness:execute_plan {plan}";
      }
      return SpatialToolResult::ok( std::move( out ) );
    }
};

} // namespace

void registerRecipeTools()
{
  static bool registered = false;
  if ( registered )
    return;
  registered = true;
  auto &registry = SpatialToolRegistry::instance();
  registry.registerTool( std::make_shared<SearchRecipesTool>() );
  registry.registerTool( std::make_shared<DescribeRecipeTool>() );
  registry.registerTool( std::make_shared<InstantiateRecipeTool>() );
}

} // namespace sicnu::agent::harness
