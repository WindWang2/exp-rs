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

#include "agent/cartography/solution_registry.h"

#include <QDir>
#include <QFile>
#include <QString>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef CMAKE_SOURCE_DIR
#define CMAKE_SOURCE_DIR "."
#endif
#ifndef SICNU_CARTOGRAPHY_DATA_DIR
#define SICNU_CARTOGRAPHY_DATA_DIR "data/cartography"
#endif

using namespace sicnu::agent::cartography;
using sicnu::agent::harness::RecipeCatalog;

namespace {

// Review P1: never rely on ambient cwd/singleton state for catalog
// resolution — point every registry at the shipped catalog explicitly so the
// drift checks cannot degrade to vacuous passes (or false failures) under
// ctest, where no working directory is pinned.
void useShippedCatalogs()
{
  const auto dir = std::string( SICNU_CARTOGRAPHY_DATA_DIR );
  ComponentRegistry::instance().setDirectory( QString::fromStdString( dir ) );
  ComponentRegistry::instance().reload();
  TemplateRegistry::instance().setDirectory( QString::fromStdString( dir ) );
  TemplateRegistry::instance().reload();
  StyleRegistry::instance().setDirectory( QString::fromStdString( dir ) );
  StyleRegistry::instance().reload();
  TokenSetRegistry::instance().setDirectory( QString::fromStdString( dir ) );
  TokenSetRegistry::instance().reload();
}


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
  useShippedCatalogs();

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
  useShippedCatalogs();
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
  useShippedCatalogs();
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
  useShippedCatalogs();
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
  useShippedCatalogs();
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

// ---------------------------------------------------------------------------
// Platform 7.0: component→token closure, template inheritance closure,
// style applicability vocabulary, and mutation tests proving the checks
// above are live (a seeded dangling reference must FAIL, not pass).
// ---------------------------------------------------------------------------

TEST_CASE( "Component style_tokens resolve against the default token set (I)",
           "[platform7][drift]" )
{
  useShippedCatalogs();
  ComponentRegistry &components = ComponentRegistry::instance();
  const Json::Value tokens = resolveTokenSet( Json::Value() );

  std::vector<std::string> drifted;
  for ( const auto &component : components.components() )
  {
    if ( !component.isMember( "style_tokens" ) )
      continue;
    for ( const auto &path : component["style_tokens"] )
      if ( path.isString() && tokenValue( tokens, path.asString() ).isNull() )
        drifted.push_back( component.get( "id", "" ).asString() + " -> unknown token path '" +
                           path.asString() + "'" );
  }
  for ( const auto &entry : drifted )
    FAIL( "drift: " << entry );
}

TEST_CASE( "Template inheritance parents resolve (I)", "[platform7][drift]" )
{
  useShippedCatalogs();
  TemplateRegistry &templates = TemplateRegistry::instance();

  std::vector<std::string> drifted;
  for ( const auto &tmpl : templates.templates() )
  {
    const std::string id = tmpl.get( "id", "" ).asString();
    const Json::Value &extends = tmpl["extends"];
    if ( extends.isString() && !extends.asString().empty() &&
         templates.find( QString::fromStdString( extends.asString() ) ).isNull() )
      drifted.push_back( id + " -> unknown parent '" + extends.asString() + "'" );
    if ( extends.isArray() )
      for ( const auto &parent : extends )
        if ( parent.isString() &&
             templates.find( QString::fromStdString( parent.asString() ) ).isNull() )
          drifted.push_back( id + " -> unknown parent '" + parent.asString() + "'" );
  }
  for ( const auto &entry : drifted )
    FAIL( "drift: " << entry );
}

TEST_CASE( "Style applicability modalities stay in the closed vocabulary (I)",
           "[platform7][drift]" )
{
  useShippedCatalogs();
  StyleRegistry &styles = StyleRegistry::instance();

  std::vector<std::string> drifted;
  for ( const auto &style : styles.styles() )
  {
    if ( !style.isMember( "applicability" ) || !style["applicability"].isObject() ||
         !style["applicability"].isMember( "modalities" ) )
      continue;
    for ( const auto &modality : style["applicability"]["modalities"] )
      if ( modality.isString() &&
           !sicnu::agent::cartography::isSolutionModality( modality.asString() ) )
        drifted.push_back( style.get( "id", "" ).asString() + " -> unknown modality '" +
                           modality.asString() + "'" );
  }
  for ( const auto &entry : drifted )
    FAIL( "drift: " << entry );
}

TEST_CASE( "Mutation: a dangling component style_token fails the drift check (I)",
           "[platform7][drift][mutation]" )
{
  ComponentRegistry &components = ComponentRegistry::instance();
  const QString tempDir = QDir::temp().filePath( QStringLiteral( "sicnu-p7-drift-mutant" ) );
  QDir().mkpath( tempDir );

  Json::Value mutant( Json::objectValue );
  mutant["id"] = "test/dangling-tokens";
  mutant["category"] = "text";
  mutant["version"] = 1;
  mutant["style_tokens"] = Json::Value( Json::arrayValue );
  mutant["style_tokens"].append( "colors.does_not_exist" );
  const QString path = tempDir + "/mutant.json";
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  file.write( Json::writeString( Json::StreamWriterBuilder(), mutant ).c_str() );
  file.close();

  // Point the registry at the mutated catalog and re-run the walk.
  components.setDirectory( tempDir );
  components.reload();
  const Json::Value tokens = resolveTokenSet( Json::Value() );
  int dangling = 0;
  for ( const auto &component : components.components() )
    if ( component.isMember( "style_tokens" ) )
      for ( const auto &tokenPath : component["style_tokens"] )
        if ( tokenPath.isString() && tokenValue( tokens, tokenPath.asString() ).isNull() )
          ++dangling;
  REQUIRE( dangling == 1 );

  // Restore the shipped catalog for the remaining tests.
  components.setDirectory( QString::fromStdString( SICNU_CARTOGRAPHY_DATA_DIR ) );
  components.reload();
  REQUIRE( components.loadProblems().isEmpty() );
}

TEST_CASE( "Mutation: an unknown template extends parent fails at load (I)",
           "[platform7][drift][mutation]" )
{
  TemplateRegistry &templates = TemplateRegistry::instance();
  const QString tempDir = QDir::temp().filePath( QStringLiteral( "sicnu-p7-drift-parent" ) );
  QDir().mkpath( tempDir );
  Json::Value orphan( Json::objectValue );
  orphan["id"] = "test/orphan-tpl";
  orphan["extends"] = "ghost-parent";
  const QString path = tempDir + "/orphan.json";
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  file.write( Json::writeString( Json::StreamWriterBuilder(), orphan ).c_str() );
  file.close();

  templates.setDirectory( tempDir );
  templates.reload();
  bool reported = false;
  for ( const QString &problem : templates.loadProblems() )
    reported = reported || problem.contains( QLatin1String( "ghost-parent" ) );
  REQUIRE( reported );

  templates.setDirectory( QString::fromStdString( SICNU_CARTOGRAPHY_DATA_DIR ) );
  templates.reload();
  REQUIRE( templates.loadProblems().isEmpty() );
}
