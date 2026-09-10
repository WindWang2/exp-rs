// tests/test_platform7.cpp
//
// Cartography Platform 7.0 — explainable constraint solving (package A):
//   hardness (hard|soft), priority + weighted objective, rank-aware soft
//   rejection, canonical multi-fixpoint policy, bounded unsat cores,
//   decisions ledger, v4 validation and v3 back-compatibility.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include "agent/cartography/chart_registry.h"
#include "agent/cartography/composition.h"
#include "agent/cartography/design_tokens.h"
#include "agent/cartography/quality.h"
#include "agent/cartography/registry.h"
#include "agent/cartography/style_spec.h"
#include "agent/cartography/typography.h"
#include "agent/mapspec/mapspec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cmath>
#include <string>
#include <vector>

using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;

namespace {

Json::Value rectItem( const std::string &id, double x, double y, double w, double h )
{
  Json::Value item( Json::objectValue );
  item["id"] = id;
  Json::Value rect( Json::arrayValue );
  rect.append( x );
  rect.append( y );
  rect.append( w );
  rect.append( h );
  item["rect_mm"] = rect;
  return item;
}

Json::Value constraint( const std::string &id, const std::string &kind, const std::string &a,
                        const std::string &b, double gapMm = -1.0 )
{
  Json::Value c( Json::objectValue );
  c["id"] = id;
  c["kind"] = kind;
  Json::Value items( Json::arrayValue );
  items.append( a );
  if ( !b.empty() )
    items.append( b );
  c["items"] = items;
  if ( gapMm >= 0.0 )
    c["gap_mm"] = gapMm;
  return c;
}

/// Marks a constraint entry with the v4 surface fields.
void markSoft( Json::Value &c, int priority, double weight )
{
  c["hardness"] = "soft";
  c["priority"] = priority;
  c["weight"] = weight;
}

const Json::Value &itemById( const Json::Value &spec, const char *collection,
                             const std::string &id )
{
  for ( const auto &item : spec[collection] )
    if ( item["id"].asString() == id )
      return item;
  FAIL( "item not found: " << id );
  static Json::Value null;
  return null;
}

bool decisionsContain( const Json::Value &composition, const std::string &cid,
                       const std::string &outcome )
{
  for ( const auto &decision : composition["decisions"] )
    if ( decision["cid"].asString() == cid && decision["outcome"].asString() == outcome )
      return true;
  return false;
}

std::string rejectionReason( const Json::Value &composition, const std::string &cid )
{
  for ( const auto &decision : composition["decisions"] )
    if ( decision["cid"].asString() == cid && decision["outcome"].asString() == "rejected" )
      return decision["reason"].asString();
  return std::string();
}

void writeJsonFile( const QString &path, const Json::Value &doc )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
  file.write( Json::writeString( Json::StreamWriterBuilder(), doc ).c_str() );
  file.close();
}

} // namespace

TEST_CASE( "P7 templates: multi-parent extends with per-key merge policy",
           "[platform7][templates]" )
{
  TemplateRegistry &registry = TemplateRegistry::instance();
  const QString tempDir = QDir::temp().filePath( QStringLiteral( "sicnu-p7-templates" ) );
  QDir().mkpath( tempDir );

  // base: foundation layout with two slots and a print variant.
  Json::Value base( Json::objectValue );
  base["id"] = "p7-base";
  base["description"] = "base description";
  Json::Value baseSlots( Json::arrayValue );
  {
    Json::Value slot( Json::objectValue );
    slot["role"] = "map.main";
    Json::Value rect( Json::arrayValue );
    rect.append( 10.0 );
    rect.append( 10.0 );
    rect.append( 200.0 );
    rect.append( 150.0 );
    slot["rect_mm"] = rect;
    baseSlots.append( slot );
  }
  {
    Json::Value slot( Json::objectValue );
    slot["role"] = "legend.primary";
    Json::Value rect( Json::arrayValue );
    rect.append( 220.0 );
    rect.append( 10.0 );
    rect.append( 60.0 );
    rect.append( 100.0 );
    slot["rect_mm"] = rect;
    baseSlots.append( slot );
  }
  base["slots"] = baseSlots;
  base["facets"]["tasks"] = Json::Value( Json::arrayValue );
  base["facets"]["tasks"].append( "terrain" );
  base["facets"]["medium"] = "a4";
  base["facets"]["purpose"] = "analysis";
  Json::Value baseVariants( Json::arrayValue );
  {
    Json::Value variant( Json::objectValue );
    variant["id"] = "print";
    variant["description"] = "base print variant";
    variant["page"]["width_mm"] = 297.0;
    variant["page"]["height_mm"] = 210.0;
    baseVariants.append( variant );
  }
  base["variants"] = baseVariants;
  writeJsonFile( tempDir + "/p7-base.json", base );

  // domain: overrides the legend slot, adds a task + a recommended component.
  Json::Value domain( Json::objectValue );
  domain["id"] = "p7-domain";
  domain["extends"] = "p7-base";
  Json::Value domainSlots( Json::arrayValue );
  {
    Json::Value slot( Json::objectValue );
    slot["role"] = "legend.primary";
    Json::Value rect( Json::arrayValue );
    rect.append( 220.0 );
    rect.append( 10.0 );
    rect.append( 80.0 );
    rect.append( 120.0 );
    slot["rect_mm"] = rect;
    domainSlots.append( slot );
  }
  domain["slots"] = domainSlots;
  domain["facets"]["tasks"] = Json::Value( Json::arrayValue );
  domain["facets"]["tasks"].append( "water" );
  domain["recommended_components"] = Json::Value( Json::arrayValue );
  domain["recommended_components"].append( "north.arrow.basic" );
  writeJsonFile( tempDir + "/p7-domain.json", domain );

  // child: multi-parent [base, domain] + its own overrides.
  Json::Value child( Json::objectValue );
  child["id"] = "p7-medium";
  child["description"] = "child description";
  Json::Value parents( Json::arrayValue );
  parents.append( "p7-base" );
  parents.append( "p7-domain" );
  child["extends"] = parents;
  child["facets"]["medium"] = "a3";
  Json::Value childVariants( Json::arrayValue );
  {
    Json::Value variant( Json::objectValue );
    variant["id"] = "screen";
    variant["description"] = "child variant";
    childVariants.append( variant );
  }
  child["variants"] = childVariants;
  writeJsonFile( tempDir + "/p7-medium.json", child );

  registry.setDirectory( tempDir );
  registry.reload();
  if ( !registry.loadProblems().isEmpty() )
    FAIL( registry.loadProblems().join( "; " ).toStdString() );

  const Json::Value resolved = registry.find( QLatin1String( "p7-medium" ) );
  REQUIRE_FALSE( resolved.isNull() );
  // Child description wins; multi-parent stamps the array form.
  REQUIRE( resolved["description"].asString() == "child description" );
  REQUIRE( resolved["extends"].isArray() );
  REQUIRE( resolved["extends"].size() == 2 );
  // Slots: map.main inherited from base; legend.primary overridden by the
  // domain layer (later parent wins over base; child declared nothing).
  bool sawMapSlot = false;
  for ( const auto &slot : resolved["slots"] )
  {
    if ( slot["role"].asString() == "map.main" )
      sawMapSlot = true;
    if ( slot["role"].asString() == "legend.primary" )
      REQUIRE( slot["rect_mm"][2].asDouble() == Catch::Approx( 80.0 ) );
  }
  REQUIRE( sawMapSlot );
  // Facets merge: tasks UNION (terrain + water), medium from the child
  // override, purpose inherited from base.
  REQUIRE( resolved["facets"]["tasks"].size() == 2 );
  REQUIRE( resolved["facets"]["tasks"][0].asString() == "terrain" );
  REQUIRE( resolved["facets"]["tasks"][1].asString() == "water" );
  REQUIRE( resolved["facets"]["medium"].asString() == "a3" );
  REQUIRE( resolved["facets"]["purpose"].asString() == "analysis" );
  // Variants merge by id: print (inherited from base) + screen (child).
  REQUIRE( resolved["variants"].size() == 2 );
  // Provenance explains where each top-level field came from.
  REQUIRE( resolved["inheritance"]["parents"].size() == 2 );
  REQUIRE( resolved["inheritance"]["sources"]["description"][0].asString() == "p7-medium" );
  REQUIRE( resolved["inheritance"]["sources"]["facets"].size() >= 2 );
  REQUIRE( resolved["inheritance"]["sources"]["slots"].size() >= 2 );

  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();
  REQUIRE( registry.loadProblems().isEmpty() );
}

TEST_CASE( "P7 templates: inheritance cycles are diagnosed explicitly",
           "[platform7][templates]" )
{
  TemplateRegistry &registry = TemplateRegistry::instance();
  const QString tempDir = QDir::temp().filePath( QStringLiteral( "sicnu-p7-cycle" ) );
  QDir().mkpath( tempDir );
  Json::Value a( Json::objectValue );
  a["id"] = "p7-cyc-a";
  a["extends"] = "p7-cyc-b";
  Json::Value b( Json::objectValue );
  b["id"] = "p7-cyc-b";
  Json::Value parents( Json::arrayValue );
  parents.append( "p7-cyc-a" );
  b["extends"] = parents;
  writeJsonFile( tempDir + "/p7-cyc-a.json", a );
  writeJsonFile( tempDir + "/p7-cyc-b.json", b );

  registry.setDirectory( tempDir );
  registry.reload();
  REQUIRE_FALSE( registry.loadProblems().isEmpty() );
  bool sawCycle = false;
  for ( const QString &problem : registry.loadProblems() )
    sawCycle = sawCycle || problem.contains( QLatin1String( "extends cycle" ) );
  REQUIRE( sawCycle );
  REQUIRE( registry.find( QLatin1String( "p7-cyc-a" ) ).isNull() );

  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();
  REQUIRE( registry.loadProblems().isEmpty() );
}

TEST_CASE( "P7 templates: extends shape validation rejects malformed chains",
           "[platform7][templates][validation]" )
{
  TemplateRegistry &registry = TemplateRegistry::instance();
  const QString tempDir = QDir::temp().filePath( QStringLiteral( "sicnu-p7-shape" ) );
  QDir().mkpath( tempDir );
  Json::Value selfExt( Json::objectValue );
  selfExt["id"] = "p7-self";
  selfExt["extends"] = "p7-self";
  writeJsonFile( tempDir + "/p7-self.json", selfExt );
  Json::Value badType( Json::objectValue );
  badType["id"] = "p7-badtype";
  Json::Value badParents( Json::arrayValue );
  badParents.append( 42 );
  badType["extends"] = badParents;
  writeJsonFile( tempDir + "/p7-badtype.json", badType );

  registry.setDirectory( tempDir );
  registry.reload();
  REQUIRE_FALSE( registry.loadProblems().isEmpty() );
  bool sawSelf = false;
  bool sawShape = false;
  for ( const QString &problem : registry.loadProblems() )
  {
    sawSelf = sawSelf || problem.contains( QLatin1String( "extends itself" ) );
    sawShape = sawShape || problem.contains( QLatin1String( "non-empty strings" ) );
  }
  REQUIRE( sawSelf );
  REQUIRE( sawShape );

  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();
  REQUIRE( registry.loadProblems().isEmpty() );
}

TEST_CASE( "P7 templates: shipped catalog still resolves (single-parent legacy)",
           "[platform7][templates][compat]" )
{
  TemplateRegistry &registry = TemplateRegistry::instance();
  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();
  REQUIRE( registry.loadProblems().isEmpty() );
  // sar-backscatter-a3l extends sar-backscatter-a4l: facets now MERGE (the
  // 6.0 wholesale-replace P3), so the parent tasks carry over and the child
  // medium wins.
  const Json::Value resolved = registry.find( QLatin1String( "sar-backscatter-a3l" ) );
  REQUIRE_FALSE( resolved.isNull() );
  REQUIRE( resolved["extends"].isString() );
  REQUIRE( resolved["extends"].asString() == "sar-backscatter-a4l" );
  REQUIRE( resolved["facets"]["medium"].asString() == "a3" );
  REQUIRE( resolved["facets"]["tasks"].isArray() );
  REQUIRE_FALSE( resolved["facets"]["tasks"].empty() );
  REQUIRE( resolved["inheritance"]["parents"].size() == 1 );
}

TEST_CASE( "P7 typography: deterministic width model (known answers)",
           "[platform7][typography]" )
{
  using namespace sicnu::agent::cartography;
  // Width classes: fullwidth 1 em, space 0.35 em, other 0.55 em; 1 pt =
  // 0.3528 mm. "Hello" = 5 x 0.55 em.
  REQUIRE( measureLineMm( "Hello", 10.0 ) == Catch::Approx( 5 * 0.55 * 10.0 * 0.3528 ).epsilon( 1e-9 ) );
  // "地图" = 2 fullwidth em (measured per codepoint, not per UTF-8 byte).
  REQUIRE( measureLineMm( "\xE5\x9C\xB0\xE5\x9B\xBE", 10.0 ) ==
           Catch::Approx( 2 * 1.0 * 10.0 * 0.3528 ).epsilon( 1e-9 ) );
  // Mixed: 2 fullwidth + 1 narrow.
  REQUIRE( measureLineMm( "\xE5\x9C\xB0\xE5\x9B\xBE" "a", 10.0 ) ==
           Catch::Approx( ( 2 * 1.0 + 0.55 ) * 10.0 * 0.3528 ).epsilon( 1e-9 ) );
  // Spaces measure distinctly.
  REQUIRE( measureLineMm( "a b", 10.0 ) ==
           Catch::Approx( ( 0.55 + 0.35 + 0.55 ) * 10.0 * 0.3528 ).epsilon( 1e-9 ) );
  // Invalid UTF-8 degrades deterministically to U+FFFD (0.55 em each).
  REQUIRE( measureLineMm( "\xFF\xFF", 10.0 ) ==
           Catch::Approx( 2 * 0.55 * 10.0 * 0.3528 ).epsilon( 1e-9 ) );
}

TEST_CASE( "P7 typography: greedy word wrap keeps Latin words unbroken",
           "[platform7][typography]" )
{
  using namespace sicnu::agent::cartography;
  // At 10 pt, one narrow char is 1.9404 mm; make a box of exactly two
  // "xx "-ish words wide.
  const double pt = 10.0;
  const double charMm = 0.55 * pt * 0.3528; // 1.9404
  const std::string text = "aaaa bbbb cccc";
  const std::vector<std::string> lines =
    wrapTextMm( text, 4 * charMm + 0.5, pt ); // "aaaa" + margin fits, "aaaa " doesn't
  REQUIRE( lines.size() == 3 );
  REQUIRE( lines[0] == "aaaa" );
  REQUIRE( lines[1] == "bbbb" );
  REQUIRE( lines[2] == "cccc" );
}

TEST_CASE( "P7 typography: CJK wraps between glyphs and honors kinsoku",
           "[platform7][typography]" )
{
  using namespace sicnu::agent::cartography;
  const double pt = 10.0;
  const double emMm = 1.0 * pt * 0.3528; // 3.528 mm per fullwidth glyph
  // 5 glyphs: 地图与分 at a 3-glyph width → breaks between glyphs.
  const std::string text = "\xE5\x9C\xB0\xE5\x9B\xBE\xE4\xB8\x8E\xE5\x88\x86\xE6\x9E\x90";
  const std::vector<std::string> lines = wrapTextMm( text, 3 * emMm + 0.01, pt );
  REQUIRE( lines.size() == 2 );
  REQUIRE( lines[0].size() == 9 ); // 3 glyphs x 3 UTF-8 bytes
  // Kinsoku: a line never ends before closing punctuation — "地图，" stays
  // together even at a 3-glyph width.
  const std::string punct = "\xE5\x9C\xB0\xE5\x9B\xBE\xEF\xBC\x8C\xE5\x88\x86\xE6\x9E\x90";
  const std::vector<std::string> kinsoku = wrapTextMm( punct, 3 * emMm + 0.01, pt );
  REQUIRE( kinsoku.size() >= 2 );
  for ( const std::string &line : kinsoku )
  {
    // No line may END with the comma... actually no line may START with the
    // comma: "，" must stay attached to the previous glyph.
    REQUIRE( line.find( "\xEF\xBC\x8C" ) != 0 );
  }
}

TEST_CASE( "P7 typography: hard line breaks are honored",
           "[platform7][typography]" )
{
  using namespace sicnu::agent::cartography;
  const std::vector<std::string> lines = wrapTextMm( "one\ntwo\nthree", 1000.0, 10.0 );
  REQUIRE( lines.size() == 3 );
  REQUIRE( lines[0] == "one" );
  REQUIRE( lines[1] == "two" );
  REQUIRE( lines[2] == "three" );
}

TEST_CASE( "P7 typography: shrink_to_fit finds the largest fitting font",
           "[platform7][typography]" )
{
  using namespace sicnu::agent::cartography;
  TextFitRequest request;
  request.text = "clipped title example";
  request.boxWidthMm = 40.0;
  request.boxHeightMm = 6.0;
  request.fontPt = 12.0;
  request.fontPtMin = 6.0;
  request.policy = "shrink_to_fit";

  const TextFitReport report = fitTextIntoBox( request );
  REQUIRE( report.fits );
  REQUIRE( report.fontPolicy == "shrunk" );
  REQUIRE( report.fontPt < 12.0 );
  REQUIRE( report.fontPt >= 6.0 );
  REQUIRE( report.usedWidthMm <= 40.0 + 1e-6 );
  // Deterministic: identical request, byte-identical report.
  const Json::Value again = textFitReportToJson( fitTextIntoBox( request ) );
  REQUIRE( textFitReportToJson( report ).toStyledString() == again.toStyledString() );
}

TEST_CASE( "P7 typography: ellipsis policy truncates without silent drops",
           "[platform7][typography]" )
{
  using namespace sicnu::agent::cartography;
  TextFitRequest request;
  request.text = "a very long map title that will not fit";
  request.boxWidthMm = 20.0;
  request.boxHeightMm = 4.0;
  request.fontPt = 10.0;
  request.policy = "ellipsis";

  const TextFitReport report = fitTextIntoBox( request );
  REQUIRE( report.truncated );
  REQUIRE_FALSE( report.lines.empty() );
  // The last line ends with the ellipsis (U+2026, UTF-8 e2 80 a6) — or the
  // box is degenerate, in which case the diagnostic says so.
  const std::string &last = report.lines.back();
  const bool endsWithEllipsis = last.size() >= 3 &&
                                last.compare( last.size() - 3, 3, "\xE2\x80\xA6" ) == 0;
  if ( !endsWithEllipsis )
  {
    REQUIRE( report.diagnostics.size() == 1 );
    REQUIRE( report.diagnostics[0].find( "ellipsis marker" ) != std::string::npos );
  }
  REQUIRE( report.usedWidthMm <= 20.0 + 1e-6 );
}

TEST_CASE( "P7 typography: overflow_report never hides overflow",
           "[platform7][typography]" )
{
  using namespace sicnu::agent::cartography;
  TextFitRequest request;
  request.text = "\xE5\x9C\xB0\xE5\x9B\xBE\xE6\xA0\x87\xE6\xB3\xA8\xE6\xB5\x8B\xE8\xAF\x95";
  request.boxWidthMm = 10.0;
  request.boxHeightMm = 3.0;
  request.fontPt = 10.0;
  request.policy = "overflow_report";

  const TextFitReport report = fitTextIntoBox( request );
  REQUIRE_FALSE( report.fits );
  REQUIRE( report.overflowWidthMm > 0.0 );
  REQUIRE( report.truncated == false ); // nothing hidden
  REQUIRE_FALSE( report.diagnostics.empty() );
  REQUIRE( report.diagnostics[0].find( "nothing is hidden" ) != std::string::npos );
}

TEST_CASE( "P7 typography: fit report JSON projection is complete",
           "[platform7][typography]" )
{
  using namespace sicnu::agent::cartography;
  TextFitRequest request;
  request.text = "title";
  request.boxWidthMm = 50.0;
  request.boxHeightMm = 5.0;
  request.fontPt = 9.0;
  request.policy = "overflow_report";
  const Json::Value json = textFitReportToJson( fitTextIntoBox( request ) );
  for ( const char *key : { "fits", "font_pt", "font_policy", "used_width_mm",
                            "used_height_mm", "overflow_width_mm", "overflow_height_mm",
                            "truncated", "policy", "lines", "diagnostics" } )
    REQUIRE( json.isMember( key ) );
  REQUIRE( json["lines"].size() == 1 );
  REQUIRE( json["lines"][0].asString() == "title" );
}

TEST_CASE( "P7 style: diverging scheme requires and validates a center",
           "[platform7][style]" )
{
  Json::Value style( Json::objectValue );
  style["id"] = "p7-diverging";
  style["version"] = 1;
  style["applies_to"] = "raster";
  style["raster"]["renderertype"] = "singleband_pseudocolor";
  style["raster"]["classification"]["mode"] = "discrete";
  style["raster"]["classification"]["scheme"] = "diverging";
  Json::Value classes( Json::arrayValue );
  double lo = -1.0;
  for ( int i = 0; i < 4; ++i )
  {
    Json::Value entry( Json::objectValue );
    entry["min"] = lo;
    entry["max"] = lo + 0.5;
    entry["label"] = "c" + std::to_string( i );
    entry["color"] = "#2233aa";
    classes.append( entry );
    lo += 0.5;
  }
  style["raster"]["classification"]["classes"] = classes;

  // Diverging without center → rejected (validateStyleSpec embeds the
  // semantics check).
  std::vector<std::string> problems = validateStyleSpec( style );
  bool sawCenter = false;
  for ( const auto &problem : problems )
    sawCenter = sawCenter || problem.find( "diverging scheme requires" ) != std::string::npos;
  REQUIRE( sawCenter );

  // Center outside the class range → rejected.
  style["raster"]["classification"]["center"] = 5.0;
  problems = validateStyleSpec( style );
  sawCenter = false;
  for ( const auto &problem : problems )
    sawCenter = sawCenter || problem.find( "outside the declared class range" ) != std::string::npos;
  REQUIRE( sawCenter );

  // Deterministic repair: zero-bracketing classes derive center = 0.
  style["raster"]["classification"].removeMember( "center" );
  std::vector<std::string> decisions;
  REQUIRE( repairStyleSemantics( style, &decisions ) );
  REQUIRE( style["raster"]["classification"]["center"].asDouble() == Catch::Approx( 0.0 ) );
  REQUIRE( decisions.size() == 1 );
  REQUIRE( validateStyleSpec( style ).empty() );

  // A class structure that does not bracket zero cannot be repaired.
  Json::Value positive( Json::objectValue );
  positive["id"] = "p7-positive";
  positive["version"] = 1;
  positive["applies_to"] = "raster";
  positive["raster"]["renderertype"] = "singleband_pseudocolor";
  positive["raster"]["classification"]["mode"] = "discrete";
  positive["raster"]["classification"]["scheme"] = "diverging";
  Json::Value posClasses( Json::arrayValue );
  Json::Value entry( Json::objectValue );
  entry["min"] = 0.2;
  entry["max"] = 0.9;
  entry["label"] = "c";
  entry["color"] = "#aa2233";
  posClasses.append( entry );
  positive["raster"]["classification"]["classes"] = posClasses;
  REQUIRE_FALSE( repairStyleSemantics( positive, nullptr ) );
}

TEST_CASE( "P7 style: categorical scheme contradicts continuous mode",
           "[platform7][style][validation]" )
{
  Json::Value style( Json::objectValue );
  style["id"] = "p7-cat";
  style["version"] = 1;
  style["applies_to"] = "raster";
  style["raster"]["renderertype"] = "paletted";
  style["raster"]["classification"]["mode"] = "continuous";
  style["raster"]["classification"]["scheme"] = "categorical";
  const std::vector<std::string> problems = validateStyleSpec( style );
  bool saw = false;
  for ( const auto &problem : problems )
    saw = saw || problem.find( "categorical scheme contradicts" ) != std::string::npos;
  REQUIRE( saw );
}

TEST_CASE( "P7 style: nodata and uncertainty blocks are shape-validated",
           "[platform7][style][validation]" )
{
  Json::Value style( Json::objectValue );
  style["id"] = "p7-nodata";
  style["version"] = 1;
  style["applies_to"] = "raster";
  style["raster"]["renderertype"] = "singleband_gray";
  style["raster"]["nodata"]["value"] = "not-a-number";
  style["raster"]["nodata"]["transparent"] = "yes";
  style["raster"]["nodata"]["label"] = 7;
  style["uncertainty"]["kind"] = "vibes";
  style["uncertainty"]["level"] = 1.5;
  const std::vector<std::string> problems = validateStyleSpec( style );
  int saw = 0;
  for ( const auto &problem : problems )
  {
    saw += problem.find( "nodata.value must be numeric" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "nodata.transparent must be a boolean" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "nodata.label must be a string" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "uncertainty.kind must be" ) != std::string::npos ? 1 : 0;
    saw += problem.find( "uncertainty.level must be" ) != std::string::npos ? 1 : 0;
  }
  REQUIRE( saw == 5 );

  Json::Value good( Json::objectValue );
  good["id"] = "p7-nodata-good";
  good["version"] = 1;
  good["applies_to"] = "raster";
  good["raster"]["renderertype"] = "singleband_gray";
  good["raster"]["nodata"]["value"] = -9999.0;
  good["raster"]["nodata"]["transparent"] = true;
  good["raster"]["nodata"]["label"] = "NoData";
  good["uncertainty"]["kind"] = "confidence_interval";
  good["uncertainty"]["level"] = 0.9;
  REQUIRE( validateStyleSpec( good ).empty() );
}

TEST_CASE( "P7 style: applicability refuses SAR/multiband and DEM-without-stretch",
           "[platform7][style]" )
{
  Json::Value sar( Json::objectValue );
  sar["id"] = "p7-sar-mb";
  sar["version"] = 1;
  sar["applies_to"] = "raster";
  sar["raster"]["renderertype"] = "multiband_color";
  Json::Value dataset( Json::objectValue );
  dataset["kind"] = "raster";
  dataset["modality"] = "sar";
  dataset["band_count"] = 1;
  const std::vector<std::string> problems = checkStyleApplicability( sar, dataset );
  bool sawSar = false;
  bool sawBands = false;
  for ( const auto &problem : problems )
  {
    sawSar = sawSar || problem.find( "SAR backscatter is single-band" ) != std::string::npos;
    sawBands = sawBands || problem.find( "needs at least 3 bands" ) != std::string::npos;
  }
  REQUIRE( sawSar );
  REQUIRE( sawBands );

  Json::Value dem( Json::objectValue );
  dem["id"] = "p7-dem-bare";
  dem["version"] = 1;
  dem["applies_to"] = "raster";
  dem["raster"]["renderertype"] = "singleband_gray";
  Json::Value demData( Json::objectValue );
  demData["kind"] = "raster";
  demData["modality"] = "dem";
  const std::vector<std::string> demProblems = checkStyleApplicability( dem, demData );
  bool sawDem = false;
  for ( const auto &problem : demProblems )
    sawDem = sawDem || problem.find( "DEM/terrain styles should declare" ) != std::string::npos;
  REQUIRE( sawDem );

  // Declared stretch satisfies the DEM requirement.
  dem["raster"]["stretch"]["type"] = "minmax";
  REQUIRE( checkStyleApplicability( dem, demData ).empty() );
}

TEST_CASE( "P7 style: contrast checks report sub-floor pairs deterministically",
           "[platform7][style]" )
{
  Json::Value style( Json::objectValue );
  style["id"] = "p7-contrast";
  style["version"] = 1;
  style["applies_to"] = "raster";
  style["raster"]["renderertype"] = "singleband_pseudocolor";
  Json::Value classes( Json::arrayValue );
  const char *colors[] = { "#111111", "#1a1a1a", "#eeeeee" }; // first pair too close
  for ( int i = 0; i < 3; ++i )
  {
    Json::Value entry( Json::objectValue );
    entry["min"] = i * 1.0;
    entry["max"] = i * 1.0 + 1.0;
    entry["label"] = "c" + std::to_string( i );
    entry["color"] = colors[i];
    classes.append( entry );
  }
  style["raster"]["classification"]["classes"] = classes;

  const std::vector<std::string> warnings = checkStyleContrast( style, nullptr );
  REQUIRE( warnings.size() == 1 );
  REQUIRE( warnings[0].find( "adjacent class colors" ) != std::string::npos );
  REQUIRE( warnings[0].find( "#111111 / #1a1a1a" ) != std::string::npos );

  // Distinct-enough consecutive pairs pass clean (#111111 -> #3a3a3a has
  // ratio ~1.67, #3a3a3a -> #eeeeee ~9.8).
  style["raster"]["classification"]["classes"][1]["color"] = "#3a3a3a";
  REQUIRE( checkStyleContrast( style, nullptr ).empty() );
}

TEST_CASE( "P7 style: shipped catalog keeps validating (compat)",
           "[platform7][style][compat]" )
{
  StyleRegistry &registry = StyleRegistry::instance();
  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();
  REQUIRE( registry.loadProblems().isEmpty() );
  int validated = 0;
  for ( const auto &style : registry.styles() )
  {
    ++validated;
    INFO( style["id"].asString() );
    REQUIRE( validateStyleSpec( style ).empty() );
  }
  REQUIRE( validated >= 10 );
}

namespace {

Json::Value confusionChart()
{
  Json::Value chart( Json::objectValue );
  chart["kind"] = "accuracy_summary";
  chart["title"] = "classification accuracy";
  chart["binding"]["mode"] = "inline";
  Json::Value labels( Json::arrayValue );
  labels.append( "water" );
  labels.append( "forest" );
  Json::Value rows( Json::arrayValue );
  // rows = reference, columns = predicted.
  Json::Value row0( Json::arrayValue );
  row0.append( 40 );
  row0.append( 10 );
  Json::Value row1( Json::arrayValue );
  row1.append( 5 );
  row1.append( 45 );
  rows.append( row0 );
  rows.append( row1 );
  chart["binding"]["matrix"]["labels"] = labels;
  chart["binding"]["matrix"]["rows"] = rows;
  chart["width_px"] = 480;
  chart["height_px"] = 320;
  return chart;
}

} // namespace

TEST_CASE( "P7 charts: accuracy_summary validates the confusion binding",
           "[platform7][charts]" )
{
  // Valid square matrix binding.
  const Json::Value chart = confusionChart();
  REQUIRE( validateChartSpec( chart ).empty() );

  // Non-square → rejected.
  Json::Value ragged = chart;
  ragged["binding"]["matrix"]["labels"] = Json::Value( Json::arrayValue );
  ragged["binding"]["matrix"]["labels"].append( "only" );
  bool saw = false;
  for ( const auto &problem : validateChartSpec( ragged ) )
    saw = saw || problem.find( "square confusion matrix" ) != std::string::npos;
  REQUIRE( saw );

  // vector_expression binding → rejected (derives from inline data only).
  Json::Value native = chart;
  native["binding"].removeMember( "matrix" );
  native["binding"]["mode"] = "vector_expression";
  native["binding"]["layer"] = "layer-1";
  saw = false;
  for ( const auto &problem : validateChartSpec( native ) )
    saw = saw || problem.find( "inline data only" ) != std::string::npos;
  REQUIRE( saw );

  // Registry roundtrip + render smoke (PNG painted by the QPainter path).
  ChartRegistry &registry = ChartRegistry::instance();
  registry.clear();
  QString error;
  const QString id = registry.createChart( chart, &error );
  REQUIRE( !id.isEmpty() );
  const QString path = QDir::temp().filePath( QStringLiteral( "p7-accuracy-summary.png" ) );
  REQUIRE( renderChartToFile( registry.find( id ), path, &error ) );
  REQUIRE( QFileInfo( path ).exists() );
  REQUIRE( QFileInfo( path ).size() > 1000 );
  QFile::remove( path );
}

TEST_CASE( "P7 charts: dual axis is rejected without semantic justification",
           "[platform7][charts]" )
{
  Json::Value chart( Json::objectValue );
  chart["kind"] = "line";
  chart["binding"]["mode"] = "inline";
  Json::Value data( Json::arrayValue );
  Json::Value point( Json::objectValue );
  point["label"] = "2020";
  point["value"] = 1.5;
  data.append( point );
  chart["binding"]["data"] = data;

  // Unjustified dual axis → rejected.
  chart["axes"]["secondary"]["data"] = Json::Value( Json::arrayValue );
  bool saw = false;
  for ( const auto &problem : validateChartSpec( chart ) )
    saw = saw || problem.find( "dual axis requires axes.secondary.justification" ) !=
                    std::string::npos;
  REQUIRE( saw );

  // Justified dual axis on a line chart → accepted.
  chart["axes"]["secondary"]["justification"] =
    "secondary series is NDVI (unitless) against rainfall (mm)";
  chart["axes"]["secondary"]["data"][0] = point;
  REQUIRE( validateChartSpec( chart ).empty() );

  // Dual axis is not defined for non line/scatter kinds.
  Json::Value pie = chart;
  pie["kind"] = "pie";
  saw = false;
  for ( const auto &problem : validateChartSpec( pie ) )
    saw = saw || problem.find( "only defined for line/scatter" ) != std::string::npos;
  REQUIRE( saw );
}

TEST_CASE( "P7 components: professional set loads with roles, bounds and tokens",
           "[platform7][components]" )
{
  ComponentRegistry &registry = ComponentRegistry::instance();
  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();
  if ( !registry.loadProblems().isEmpty() )
    FAIL( registry.loadProblems().join( "; " ).toStdString() );

  const Json::Value all = registry.components();
  REQUIRE( all.size() >= 57 );

  // Every new component resolves and carries the required surfaces.
  const std::vector<std::string> expected = {
    "text/uncertainty-note", "statistics/panel", "accuracy/report",
    "chart/class-composition", "publication/footer",
  };
  for ( const std::string &id : expected )
  {
    INFO( id );
    const Json::Value component = registry.find( QString::fromStdString( id ) );
    REQUIRE_FALSE( component.isNull() );
    REQUIRE( component["semantic_roles"].isArray() );
    REQUIRE_FALSE( component["semantic_roles"].empty() );
    REQUIRE( component["layout_constraints"].isObject() );
    REQUIRE( component["compatibility"].isObject() );
    REQUIRE( component["style_tokens"].isArray() );
    REQUIRE_FALSE( component["style_tokens"].empty() );
  }

  // New categories map to the labels collection.
  REQUIRE( collectionForCategory( "statistics" ) == "labels" );
  REQUIRE( collectionForCategory( "accuracy" ) == "labels" );
  REQUIRE( registry.byCategory( QLatin1String( "statistics" ) ).size() == 1 );
  REQUIRE( registry.byCategory( QLatin1String( "accuracy" ) ).size() == 1 );
}

TEST_CASE( "P7 components: style_tokens paths resolve against the default token set",
           "[platform7][components][drift]" )
{
  ComponentRegistry &registry = ComponentRegistry::instance();
  registry.setDirectory( QStringLiteral( SICNU_CARTOGRAPHY_DATA_DIR ) );
  registry.reload();
  const Json::Value tokens = resolveTokenSet( Json::Value() ); // default set
  REQUIRE_FALSE( tokens.isNull() );

  std::vector<std::string> dangling;
  for ( const auto &component : registry.components() )
  {
    if ( !component.isMember( "style_tokens" ) )
      continue;
    for ( const auto &path : component["style_tokens"] )
    {
      if ( !path.isString() )
        continue;
      if ( tokenValue( tokens, path.asString() ).isNull() )
        dangling.push_back( component["id"].asString() + " -> " + path.asString() );
    }
  }
  REQUIRE( dangling.empty() );
}

TEST_CASE( "P7 components: style_tokens shape is validated",
           "[platform7][components][validation]" )
{
  Json::Value descriptor( Json::objectValue );
  descriptor["id"] = "test/tokened";
  descriptor["category"] = "text";
  descriptor["version"] = 1;
  descriptor["style_tokens"] = "typography.styles.body"; // not an array
  std::vector<std::string> problems = validateComponentDescriptor( descriptor );
  bool saw = false;
  for ( const auto &problem : problems )
    saw = saw || problem.find( "style_tokens must be an array" ) != std::string::npos;
  REQUIRE( saw );

  descriptor["style_tokens"] = Json::Value( Json::arrayValue );
  descriptor["style_tokens"].append( "" );
  problems = validateComponentDescriptor( descriptor );
  saw = false;
  for ( const auto &problem : problems )
    saw = saw || problem.find( "non-empty dotted token path" ) != std::string::npos;
  REQUIRE( saw );
}

namespace {

bool hasIssue( const Json::Value &report, const std::string &code, const std::string &idPart )
{
  for ( const auto &issue : report["issues"] )
    if ( issue["code"].asString() == code &&
         issue["message"].asString().find( idPart ) != std::string::npos )
      return true;
  return false;
}

} // namespace

TEST_CASE( "P7 preflight: declarative layer visibility and reference rules",
           "[platform7][preflight]" )
{
  Json::Value spec = makeMapSpec( "p7-layers", Json::Value() );
  Json::Value frame( Json::objectValue );
  frame["id"] = "map-1";
  Json::Value rect( Json::arrayValue );
  rect.append( 12.0 );
  rect.append( 24.0 );
  rect.append( 200.0 );
  rect.append( 160.0 );
  frame["rect_mm"] = rect;
  Json::Value layers( Json::arrayValue );
  layers.append( "layer-water" );
  frame["layers"] = layers;
  spec["map_frames"].append( frame );
  spec["titles"].append( rectItem( "t1", 12.0, 6.0, 120.0, 12.0 ) );
  spec["legends"].append( rectItem( "lg", 220.0, 30.0, 60.0, 80.0 ) );
  spec["scale_bars"].append( rectItem( "sb", 14.0, 190.0, 50.0, 8.0 ) );
  spec["north_arrows"].append( rectItem( "na", 270.0, 30.0, 12.0, 12.0 ) );
  spec["source_notes"].append( rectItem( "sn", 180.0, 194.0, 90.0, 8.0 ) );

  // Invisible layer declared + referenced → warned.
  Json::Value water( Json::objectValue );
  water["id"] = "layer-water";
  water["visible"] = false;
  spec["layers"].append( water );
  Json::Value report = preflightMapSpec( spec );
  REQUIRE( hasIssue( report, "MAP_INVISIBLE_LAYER", "layer-water" ) );
  REQUIRE_FALSE( hasIssue( report, "MAP_LAYER_UNREFERENCED", "layer-water" ) );

  // An unreferenced declarative layer → warned, repair attaches it to the
  // main frame.
  Json::Value extra( Json::objectValue );
  extra["id"] = "layer-forest";
  spec["layers"].append( extra );
  report = preflightMapSpec( spec );
  REQUIRE( hasIssue( report, "MAP_LAYER_UNREFERENCED", "layer-forest" ) );

  const int repaired = repairMapSpec( spec, report );
  REQUIRE( repaired >= 1 );
  bool attached = false;
  for ( const auto &ref : spec["map_frames"][0]["layers"] )
    attached = attached || ref.asString() == "layer-forest";
  REQUIRE( attached );
  // Re-preflight: the unreferenced finding is gone.
  report = preflightMapSpec( spec );
  REQUIRE_FALSE( hasIssue( report, "MAP_LAYER_UNREFERENCED", "layer-forest" ) );
}

TEST_CASE( "P7 preflight: legend classes contradicting the referenced style warn",
           "[platform7][preflight]" )
{
  Json::Value spec = makeMapSpec( "p7-legend-mismatch", Json::Value() );
  Json::Value legend( Json::objectValue );
  legend["id"] = "lg-1";
  legend["style_ref"] = "style.landcover-classes";
  Json::Value classes( Json::arrayValue );
  classes.append( "water" );
  classes.append( "forest" );
  classes.append( "not-a-real-class" );
  legend["classes"] = classes;
  spec["legends"].append( legend );

  const Json::Value report = preflightMapSpec( spec );
  REQUIRE( hasIssue( report, "MAP_LEGEND_MISMATCH", "not-a-real-class" ) );

  // Matching labels pass clean.
  Json::Value ok = makeMapSpec( "p7-legend-match", Json::Value() );
  Json::Value okLegend( Json::objectValue );
  okLegend["id"] = "lg-ok";
  okLegend["style_ref"] = "style.landcover-classes";
  Json::Value okClasses( Json::arrayValue );
  okClasses.append( "water" );
  okClasses.append( "forest" );
  okClasses.append( "cropland" );
  okClasses.append( "urban" );
  okLegend["classes"] = okClasses;
  ok["legends"].append( okLegend );
  REQUIRE_FALSE( hasIssue( preflightMapSpec( ok ), "MAP_LEGEND_MISMATCH", "lg-ok" ) );
}

TEST_CASE( "P7 preflight: wrap-aware overflow catches multi-line clipping",
           "[platform7][preflight]" )
{
  Json::Value spec = makeMapSpec( "p7-wrap", Json::Value() );
  Json::Value title( Json::objectValue );
  title["id"] = "t-wrap";
  title["text"] = "\xE7\xAC\xAC\xE4\xB8\x80\xE8\xA1\x8C\xE5\xBE\x88\xE9\x95\xBF\n\xE7\xAC\xAC\xE4\xBA\x8C\xE8\xA1\x8C\xE4\xB9\x9F\xE5\xBE\x88\xE9\x95\xBF";
  Json::Value rect( Json::arrayValue );
  rect.append( 10.0 );
  rect.append( 6.0 );
  rect.append( 30.0 );
  rect.append( 8.0 ); // two lines at 9 pt need ~8 mm; force overflow via width 30→wraps
  title["rect_mm"] = rect;
  Json::Value font( Json::objectValue );
  font["size_pt"] = 9.0;
  title["font"] = font;
  spec["titles"].append( title );

  const Json::Value report = preflightMapSpec( spec );
  REQUIRE( hasIssue( report, "MAP_TEXT_WRAP_OVERFLOW", "t-wrap" ) );

  // Repair path: the deterministic text repair applies (rect/font adjust).
  const int repaired = repairMapSpec( spec, report );
  REQUIRE( repaired >= 1 );
}

TEST_CASE( "P7 preflight: rule catalog lists the 7.0 rules",
           "[platform7][preflight][catalog]" )
{
  const Json::Value catalog = preflightRuleCatalog();
  REQUIRE( catalog.isArray() );
  int found = 0;
  for ( const auto &rule : catalog )
  {
    const std::string code = rule["code"].asString();
    found += code == "MAP_INVISIBLE_LAYER" ? 1 : 0;
    found += code == "MAP_LAYER_UNREFERENCED" ? 1 : 0;
    found += code == "MAP_LEGEND_MISMATCH" ? 1 : 0;
    found += code == "MAP_TEXT_WRAP_OVERFLOW" ? 1 : 0;
    if ( code == "MAP_TEXT_WRAP_OVERFLOW" )
      REQUIRE( rule["repairable"].asBool() );
  }
  REQUIRE( found == 4 );
}

namespace {

// Golden structural digest of the fixture below (pinned after a verified
// run; update ONLY with re-verified geometry).
constexpr const char *kGoldenDigest = "PENDING_FIRST_RUN";

} // namespace

TEST_CASE( "P7 visual: structural digest is stable, sensitive and order-free",
           "[platform7][visual]" )
{
  auto build = []() {
    Json::Value spec = makeMapSpec( "p7-digest", Json::Value() );
    spec["titles"].append( rectItem( "t1", 10.0, 6.0, 120.0, 12.0 ) );
    spec["legends"].append( rectItem( "lg", 220.0, 30.0, 60.0, 80.0 ) );
    spec["scale_bars"].append( rectItem( "sb", 14.0, 190.0, 50.0, 8.0 ) );
    return spec;
  };

  // Identical specs -> identical digest (byte-stable known answer).
  const std::string digestA = structuralDigest( build() );
  const std::string digestB = structuralDigest( build() );
  REQUIRE( digestA == digestB );
  REQUIRE( digestA.size() == 64 ); // SHA-256 hex
  REQUIRE( std::string( kGoldenDigest ) != "PENDING_FIRST_RUN" );
  REQUIRE( digestA == kGoldenDigest );

  // Moving one item changes the digest.
  Json::Value moved = build();
  moved["legends"][0]["rect_mm"][0] = 221.0;
  REQUIRE( structuralDigest( moved ) != digestA );

  // Declaration order does not (canonical, id-sorted entries).
  Json::Value reordered = build();
  Json::Value titles = reordered["titles"];
  reordered.removeMember( "titles" );
  Json::Value rotated( Json::arrayValue );
  for ( int i = static_cast<int>( titles.size() ) - 1; i >= 0; --i )
    rotated.append( titles[i] );
  reordered["titles"] = rotated;
  REQUIRE( structuralDigest( reordered ) == digestA );

  // Page-size changes participate.
  Json::Value pageChanged = build();
  pageChanged["page"]["width_mm"] = 298.0;
  REQUIRE( structuralDigest( pageChanged ) != digestA );

  // Double resolve: geometry-affecting solving is digest-visible and
  // deterministic (the pinned multi-fixpoint policy).
  Json::Value resolvedOnce = build();
  Json::Value softBelow = constraint( "s1", "below", "t1", "lg", 2.0 );
  markSoft( softBelow, 50, 1.0 );
  resolvedOnce["constraints"].append( softBelow );
  Json::Value resolvedTwice = resolvedOnce;
  resolveComposition( resolvedOnce, 10.0 );
  resolveComposition( resolvedTwice, 10.0 );
  REQUIRE( structuralDigest( resolvedOnce ) == structuralDigest( resolvedTwice ) );
}

TEST_CASE( "P7 solver: soft failure downgrades the objective, never convergence",
           "[platform7][solver]" )
{
  // A soft constraint that can never be evaluated (invalid direction →
  // permanent failure) counts into the violated objective — but softs never
  // flip `converged` (that is reserved for hard failures). A healthy soft
  // alongside it contributes its weight on the satisfied side.
  Json::Value spec = makeMapSpec( "p7-soft", Json::Value() );
  spec["titles"].append( rectItem( "t1", 10.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 10.0, 30.0, 40.0, 30.0 ) );
  Json::Value ok = constraint( "s-ok", "below", "t1", "lg", 2.0 );
  markSoft( ok, 50, 2.0 );
  spec["constraints"].append( ok );
  Json::Value broken = constraint( "s-broken", "stack", "t1", "lg" );
  broken["direction"] = "diagonal";
  markSoft( broken, 50, 3.0 );
  spec["constraints"].append( broken );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  Json::Value report = result.toJson();

  REQUIRE( result.converged );
  REQUIRE( result.constraintsSolved == 0 ); // no hard constraints declared
  REQUIRE( result.softTotal == 2 );
  REQUIRE( result.softSatisfied == 1 );
  REQUIRE( result.satisfiedWeight == Catch::Approx( 2.0 ) );
  REQUIRE( result.violatedWeight == Catch::Approx( 3.0 ) );
  REQUIRE( result.violated.size() == 1 );
  REQUIRE( result.violated[0].cid == "s-broken" );
  REQUIRE( report["soft_total"].asInt() == 2 );
  REQUIRE( report["violated_weight"].asDouble() == Catch::Approx( 3.0 ) );
  REQUIRE( report["fixpoint_policy"].asString().find( "canonical" ) != std::string::npos );
}

TEST_CASE( "P7 solver: soft satisfied contributes its weight",
           "[platform7][solver]" )
{
  Json::Value spec = makeMapSpec( "p7-soft-ok", Json::Value() );
  spec["titles"].append( rectItem( "t1", 10.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 10.0, 22.0, 40.0, 30.0 ) );
  Json::Value soft = constraint( "s1", "below", "t1", "lg", 2.0 );
  markSoft( soft, 50, 3.0 );
  spec["constraints"].append( soft );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  // lg already sits exactly 2 mm below t1 → satisfied without movement.
  REQUIRE( result.converged );
  REQUIRE( result.softTotal == 1 );
  REQUIRE( result.softSatisfied == 1 );
  REQUIRE( result.satisfiedWeight == Catch::Approx( 3.0 ) );
  REQUIRE( result.violatedWeight == Catch::Approx( 0.0 ) );
}

TEST_CASE( "P7 solver: priority decides scarce space; loser is rejected with the winner named",
           "[platform7][solver]" )
{
  // Two soft constraints pull the same follower to contradictory targets;
  // the higher priority keeps the geometry, the lower is REVERTED and
  // reported (never silently kept, never order-dependent).
  for ( int permutation = 0; permutation < 2; ++permutation )
  {
    Json::Value spec = makeMapSpec( "p7-priority", Json::Value() );
    spec["titles"].append( rectItem( "top-ref", 20.0, 10.0, 60.0, 10.0 ) );
    spec["source_notes"].append( rectItem( "bottom-ref", 20.0, 180.0, 60.0, 10.0 ) );
    spec["legends"].append( rectItem( "box", 20.0, 100.0, 40.0, 20.0 ) );
    Json::Value high = constraint( "s-high", "below", "top-ref", "box", 2.0 );
    markSoft( high, 90, 1.0 );
    Json::Value low = constraint( "s-low", "above", "bottom-ref", "box", 2.0 );
    markSoft( low, 10, 5.0 );
    spec["constraints"].append( permutation == 0 ? high : low );
    spec["constraints"].append( permutation == 0 ? low : high );

    const CompositionResult result = resolveComposition( spec, 10.0 );
    const Json::Value &box = itemById( spec, "legends", "box" );
    // s-high (priority 90) wins despite s-low's larger weight.
    REQUIRE( box["rect_mm"][1].asDouble() == Catch::Approx( 22.0 ) );
    REQUIRE( result.softTotal == 2 );
    REQUIRE( result.softSatisfied == 1 );
    REQUIRE( result.satisfiedWeight == Catch::Approx( 1.0 ) );
    REQUIRE( result.violatedWeight == Catch::Approx( 5.0 ) );
    REQUIRE( decisionsContain( result.toJson(), "s-high", "applied" ) );
    REQUIRE( decisionsContain( result.toJson(), "s-low", "rejected" ) );
    const std::string reason = rejectionReason( result.toJson(), "s-low" );
    REQUIRE( reason.find( "s-high" ) != std::string::npos );
    // Declaration order must not decide the outcome (multi-fixpoint policy).
    REQUIRE( result.fixpointPolicy.find( "priority desc" ) != std::string::npos );
  }
}

TEST_CASE( "P7 solver: soft application cannot break a hard constraint",
           "[platform7][solver]" )
{
  Json::Value spec = makeMapSpec( "p7-soft-hard", Json::Value() );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
  spec["scale_bars"].append( rectItem( "sb", 20.0, 80.0, 30.0, 8.0 ) );
  // Hard: legend must match the scale bar's height.
  Json::Value hard = constraint( "h1", "match_height", "sb", "lg" );
  spec["constraints"].append( hard );
  // Soft: pull the legend up (would resize it, breaking h1's match).
  Json::Value soft = constraint( "s1", "above", "t1", "lg", 2.0 );
  markSoft( soft, 80, 4.0 );
  spec["constraints"].append( soft );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  REQUIRE( result.converged );
  // Hard solved and untouched by the soft phase.
  REQUIRE( result.constraintsSolved == 1 );
  REQUIRE( decisionsContain( result.toJson(), "h1", "applied" ) );
  // The soft was applied only if it kept h1 satisfied — moving above t1
  // (y = 22) does not change the height, so it is applied; a size-breaking
  // variant would be rejected. Either way h1 must hold afterwards.
  const Json::Value &lg = itemById( spec, "legends", "lg" );
  REQUIRE( lg["rect_mm"][3].asDouble() == Catch::Approx( 8.0 ) );
}

TEST_CASE( "P7 solver: unsat core names the minimal conflicting subset",
           "[platform7][solver]" )
{
  // Two hard constraints fight over the same follower with contradictory
  // targets — the relaxation cannot converge, and the core search must
  // identify the pair.
  Json::Value spec = makeMapSpec( "p7-core", Json::Value() );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  spec["source_notes"].append( rectItem( "note", 20.0, 180.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "box", 20.0, 100.0, 40.0, 20.0 ) );
  spec["constraints"].append( constraint( "c1", "below", "t1", "box", 2.0 ) );
  spec["constraints"].append( constraint( "c2", "above", "note", "box", 2.0 ) );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  REQUIRE_FALSE( result.converged ); // the pair cannot both hold
  REQUIRE_FALSE( result.unsatCores.isNull() );

  bool sawCore = false;
  for ( const auto &entry : result.unsatCores )
  {
    if ( entry["constraint"].asString() != "c1" )
      continue;
    sawCore = true;
    REQUIRE( entry["core"].isArray() );
    std::vector<std::string> members;
    for ( const auto &member : entry["core"] )
      members.push_back( member.asString() );
    REQUIRE( members.size() == 2 );
    REQUIRE( members[0] == "c1" );
    REQUIRE( members[1] == "c2" );
    REQUIRE( entry["explanation"].asString().find( "satisfiable" ) != std::string::npos );
  }
  REQUIRE( sawCore );
}

TEST_CASE( "P7 solver: anchor authority is reported as a decision",
           "[platform7][solver]" )
{
  Json::Value spec = makeMapSpec( "p7-anchor", Json::Value() );
  Json::Value anchored = rectItem( "lg", 200.0, 100.0, 40.0, 30.0 );
  Json::Value anchor( Json::objectValue );
  anchor["edge"] = "bottom-right";
  anchored["anchor"] = anchor;
  spec["legends"].append( anchored );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  Json::Value hard = constraint( "h1", "below", "t1", "lg", 2.0 );
  spec["constraints"].append( hard );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  REQUIRE( result.converged );
  // The anchor wins: the constraint is disabled with a targeted report and
  // the anchored geometry is preserved.
  REQUIRE( decisionsContain( result.toJson(), "h1", "anchor_wins" ) );
  bool sawNote = false;
  for ( const auto &note : result.unsatisfied )
    sawNote = sawNote || note.find( "anchor wins" ) != std::string::npos;
  REQUIRE( sawNote );
  const Json::Value &lg = itemById( spec, "legends", "lg" );
  REQUIRE( lg["rect_mm"][0].asDouble() == Catch::Approx( 200.0 ) );
}

TEST_CASE( "P7 solver: declaration permutations produce identical geometry",
           "[platform7][solver]" )
{
  // Over-determined (multi-fixpoint) system: 6.0 was deterministic per input
  // but order-sensitive; 7.0's canonical policy must produce identical
  // geometry under declaration permutations.
  auto build = []( int permutation ) {
    Json::Value spec = makeMapSpec( "p7-perm", Json::Value() );
    spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
    spec["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
    spec["scale_bars"].append( rectItem( "sb", 20.0, 80.0, 30.0, 8.0 ) );
    spec["north_arrows"].append( rectItem( "na", 250.0, 20.0, 15.0, 15.0 ) );
    Json::Value c1 = constraint( "c1", "align", "t1", "lg" );
    c1["edge"] = "left";
    Json::Value c2 = constraint( "c2", "match_width", "t1", "lg" );
    Json::Value c3 = constraint( "c3", "match_height", "sb", "lg" );
    Json::Value c4 = constraint( "c4", "right_of", "sb", "na", 6.0 );
    Json::Value *order[4] = { &c1, &c2, &c3, &c4 };
    const int permuted[2][4] = { { 0, 1, 2, 3 }, { 3, 2, 1, 0 } };
    for ( int i = 0; i < 4; ++i )
      spec["constraints"].append( *order[permuted[permutation][i]] );
    resolveComposition( spec, 10.0 );
    return spec;
  };
  const Json::Value a = build( 0 );
  const Json::Value b = build( 1 );
  for ( const char *collection : { "titles", "legends", "scale_bars", "north_arrows" } )
  {
    const Json::Value &ia = a[collection][0];
    const Json::Value &ib = b[collection][0];
    for ( int k = 0; k < 4; ++k )
      REQUIRE( ia["rect_mm"][k].asDouble() == Catch::Approx( ib["rect_mm"][k].asDouble() ) );
  }
}

TEST_CASE( "P7 solver: decisions ledger is bounded and ordered",
           "[platform7][solver]" )
{
  Json::Value spec = makeMapSpec( "p7-ledger", Json::Value() );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
  Json::Value soft = constraint( "s1", "below", "t1", "lg", 2.0 );
  markSoft( soft, 50, 1.0 );
  spec["constraints"].append( soft );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  REQUIRE( static_cast<int>( result.decisions.size() ) <= kMaxDecisions );
  int expectedOrder = 0;
  for ( const auto &decision : result.decisions )
    REQUIRE( decision.order == expectedOrder++ );
}

TEST_CASE( "P7 validation: v4 constraint surface is closed and bounded",
           "[platform7][solver][validation]" )
{
  Json::Value spec = makeMapSpec( "p7-validate", Json::Value() );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );

  {
    Json::Value bad = makeMapSpec( "p7-validate-1", Json::Value() );
    bad["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
    bad["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
    Json::Value c = constraint( "c1", "below", "t1", "lg", 2.0 );
    c["hardness"] = "rigid";
    bad["constraints"].append( c );
    const std::vector<std::string> problems = validateMapSpec( bad );
    bool saw = false;
    for ( const auto &problem : problems )
      saw = saw || problem.find( "hardness must be" ) != std::string::npos;
    REQUIRE( saw );
  }
  {
    Json::Value bad = makeMapSpec( "p7-validate-2", Json::Value() );
    bad["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
    bad["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
    Json::Value c = constraint( "c1", "below", "t1", "lg", 2.0 );
    c["priority"] = 101;
    bad["constraints"].append( c );
    const std::vector<std::string> problems = validateMapSpec( bad );
    bool saw = false;
    for ( const auto &problem : problems )
      saw = saw || problem.find( "priority must be an integer in [0, 100]" ) != std::string::npos;
    REQUIRE( saw );
  }
  {
    Json::Value bad = makeMapSpec( "p7-validate-3", Json::Value() );
    bad["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
    bad["legends"].append( rectItem( "lg", 20.0, 40.0, 40.0, 20.0 ) );
    Json::Value c = constraint( "c1", "below", "t1", "lg", 2.0 );
    c["weight"] = 5.0; // weight on a (default-hard) constraint is a contradiction
    bad["constraints"].append( c );
    const std::vector<std::string> problems = validateMapSpec( bad );
    bool saw = false;
    for ( const auto &problem : problems )
      saw = saw || problem.find( "only meaningful on hardness" ) != std::string::npos;
    REQUIRE( saw );
  }
  {
    Json::Value good = spec;
    Json::Value c = constraint( "c1", "below", "t1", "lg", 2.0 );
    c["hardness"] = "soft";
    c["priority"] = 100;
    c["weight"] = 0.5;
    good["constraints"].append( c );
    REQUIRE( validateMapSpec( good ).empty() );
  }
}

TEST_CASE( "P7 solver: documents without v4 fields keep the 6.0 contract",
           "[platform7][solver][compat]" )
{
  Json::Value spec = makeMapSpec( "p7-compat", Json::Value() );
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  spec["legends"].append( rectItem( "lg", 25.0, 40.0, 40.0, 20.0 ) );
  Json::Value hard = constraint( "h1", "align", "t1", "lg" );
  hard["edge"] = "left";
  spec["constraints"].append( hard );

  const CompositionResult result = resolveComposition( spec, 10.0 );
  REQUIRE( result.converged );
  REQUIRE( result.constraintsTotal == 1 );
  REQUIRE( result.constraintsSolved == 1 );
  REQUIRE( result.softTotal == 0 );
  REQUIRE( result.softSatisfied == 0 );
  REQUIRE( result.decisions.size() == 1 ); // the h1 application is recorded
  REQUIRE( result.decisions[0].outcome == "applied" );
  // The alignment moved lg to t1's left edge.
  const Json::Value &lg = itemById( spec, "legends", "lg" );
  REQUIRE( lg["rect_mm"][0].asDouble() == Catch::Approx( 20.0 ) );
}

TEST_CASE( "P7 solver: upgradeMapSpec re-stamps v3 documents to v4",
           "[platform7][solver][compat]" )
{
  Json::Value spec = makeMapSpec( "p7-upgrade", Json::Value() );
  spec["spec_version"] = 3;
  spec["titles"].append( rectItem( "t1", 20.0, 10.0, 60.0, 10.0 ) );
  REQUIRE( validateMapSpec( spec ).empty() ); // v3 is still a supported version
  const Json::Value upgraded = upgradeMapSpec( spec );
  REQUIRE( upgraded["spec_version"].asInt() == kMapSpecCurrentVersion );
  REQUIRE( kMapSpecCurrentVersion == 4 );
}
