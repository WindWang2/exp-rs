// tests/test_knowledge_drift.cpp
//
// Knowledge Platform 6.0 (Milestone H) — cross-layer mechanical drift
// checks. Knowledge layers reference each other (recipes name operators,
// solutions name recipes/templates/styles, templates name components,
// styles name token sets); any drift must fail these tests instead of
// silently degrading the platform.
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/registry.h"
#include "agent/cartography/solution_registry.h"
#include "agent/cartography/style_spec.h"
#include "agent/cartography/design_tokens.h"
#include "agent/harness/recipe_catalog.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "operators/framework/rs_operator_registry.h"

#include <QString>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif

using namespace sicnu::agent::cartography;
using sicnu::agent::harness::RecipeCatalog;

namespace {

std::set<std::string> atomicOperatorIds()
{
  std::set<std::string> ids;
  auto &atomic = sicnu::processing::AtomicAlgorithmRegistry::instance();
  atomic.initialize();
  for ( const auto &descriptor : atomic.listDescriptors() )
    ids.insert( descriptor.id );
  return ids;
}

bool operatorKnown( const std::string &id, const std::set<std::string> &atomicIds )
{
  if ( atomicIds.count( id ) > 0 )
    return true;
  return sicnu::operators::RSOperatorRegistry::instance().hasOperator( id );
}

} // namespace

TEST_CASE( "Recipe operator ids resolve against the operator registries (H)",
           "[platform6][drift]" )
{
  RecipeCatalog &catalog = RecipeCatalog::instance();
  catalog.setDirectory( ( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/recipes" ) );
  catalog.reload();
  REQUIRE( catalog.loadProblems().empty() );

  const std::set<std::string> atomicIds = atomicOperatorIds();
  REQUIRE_FALSE( atomicIds.empty() );

  int stepsChecked = 0;
  std::vector<std::string> unknown;
  for ( const auto &recipe : catalog.listRecipes() )
  {
    const Json::Value doc = catalog.recipe( recipe["recipe_id"].asString() );
    for ( const auto &step : doc.get( "steps", Json::Value( Json::arrayValue ) ) )
    {
      const std::string operatorId = step.get( "operator_id", "" ).asString();
      if ( operatorId.empty() )
        continue;
      ++stepsChecked;
      if ( !operatorKnown( operatorId, atomicIds ) )
        unknown.push_back( recipe["recipe_id"].asString() + " -> " + operatorId );
    }
  }
  REQUIRE( stepsChecked > 100 );
  CHECK( unknown.empty() );
  for ( const auto &entry : unknown )
    FAIL( "drift: recipe references unknown operator: " << entry );
}

TEST_CASE( "Solution references (recipe/template/style) resolve (H)", "[platform6][drift]" )
{
  SolutionRegistry &solutions = SolutionRegistry::instance();
  solutions.setDirectory(
    QString::fromStdString( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/solutions" ) );
  solutions.reload();
  REQUIRE( solutions.loadProblems().isEmpty() );

  RecipeCatalog &catalog = RecipeCatalog::instance();
  TemplateRegistry &templates = TemplateRegistry::instance();
  StyleRegistry &styles = StyleRegistry::instance();

  std::vector<std::string> drifted;
  for ( const auto &solution : solutions.solutions() )
  {
    const std::string id = solution.get( "id", "" ).asString();
    const std::string recipeId = solution.get( "analysis_recipe", "" ).asString();
    if ( !recipeId.empty() && catalog.recipe( recipeId ).isNull() )
      drifted.push_back( id + " -> unknown recipe '" + recipeId + "'" );
    for ( const char *field : { "map_template", "report_template" } )
    {
      const std::string templateId = solution.get( field, "" ).asString();
      if ( !templateId.empty() && templates.find( QString::fromStdString( templateId ) ).isNull() )
        drifted.push_back( id + " -> unknown template '" + templateId + "'" );
    }
    // style_spec: {role: style id} — every referenced style must resolve.
    if ( solution.isMember( "style_spec" ) && solution["style_spec"].isObject() )
      for ( const auto &role : solution["style_spec"].getMemberNames() )
      {
        const std::string styleId = solution["style_spec"][role].asString();
        if ( !styleId.empty() && styles.find( QString::fromStdString( styleId ) ).isNull() )
          drifted.push_back( id + " -> unknown style '" + styleId + "' (role " + role + ")" );
      }
  }
  CHECK( drifted.empty() );
  for ( const auto &entry : drifted )
    FAIL( "drift: " << entry );
}

TEST_CASE( "Template component references resolve (H)", "[platform6][drift]" )
{
  TemplateRegistry &templates = TemplateRegistry::instance();
  ComponentRegistry &components = ComponentRegistry::instance();

  std::vector<std::string> drifted;
  for ( const auto &tmpl : templates.templates() )
  {
    const std::string id = tmpl.get( "id", "" ).asString();
    auto checkComponentRef = [ &components, &id, &drifted ]( const Json::Value &ref ) {
      const std::string componentId = ref.isString() ? ref.asString()
                                                     : ref.get( "id", "" ).asString();
      if ( componentId.empty() )
        return;
      const Json::Value variant = ref.isObject() ? ref.get( "variant", "" ) : Json::Value();
      const Json::Value resolved = components.find( QString::fromStdString( componentId ) );
      if ( resolved.isNull() )
        drifted.push_back( id + " -> unknown component '" + componentId + "'" );
      else if ( !variant.isNull() && variant.isString() && !variant.asString().empty() )
      {
        bool found = false;
        if ( resolved.isMember( "variants" ) && resolved["variants"].isArray() )
          for ( const auto &candidate : resolved["variants"] )
            found = found || candidate.get( "id", "" ).asString() == variant.asString();
        if ( !found )
          drifted.push_back( id + " -> component '" + componentId +
                             "' lacks variant '" + variant.asString() + "'" );
      }
    };
    if ( tmpl.isMember( "recommended_components" ) && tmpl["recommended_components"].isArray() )
      for ( const auto &ref : tmpl["recommended_components"] )
        checkComponentRef( ref );
    const Json::Value &slotList = tmpl.isMember( "slots" ) && tmpl["slots"].isArray()
                                    ? tmpl["slots"]
                                    : tmpl["required_slots"];
    if ( slotList.isArray() )
      for ( const auto &slot : slotList )
        if ( slot.isObject() && slot.isMember( "component" ) )
          checkComponentRef( slot["component"] );
  }
  CHECK( drifted.empty() );
  for ( const auto &entry : drifted )
    FAIL( "drift: " << entry );
}

TEST_CASE( "Style token-set references and token chains resolve (H)", "[platform6][drift]" )
{
  StyleRegistry &styles = StyleRegistry::instance();
  TokenSetRegistry &tokens = TokenSetRegistry::instance();

  std::vector<std::string> drifted;
  for ( const auto &style : styles.styles() )
  {
    const std::string id = style.get( "id", "" ).asString();
    const std::string tokenSetRef = style.get( "token_set_ref", "" ).asString();
    if ( !tokenSetRef.empty() && tokens.find( QString::fromStdString( tokenSetRef ) ).isNull() )
      drifted.push_back( id + " -> unknown token set '" + tokenSetRef + "'" );
    // Every token reference inside the style must resolve against its set.
    const Json::Value tokenSet = tokens.find( QString::fromStdString(
      tokenSetRef.empty() ? std::string( kDefaultTokenSetId ) : tokenSetRef ) );
    if ( tokenSet.isNull() )
      continue;
    std::vector<std::string> problems;
    std::vector<const Json::Value *> stack{ &style };
    while ( !stack.empty() )
    {
      const Json::Value *node = stack.back();
      stack.pop_back();
      if ( node->isString() )
      {
        if ( isTokenReference( node->asString() ) )
          resolveTokenReferenceChain( tokenSet, *node, problems );
      }
      else if ( node->isObject() )
        for ( const auto &key : node->getMemberNames() )
          stack.push_back( &( *node )[key] );
      else if ( node->isArray() )
        for ( const auto &entry : *node )
          stack.push_back( &entry );
    }
    for ( const auto &problem : problems )
      drifted.push_back( id + " -> " + problem );
  }
  CHECK( drifted.empty() );
  for ( const auto &entry : drifted )
    FAIL( "drift: " << entry );
}

TEST_CASE( "Composite component children references resolve (H)", "[platform6][drift]" )
{
  ComponentRegistry &components = ComponentRegistry::instance();
  std::vector<std::string> drifted;
  for ( const auto &component : components.components() )
  {
    if ( !component.isMember( "children" ) || !component["children"].isArray() )
      continue;
    const std::string id = component.get( "id", "" ).asString();
    for ( const auto &child : component["children"] )
    {
      const std::string ref = child.get( "source_component", "" ).asString();
      if ( !ref.empty() && components.find( QString::fromStdString( ref ) ).isNull() )
        drifted.push_back( id + " -> child '" + child.get( "role", "" ).asString() +
                           "' references unknown component '" + ref + "'" );
    }
  }
  CHECK( drifted.empty() );
}
