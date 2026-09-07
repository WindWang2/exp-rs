// tests/test_cartography_tokens.cpp
// Design System 4.0 Milestone A — design token registry, resolution, and
// compiler consumption (ADR 0130).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/design_tokens.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_compiler.h"

#include <qgsapplication.h>
#include <qgslayoutitemlabel.h>
#include <qgslayoutitemmap.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>

#include <QFont>
#include <QString>

#include "agent/layout_tools/layout_service.h"

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;

namespace {

Json::Value minimalSpec( const std::string &layoutName )
{
  Json::Value spec = makeMapSpec( layoutName, Json::Value() );
  Json::Value frame( Json::objectValue );
  frame["id"] = "map-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 12 );
  rect.append( 24 );
  rect.append( 190 );
  rect.append( 160 );
  frame["rect_mm"] = rect;
  Json::Value extent( Json::arrayValue );
  extent.append( 116.0 );
  extent.append( 39.0 );
  extent.append( 117.0 );
  extent.append( 40.0 );
  frame["extent"] = extent;
  spec["map_frames"].append( frame );
  return spec;
}

} // namespace

TEST_CASE( "TokenSetRegistry loads shipped sets with embedded fallback",
           "[cartography][tokens]" )
{
  auto &registry = TokenSetRegistry::instance();
  const Json::Value sets = registry.tokenSets();
  REQUIRE( sets.isArray() );
  CHECK( sets.size() >= 2 ); // scientific-light + scientific-dark shipped

  bool hasLight = false;
  bool hasDark = false;
  for ( const auto &set : sets )
  {
    hasLight = hasLight || set["id"].asString() == "scientific-light";
    hasDark = hasDark || set["id"].asString() == "scientific-dark";
  }
  CHECK( hasLight );
  CHECK( hasDark );

  const Json::Value light = registry.find( QLatin1String( "scientific-light" ) );
  REQUIRE( !light.isNull() );
  CHECK( light["typography"]["styles"]["title"]["size_pt"].asDouble() ==
         Catch::Approx( 20.0 ) );
  CHECK( registry.find( QLatin1String( "no-such-set" ) ).isNull() );

  // Malformed registrations are rejected with a reason.
  QString error;
  Json::Value bad( Json::objectValue );
  bad["id"] = "broken-set";
  bad["colors"]["text"] = "not-a-color";
  CHECK_FALSE( registry.registerTokenSet( bad, &error ) );
  CHECK_FALSE( error.isEmpty() );
}

TEST_CASE( "Token resolution merges medium variants and overrides",
           "[cartography][tokens][resolution]" )
{
  // Default: scientific-light, print medium.
  Json::Value tokens = resolveTokenSet( Json::Value() );
  CHECK( tokens["resolved"]["token_set"].asString() == "scientific-light" );
  CHECK( tokens["resolved"]["medium"].asString() == "print" );
  CHECK( tokens["typography"]["styles"]["title"]["size_pt"].asDouble() == Catch::Approx( 20.0 ) );
  // Screen variant raises the title size (deep merge).
  Json::Value style( Json::objectValue );
  style["medium"] = "screen";
  tokens = resolveTokenSet( style );
  CHECK( tokens["typography"]["styles"]["title"]["size_pt"].asDouble() == Catch::Approx( 22.0 ) );
  CHECK( tokens["variants"].isNull() ); // consumed by resolution

  // Explicit overrides win over everything.
  style["token_set"] = "scientific-dark";
  Json::Value overrides( Json::objectValue );
  overrides["typography"]["styles"]["title"]["size_pt"] = 26;
  style["overrides"] = overrides;
  tokens = resolveTokenSet( style );
  CHECK( tokens["resolved"]["token_set"].asString() == "scientific-dark" );
  CHECK( tokens["typography"]["styles"]["title"]["size_pt"].asDouble() == Catch::Approx( 26.0 ) );
  CHECK( tokens["colors"]["background"].asString() == "#1e2126" );

  // Unknown sets fall back to the default (total resolution).
  style["token_set"] = "does-not-exist";
  tokens = resolveTokenSet( style );
  CHECK( tokens["resolved"]["token_set"].asString() == "scientific-light" );

  // mergeTokenValues is pure and deep for objects, replacing for arrays.
  Json::Value base( Json::objectValue );
  base["a"]["x"] = 1;
  base["a"]["y"] = 2;
  base["list"] = Json::Value( Json::arrayValue );
  base["list"].append( "one" );
  Json::Value overlay( Json::objectValue );
  overlay["a"]["y"] = 3;
  Json::Value newList( Json::arrayValue );
  newList.append( "two" );
  overlay["list"] = newList;
  const Json::Value merged = mergeTokenValues( base, overlay );
  CHECK( merged["a"]["x"].asInt() == 1 );
  CHECK( merged["a"]["y"].asInt() == 3 );
  CHECK( merged["list"].size() == 1 );
  CHECK( merged["list"][0].asString() == "two" );
  CHECK( base["a"]["y"].asInt() == 2 ); // untouched
}

TEST_CASE( "Token typed accessors fall back deterministically", "[cartography][tokens]" )
{
  const Json::Value tokens = resolveTokenSet( Json::Value() );
  CHECK( tokenNumber( tokens, "spacing.margin_mm", 0.0 ) == Catch::Approx( 12.0 ) );
  CHECK( tokenNumber( tokens, "spacing.nonexistent", 5.0 ) == Catch::Approx( 5.0 ) );
  CHECK( tokenString( tokens, "typography.font_family" ) == "Arial" );
  CHECK( tokenBool( tokens, "chart.show_grid", false ) );

  const Json::Value okabeIto = tokenPalette( tokens, "qualitative" );
  REQUIRE( okabeIto.size() >= 6 );
  CHECK( tokenPalette( tokens, "no-such-palette" ).size() == okabeIto.size() );

  const Json::Value title = tokenTextStyle( tokens, "title", 18.0 );
  CHECK( title["size_pt"].asDouble() == Catch::Approx( 20.0 ) );
  CHECK( title["weight"].asString() == "bold" );
  // Unknown styles degrade to the body style.
  const Json::Value mystery = tokenTextStyle( tokens, "mystery", 18.0 );
  CHECK( mystery["size_pt"].asDouble() == Catch::Approx( 9.0 ) );

  applyTokenFontFallbacks( tokens );
  const QStringList substitutions = QFont::substitutes( QStringLiteral( "Arial" ) );
  CHECK_FALSE( substitutions.isEmpty() ); // CJK fallbacks registered
}

TEST_CASE( "Compiler resolves tokens into label properties", "[cartography][tokens][compiler]" )
{
  // Baseline spec: title without explicit font gets the token title style.
  Json::Value spec = minimalSpec( "token-compile" );
  Json::Value title( Json::objectValue );
  title["id"] = "title-1";
  title["text"] = "Token styled map";
  Json::Value rect( Json::arrayValue );
  rect.append( 12 );
  rect.append( 6 );
  rect.append( 200 );
  rect.append( 14 );
  title["rect_mm"] = rect;
  spec["titles"].append( title );
  Json::Value note( Json::objectValue );
  note["id"] = "note-1";
  note["text"] = "Source: token test";
  Json::Value noteRect( Json::arrayValue );
  noteRect.append( 150 );
  noteRect.append( 190 );
  noteRect.append( 130 );
  noteRect.append( 8 );
  note["rect_mm"] = noteRect;
  spec["source_notes"].append( note );

  QString error;
  QgsPrintLayout *layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  sicnu::agent::layout_tools::LayoutService &service =
    sicnu::agent::layout_tools::LayoutService::instance();
  auto *titleItem = qobject_cast<QgsLayoutItemLabel *>( service.findItem( layout, "title-1" ) );
  REQUIRE( titleItem != nullptr );
  CHECK( titleItem->font().pointSizeF() == Catch::Approx( 20.0 ) );
  CHECK( titleItem->font().bold() );
  auto *noteItem = qobject_cast<QgsLayoutItemLabel *>( service.findItem( layout, "note-1" ) );
  REQUIRE( noteItem != nullptr );
  CHECK( noteItem->font().pointSizeF() == Catch::Approx( 7.0 ) );

  // Explicit item font beats the token set; screen medium variant applies.
  Json::Value &titleSpec = spec["titles"][0];
  titleSpec["font"]["size_pt"] = 15;
  Json::Value style( Json::objectValue );
  style["medium"] = "screen";
  spec["style"] = style;
  layout = MapSpecCompiler::compile( spec, &error );
  REQUIRE( layout != nullptr );
  titleItem = qobject_cast<QgsLayoutItemLabel *>( service.findItem( layout, "title-1" ) );
  REQUIRE( titleItem != nullptr );
  CHECK( titleItem->font().pointSizeF() == Catch::Approx( 15.0 ) );
  noteItem = qobject_cast<QgsLayoutItemLabel *>( service.findItem( layout, "note-1" ) );
  REQUIRE( noteItem != nullptr );
  CHECK( noteItem->font().pointSizeF() == Catch::Approx( 9.0 ) ); // screen variant
}
