// tests/test_source_truth_gates.cpp — R6 source-of-truth drift gates.
//
// Machine-readable convergence gates over the registries and the surfaces
// that name them. Each gate is bounded (a handful of known files or one data
// directory — never a full-repo sweep) and treats the LIVE registry as the
// only authority:
//
//   1. operator declaration parity: per family, the REGISTER_*_OPERATOR
//      macro list, the explicit add() list in the init TU, and the runtime
//      operatorNames() set must be identical (MSVC dead-strip safety, #707
//      rationale: the explicit list is the guaranteed path; macro-only ids
//      vanish on /OPT:REF links);
//   2. recipe tool/operator existence: every operator-family id named in a
//      shipped recipe (data/agent/recipes, data/agent/scientific_recipes)
//      resolves in the live registry (the harness corpus — capabilities,
//      labs, guidance, bench — has its own dedicated gates);
//   3. living-docs surface ids: operator-family tokens in the living
//      operational docs resolve in the registry. Historical records
//      (docs/adr, docs/superpowers) are out of scope by design — they
//      document decisions as made, not the current surface.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "operators/framework/rs_operator_registry.h"

namespace {

std::string readFile( const std::string &path )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in )
    return {};
  return std::string( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
}

const std::string &sourceRoot()
{
  static const std::string root = CMAKE_SOURCE_DIR;
  return root;
}

std::set<std::string> registeredOperatorIds()
{
  std::set<std::string> names;
  for ( const std::string &name : sicnu::operators::RSOperatorRegistry::instance().operatorNames() )
    names.insert( name );
  return names;
}

// The five family init TUs; each pairs a macro shape with an explicit add()
// list in the same file.
struct FamilyTu
{
  std::string file;
  std::string familyPrefix;
};

const std::vector<FamilyTu> &familyTus()
{
  static const std::vector<FamilyTu> tus = {
    { "src/operators/rs/rs_operators_init.cpp", "rs:" },
    { "src/operators/gdal/gdal_operators_init.cpp", "gdal:" },
    { "src/operators/io/io_operators_init.cpp", "io:" },
    { "src/operators/opencv/opencv_operators_init.cpp", "opencv:" },
    { "src/operators/otb/otb_operators_init.cpp", "otb:" },
  };
  return tus;
}

std::set<std::string> macroIds( const std::string &text )
{
  // REGISTER_RS_OPERATOR( Class, "id" ) — the only registration macro shape
  // in src/operators/ (census @ 1e28de867; determinism_census.cpp scans the
  // same shape).
  std::set<std::string> ids;
  static const std::regex pattern( R"re(REGISTER_[A-Z_]+_OPERATOR\s*\(\s*\w+\s*,\s*"([^"]+)"\s*\))re" );
  for ( std::sregex_iterator it( text.begin(), text.end(), pattern ), end; it != end; ++it )
    ids.insert( ( *it )[1].str() );
  return ids;
}

std::set<std::string> explicitAddIds( const std::string &text )
{
  // add( "id", [] { ... } ) — the guaranteed registration path.
  std::set<std::string> ids;
  static const std::regex pattern( R"re(add\(\s*"([^"]+)"\s*,\s*\[\s*\]\s*\{\s*return)re" );
  for ( std::sregex_iterator it( text.begin(), text.end(), pattern ), end; it != end; ++it )
    ids.insert( ( *it )[1].str() );
  return ids;
}

bool isOperatorFamilyId( const std::string &token )
{
  static const std::regex pattern( R"re(\b(rs|gdal|io|opencv|otb):[a-z0-9_]+\b)re" );
  return std::regex_search( token, pattern );
}

std::set<std::string> operatorTokensIn( const std::string &text )
{
  std::set<std::string> tokens;
  static const std::regex pattern( R"re(\b(rs|gdal|io|opencv|otb):[a-z0-9_]+\b)re" );
  for ( std::sregex_iterator it( text.begin(), text.end(), pattern ), end; it != end; ++it )
    tokens.insert( ( *it ).str() );
  return tokens;
}

// Living operational docs only: policy/domain docs and the agent surface
// reference docs that name operators as current truth.
std::vector<std::string> livingDocFiles()
{
  std::vector<std::string> files;
  static const char *dirs[] = { "docs/processing", "docs/models", "docs/agent" };
  for ( const char *dir : dirs )
  {
    std::error_code ec;
    for ( auto &entry : std::filesystem::recursive_directory_iterator( sourceRoot() + "/" + dir, ec ) )
      if ( entry.is_regular_file() && entry.path().extension() == ".md" )
        files.push_back( entry.path().string() );
  }
  return files;
}

// Tokens that may appear unresolved, each with its documented reason. Keep
// this list small and deliberate — every entry is a decision, not an
// oversight.
std::set<std::string> wildcardExemptions()
{
  return {
    // Intentional namespace wildcard prose ("the rs:sar_* family",
    // "rs:temporal_* operators") — family mentions, not id references.
    "rs:sar_",
    "rs:temporal_",
  };
}

std::set<std::string> docExemptions()
{
  auto exempt = wildcardExemptions();
  exempt.insert( {
    // operator-development-template.md placeholder in the authoring skeleton.
    "rs:id",
    // foundation-5.md documents the deliberate ABSENCE of this id ("it is
    // rs:connected_components under GIS naming; the alias is documented").
    "rs:clump",
  } );
  return exempt;
}

std::vector<std::string> recipeFiles()
{
  std::vector<std::string> files;
  static const char *dirs[] = { "data/agent/recipes", "data/agent/scientific_recipes" };
  for ( const char *dir : dirs )
  {
    std::error_code ec;
    for ( auto &entry : std::filesystem::recursive_directory_iterator( sourceRoot() + "/" + dir, ec ) )
      if ( entry.is_regular_file() && entry.path().extension() == ".json" )
        files.push_back( entry.path().string() );
  }
  return files;
}

} // namespace

TEST_CASE( "Operator macro, explicit and runtime registration sets are identical per family",
           "[r6][source-truth][operators]" )
{
  const std::set<std::string> runtime = registeredOperatorIds();
  REQUIRE( runtime.size() >= 189 ); // the R6 census floor; grows are fine, shrinks are a rename

  std::set<std::string> unionOfExplicit;
  for ( const FamilyTu &family : familyTus() )
  {
    const std::string text = readFile( sourceRoot() + "/" + family.file );
    REQUIRE_FALSE( text.empty() );

    const std::set<std::string> macros = macroIds( text );
    const std::set<std::string> explicitIds = explicitAddIds( text );
    for ( const std::string &id : explicitIds )
      unionOfExplicit.insert( id );

    INFO( "family TU: " << family.file );
    // MSVC dead-strip safety: an id on the macro path but missing from the
    // explicit list vanishes from /OPT:REF links (the #707 class #1398 closed).
    for ( const std::string &id : macros )
    {
      INFO( "macro id: " << id );
      CHECK( explicitIds.count( id ) == 1 );
    }
    // The macro list mirrors the family; an explicit-only id understates the
    // family to every tool that greps the macro shape.
    for ( const std::string &id : explicitIds )
    {
      INFO( "explicit id: " << id );
      CHECK( macros.count( id ) == 1 );
    }
    // Both paths must land in the live registry (idempotent overwrites).
    for ( const std::string &id : explicitIds )
    {
      INFO( "registered id: " << id );
      CHECK( runtime.count( id ) == 1 );
    }
  }

  // No registered operator outside the five family TUs (plugin/runtime
  // registrations are dynamic by design and not part of the builtin truth).
  std::set<std::string> missing;
  for ( const std::string &id : runtime )
    if ( !unionOfExplicit.count( id ) )
      missing.insert( id );
  CHECK( missing.empty() );
}

TEST_CASE( "Shipped recipe steps only name registered operators",
           "[r6][source-truth][recipes]" )
{
  const std::set<std::string> runtime = registeredOperatorIds();
  const std::set<std::string> exempt = wildcardExemptions();
  std::size_t checked = 0;
  for ( const std::string &file : recipeFiles() )
  {
    const std::string text = readFile( file );
    INFO( "recipe: " << file );
    // Only operator-id-shaped step references are gated; tool-prefixed ids
    // (temporal:*, spatial:*, harness:*, cartography:*) are surface-tool
    // vocabulary owned by the tool catalog, and "$..." placeholders are
    // binding grammar.
    for ( const std::string &token : operatorTokensIn( text ) )
    {
      if ( exempt.count( token ) )
        continue;
      ++checked;
      INFO( "token: " << token );
      CHECK( runtime.count( token ) == 1 );
    }
  }
  CHECK( checked > 200 ); // the corpus is non-empty and operator-dense
}

TEST_CASE( "Living operational docs only name registered operators",
           "[r6][source-truth][docs]" )
{
  const std::set<std::string> runtime = registeredOperatorIds();
  const std::set<std::string> exempt = docExemptions();
  std::size_t checked = 0;
  for ( const std::string &file : livingDocFiles() )
  {
    for ( const std::string &token : operatorTokensIn( readFile( file ) ) )
    {
      if ( exempt.count( token ) )
        continue;
      ++checked;
      INFO( "doc: " << file << " token: " << token );
      CHECK( runtime.count( token ) == 1 );
    }
  }
  CHECK( checked > 100 ); // the living docs are operator-dense; a wipe is a gate failure too
}
