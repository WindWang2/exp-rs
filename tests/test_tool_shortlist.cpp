// tests/test_tool_shortlist.cpp
//
// Compiler 10.0: deterministic, provenance-carrying tool shortlist under a
// hard byte budget + the knowledge-budget report.

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>

#include <string>

#include "agent/harness/tool_shortlist.h"

using namespace sicnu::agent::harness;

namespace
{
std::string rendered( const Json::Value &value )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, value );
}
} // namespace

TEST_CASE( "Shortlist is deterministic: same query, byte-identical page",
           "[tool_shortlist]" )
{
  const Json::Value first = toolShortlist( "ndvi", Json::Value(), Json::Value(), 8 );
  const Json::Value second = toolShortlist( "ndvi", Json::Value(), Json::Value(), 8 );
  CHECK( rendered( first ) == rendered( second ) );
}

TEST_CASE( "Every inclusion carries provenance; unexplained items never ship",
           "[tool_shortlist]" )
{
  const Json::Value page = toolShortlist( "ndvi", Json::Value(), Json::Value(), 24 );
  REQUIRE( page.isMember( "items" ) );
  for ( const Json::Value &item : page["items"] )
  {
    CHECK( item.isMember( "why_included" ) );
    CHECK( item["why_included"].isArray() );
    CHECK( item["why_included"].size() >= 1 );
    CHECK( item["score"].asInt() > 0 );
    for ( const Json::Value &why : item["why_included"] )
      CHECK( !why["reason"].asString().empty() );
  }
}

TEST_CASE( "The rendered page respects the hard byte budget with visible truncation",
           "[tool_shortlist]" )
{
  const Json::Value page =
    toolShortlist( "e", Json::Value(), Json::Value(), kShortlistMaxLimit );
  CHECK( page["budget_bytes"].asInt64() == static_cast<Json::Int64>( kShortlistBudgetBytes ) );
  // rendered_bytes is measured with the field present; the final document is
  // at most a few digits larger (the recorded number's own width).
  CHECK( rendered( page ).size() - static_cast<size_t>( page["rendered_bytes"].asInt64() ) <=
         8u );
  CHECK( rendered( page ).size() <=
         static_cast<size_t>( kShortlistBudgetBytes ) + 8u );
  if ( page["truncated"].asBool() )
    CHECK( page["total_unfiltered"].asInt() > static_cast<int>( page["items"].size() ) );
}

TEST_CASE( "Capability operators serving an intent outrank loose description matches",
           "[tool_shortlist]" )
{
  const Json::Value page = toolShortlist( "ndvi", Json::Value(), Json::Value(), 24 );
  REQUIRE( !page["items"].empty() );
  // The top operator item must serve the intent through capability knowledge,
  // not a text coincidence.
  bool sawCapabilityIntent = false;
  for ( const Json::Value &item : page["items"] )
  {
    if ( item["surface"].asString() != "operator" )
      continue;
    for ( const Json::Value &why : item["why_included"] )
      if ( why["reason"].asString() == "serves_intent" )
        sawCapabilityIntent = true;
    break;
  }
  CHECK( sawCapabilityIntent );
}

TEST_CASE( "An empty query is an honest empty page, not a padded guess", "[tool_shortlist]" )
{
  const Json::Value page = toolShortlist( "", Json::Value(), Json::Value(), 8 );
  CHECK( page["items"].empty() );
  CHECK( page["total_unfiltered"].asInt() == 0 );
  CHECK( page["truncated"].asBool() == false );
}

TEST_CASE( "An adversarial filter list cannot blow the byte budget", "[tool_shortlist]" )
{
  Json::Value families( Json::arrayValue );
  for ( int i = 0; i < 64; ++i )
    families.append( "family-with-a-very-long-descriptive-name-" + std::to_string( i ) );
  const Json::Value page = toolShortlist( "", families, Json::Value(), kShortlistMaxLimit );
  CHECK( rendered( page ).size() <= static_cast<size_t>( kShortlistBudgetBytes ) + 8u );
}

TEST_CASE( "The knowledge budget report measures every bounded surface", "[tool_shortlist]" )
{
  const Json::Value report = knowledgeBudgetReport();
  REQUIRE( report.isMember( "surfaces" ) );
  CHECK( report["surfaces"].size() >= 3 );
  bool sawManifest = false;
  bool sawErrorCatalog = false;
  for ( const Json::Value &surface : report["surfaces"] )
  {
    CHECK( surface["measured_bytes"].asInt64() >= 0 );
    CHECK( surface["budget_bytes"].asInt64() > 0 );
    if ( surface["surface"].asString() == "capability_manifest_page" )
      sawManifest = true;
    if ( surface["surface"].asString() == "error_catalog" )
      sawErrorCatalog = true;
  }
  CHECK( sawManifest );
  CHECK( sawErrorCatalog );
  CHECK( report.isMember( "all_within_budget" ) );
}
