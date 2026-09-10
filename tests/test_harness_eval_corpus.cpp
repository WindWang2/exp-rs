// tests/test_harness_eval_corpus.cpp
//
// Harness 8.0 (mission Area I): runner for the externalized evaluation
// corpus under data/agent/evals/cases. The corpus is DATA; this binary is
// the deterministic executor + schema guard:
//
//   1. every case file parses; ids are unique; categories are in the closed
//      vocabulary; every referenced tool exists on the live registry;
//   2. every case runs against the live tool surface with runtime-generated
//      fixtures (bounded size, deterministic pixels — no checked-in data);
//   3. expectations assert structure and typed error codes — never prose;
//   4. the expanded corpus stays within a hard bound (<= 400 cases).
//
// Tier A only (tool contracts): engine-execution scenarios remain in
// test_harness_evals.cpp by contract (see data/agent/evals/README.md).

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QTemporaryDir>

#include <cpl_vsi.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <json/json.h>
#include <json/reader.h>

#include <agent/spatial_tools/spatial_tool.h>
#include <agent/harness/recipe_catalog.h>
#include <agent/harness/capability_knowledge.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using sicnu::agent::spatial_tools::SpatialToolRegistry;
using sicnu::agent::spatial_tools::SpatialToolResult;

namespace {

constexpr int kMaxExpandedCases = 400;
constexpr int kMaxFixtureDimension = 32;
constexpr int kMaxFixtureBands = 8;

const char *const kCategories[] = {
  "normal_workflow", "missing_data", "ambiguity", "invalid_science",
  "impossible_task", "multimodal", "context_continuation",
  "anti_hallucination", "map_confirmation", "budget",
};

std::string corpusDirectory()
{
  const QByteArray overrideDir = qgetenv( "SICNU_EVALS_DIR" );
  if ( !overrideDir.isEmpty() )
    return std::string( overrideDir.constData(), overrideDir.size() );
  return std::string( CMAKE_SOURCE_DIR ) + "/data/agent/evals/cases";
}

std::vector<Json::Value> loadCaseFiles( const std::string &dir, std::vector<std::string> &problems )
{
  std::vector<Json::Value> cases;
  QDirIterator it( QString::fromStdString( dir ), { QStringLiteral( "*.json" ) }, QDir::Files );
  while ( it.hasNext() )
  {
    it.next();
    QFile file( it.filePath() );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
      problems.push_back( it.fileName().toStdString() + ": cannot open" );
      continue;
    }
    const QByteArray raw = file.readAll();
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &parsed, &errors ) )
    {
      problems.push_back( it.fileName().toStdString() + ": " + errors );
      continue;
    }
    for ( const Json::Value &entry : parsed.get( "cases", Json::Value( Json::arrayValue ) ) )
      cases.push_back( entry );
  }
  return cases;
}

/// Expands case-level `foreach` variables: {"var": "INDEX", "values": [...]}
/// duplicates the case per value with $INDEX substituted in string leaves.
void substituteVar( Json::Value &node, const std::string &var, const std::string &value )
{
  if ( node.isString() )
  {
    const std::string token = "$" + var;
    std::string text = node.asString();
    size_t at = text.find( token );
    while ( at != std::string::npos )
    {
      text.replace( at, token.size(), value );
      at = text.find( token, at + value.size() );
    }
    node = Json::Value( text );
    return;
  }
  if ( node.isObject() )
  {
    for ( const std::string &key : node.getMemberNames() )
      substituteVar( node[ key ], var, value );
    return;
  }
  if ( node.isArray() )
    for ( Json::ArrayIndex i = 0; i < node.size(); ++i )
      substituteVar( node[ i ], var, value );
}

std::vector<Json::Value> expandCase( const Json::Value &rawCase )
{
  std::vector<Json::Value> expanded{ rawCase };
  const Json::Value &foreach = rawCase.get( "foreach", Json::Value() );
  if ( foreach.isObject() && foreach.isMember( "var" ) && foreach.isMember( "values" ) )
  {
    expanded.clear();
    for ( const Json::Value &value : foreach["values"] )
    {
      Json::Value clone = rawCase;
      substituteVar( clone, foreach["var"].asString(), value.asString() );
      clone.removeMember( "foreach" );
      expanded.push_back( std::move( clone ) );
    }
  }
  return expanded;
}

// --- fixture generation ----------------------------------------------------

std::string writeRasterFixture( const QString &path, const Json::Value &spec )
{
  GDALAllRegister();
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !driver )
    return {};
  const int width = std::min( kMaxFixtureDimension, spec.get( "width", 8 ).asInt() );
  const int height = std::min( kMaxFixtureDimension, spec.get( "height", 8 ).asInt() );
  const Json::Value &bands = spec.get( "bands", Json::Value( Json::arrayValue ) );
  // Band count clamped like the dimensions — a hostile corpus must not be
  // able to generate oversized files (review P3).
  const int bandCount =
    std::clamp( static_cast<int>( bands.size() ), 1, kMaxFixtureBands );
  GDALDataset *ds = driver->Create( path.toUtf8().constData(), width, height, bandCount,
                                    GDT_Float32, nullptr );
  if ( !ds )
    return {};
  const double pixelSize = spec.get( "pixel_size", 30.0 ).asDouble();
  double geoTransform[6] = { 500000.0, pixelSize, 0.0, 5000000.0, 0.0, -pixelSize };
  ds->SetGeoTransform( geoTransform );
  OGRSpatialReference srs;
  if ( srs.importFromEPSG( 32650 ) == OGRERR_NONE )
  {
    char *wkt = nullptr;
    srs.exportToWkt( &wkt );
    ds->SetProjection( wkt );
    CPLFree( wkt );
  }
  for ( const std::string &key : spec.get( "metadata", Json::Value() ).getMemberNames() )
    ds->SetMetadataItem( key.c_str(),
                         spec["metadata"][ key ].asString().c_str(), nullptr );
  const double nodata = spec.get( "nodata", -9999.0 ).asDouble();
  for ( int b = 1; b <= bandCount; ++b )
  {
    GDALRasterBand *band = ds->GetRasterBand( b );
    band->SetNoDataValue( nodata );
    if ( b <= static_cast<int>( bands.size() ) && bands[ b - 1 ].isString() &&
         !bands[ b - 1 ].asString().empty() )
      band->SetMetadataItem( "SICNU_BAND_ROLE", bands[ b - 1 ].asString().c_str(), nullptr );
    // Deterministic, bounded pixel formula — no randoms anywhere.
    std::vector<float> row( width );
    for ( int y = 0; y < height; ++y )
    {
      for ( int x = 0; x < width; ++x )
        row[x] = static_cast<float>( ( y * width + x ) % 97 ) / 97.0f +
                 static_cast<float>( b ) / 10.0f;
      band->RasterIO( GF_Write, 0, y, width, 1, row.data(), width, 1, GDT_Float32, 0, 0 );
    }
  }
  GDALClose( ds );
  return path.toStdString();
}

// --- reference + assertion machinery ---------------------------------------

Json::Value resolveDottedPath( const Json::Value &doc, const std::string &dotted )
{
  Json::Value cursor = doc;
  size_t start = 0;
  while ( start <= dotted.size() )
  {
    const size_t dot = dotted.find( '.', start );
    std::string segment = dotted.substr( start, dot == std::string::npos
                                                  ? std::string::npos
                                                  : dot - start );
    if ( segment.empty() )
      return Json::Value();
    // Optional trailing [N] index — parsed strictly so a malformed index
    // resolves to nothing instead of silently passing on element 0 (P3).
    size_t index = std::string::npos;
    const size_t open = segment.find( '[' );
    if ( open != std::string::npos && segment.back() == ']' )
    {
      const std::string digits = segment.substr( open + 1, segment.size() - open - 2 );
      if ( digits.empty() ||
           !std::all_of( digits.begin(), digits.end(),
                         []( unsigned char ch ) { return std::isdigit( ch ); } ) )
        return Json::Value();
      index = static_cast<size_t>( std::stoul( digits ) );
      segment = segment.substr( 0, open );
    }
    if ( !cursor.isObject() || !cursor.isMember( segment ) )
      return Json::Value();
    cursor = cursor[ segment ];
    if ( index != std::string::npos )
    {
      if ( !cursor.isArray() || index >= static_cast<size_t>( cursor.size() ) )
        return Json::Value();
      cursor = cursor[ static_cast<Json::ArrayIndex>( index ) ];
    }
    if ( dot == std::string::npos )
      break;
    start = dot + 1;
  }
  return cursor;
}

/// Substitutes $fixture / $tmp / $previous[.path] string leaves in place.
void substituteReferences( Json::Value &node, const std::string &fixturePath,
                           const std::string &tmpPath, const Json::Value &previousOutput )
{
  if ( node.isString() )
  {
    const std::string text = node.asString();
    const std::string prefix = "$previous";
    if ( text == "$fixture" )
    {
      node = Json::Value( fixturePath );
      return;
    }
    if ( text == "$tmp" )
    {
      node = Json::Value( tmpPath );
      return;
    }
    if ( text.rfind( prefix, 0 ) == 0 &&
         ( text.size() == prefix.size() || text[ prefix.size() ] == '.' ) )
    {
      if ( text.size() == prefix.size() )
        node = previousOutput;
      else
        node = resolveDottedPath( previousOutput, text.substr( prefix.size() + 1 ) );
      return;
    }
    return;
  }
  if ( node.isObject() )
  {
    for ( const std::string &key : node.getMemberNames() )
      substituteReferences( node[ key ], fixturePath, tmpPath, previousOutput );
    return;
  }
  if ( node.isArray() )
    for ( Json::ArrayIndex i = 0; i < node.size(); ++i )
      substituteReferences( node[ i ], fixturePath, tmpPath, previousOutput );
}

bool subtreeContains( const Json::Value &node, const std::string &needle )
{
  if ( node.isString() )
    return node.asString().find( needle ) != std::string::npos;
  if ( node.isObject() )
  {
    for ( const std::string &key : node.getMemberNames() )
      if ( subtreeContains( node[ key ], needle ) )
        return true;
    return false;
  }
  if ( node.isArray() )
    for ( const Json::Value &entry : node )
      if ( subtreeContains( entry, needle ) )
        return true;
  return false;
}

} // namespace

TEST_CASE( "eval corpus is schema-clean and bounded", "[harness][corpus]" )
{
  SpatialToolRegistry::instance().registerBuiltinTools();

  std::vector<std::string> problems;
  const std::vector<Json::Value> rawCases = loadCaseFiles( corpusDirectory(), problems );
  REQUIRE( problems.empty() );
  REQUIRE_FALSE( rawCases.empty() );

  std::vector<Json::Value> expanded;
  for ( const Json::Value &rawCase : rawCases )
  {
    auto pieces = expandCase( rawCase );
    expanded.insert( expanded.end(), std::make_move_iterator( pieces.begin() ),
                     std::make_move_iterator( pieces.end() ) );
  }
  INFO( "expanded corpus size: " << expanded.size() );
  REQUIRE( expanded.size() <= kMaxExpandedCases );

  std::set<std::string> ids;
  for ( const Json::Value &entry : expanded )
  {
    const std::string id = entry.get( "case_id", "" ).asString();
    INFO( "case: " << id );
    REQUIRE_FALSE( id.empty() );
    REQUIRE( ids.insert( id ).second ); // unique

    const std::string category = entry.get( "category", "" ).asString();
    INFO( "category: " << category );
    REQUIRE( std::find_if( std::begin( kCategories ), std::end( kCategories ),
                           [ &category ]( const char *c ) { return category == c; } ) !=
             std::end( kCategories ) );

    const Json::Value &steps = entry.get( "steps", Json::Value() );
    REQUIRE( steps.isArray() );
    REQUIRE_FALSE( steps.empty() );
    for ( const Json::Value &step : steps )
    {
      const std::string tool = step.get( "tool", "" ).asString();
      INFO( "tool: " << tool );
      REQUIRE( SpatialToolRegistry::instance().find( tool ).has_value() );
      REQUIRE( step.isMember( "expect" ) );
    }
  }
}

TEST_CASE( "eval corpus cases pass against the live tool surface",
           "[harness][corpus]" )
{
  SpatialToolRegistry::instance().registerBuiltinTools();
  // Knowledge + recipes load from the source tree so feasibility, solution
  // paths, and recipe instantiation behave like the wired application.
  sicnu::agent::harness::CapabilityKnowledge &knowledge =
    sicnu::agent::harness::CapabilityKnowledge::instance();
  knowledge.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/capabilities" );
  knowledge.reload();
  sicnu::agent::harness::RecipeCatalog &catalog =
    sicnu::agent::harness::RecipeCatalog::instance();
  catalog.setDirectory( std::string( CMAKE_SOURCE_DIR ) + "/data/agent/recipes" );
  REQUIRE( catalog.reload() > 0 );

  std::vector<std::string> problems;
  const std::vector<Json::Value> rawCases = loadCaseFiles( corpusDirectory(), problems );
  REQUIRE( problems.empty() );

  std::vector<std::string> failures;
  int total = 0;
  for ( const Json::Value &rawCase : rawCases )
  {
    for ( const Json::Value &entry : expandCase( rawCase ) )
    {
      ++total;
      const std::string id = entry.get( "case_id", "??" ).asString();
      QTemporaryDir dir;
      if ( !dir.isValid() )
      {
        failures.push_back( id + ": temp dir unavailable" );
        continue;
      }
      const Json::Value &fixtureSpec = entry.get( "fixture", Json::Value() );
      std::string fixturePath;
      if ( fixtureSpec.isObject() )
      {
        if ( fixtureSpec.get( "kind", "raster" ).asString() == "raster" )
        {
          fixturePath = writeRasterFixture( dir.filePath( QStringLiteral( "fixture.tif" ) ),
                                            fixtureSpec );
          if ( fixturePath.empty() )
          {
            failures.push_back( id + ": fixture generation failed" );
            continue;
          }
        }
        else if ( fixtureSpec.get( "kind", "" ).asString() == "text" )
        {
          // Sanitized: strip any path separators so a corpus file cannot
          // point the fixture outside the case temp dir (review P3).
          QString name = QString::fromStdString(
            fixtureSpec.get( "name", "fixture.txt" ).asString() );
          name.remove( QChar( '/' ) ).remove( QChar( '\\' ) );
          if ( name.isEmpty() )
            name = QStringLiteral( "fixture.txt" );
          const QString path = dir.filePath( name );
          QFile textFile( path );
          if ( !textFile.open( QIODevice::WriteOnly ) ||
               textFile.write( fixtureSpec.get( "content", "" ).asString().c_str() ) < 0 )
          {
            failures.push_back( id + ": text fixture failed" );
            continue;
          }
          fixturePath = path.toStdString();
        }
      }
      const std::string tmpPath = dir.path().toStdString();

      Json::Value previousOutput;
      for ( const Json::Value &step : entry.get( "steps", Json::Value( Json::arrayValue ) ) )
      {
        const std::string tool = step.get( "tool", "" ).asString();
        auto registered = SpatialToolRegistry::instance().find( tool );
        if ( !registered )
        {
          failures.push_back( id + ": unknown tool " + tool );
          break;
        }
        Json::Value input = step.get( "input", Json::Value( Json::objectValue ) );
        substituteReferences( input, fixturePath, tmpPath, previousOutput );

        const SpatialToolResult result = ( *registered )->execute( input );
        const Json::Value &expect = step[ "expect" ];

        const bool wantSuccess = expect.get( "success", true ).asBool();
        if ( result.success != wantSuccess )
        {
          failures.push_back( id + ": step '" + tool + "' success=" +
                              ( result.success ? "true" : "false" ) + " expected " +
                              ( wantSuccess ? "true" : "false" ) +
                              ( result.success
                                  ? ""
                                  : " error=" + result.errorCode + " msg=" +
                                        result.error.substr( 0, 200 ) ) +
                              "\n  input: " + input.toStyledString().substr( 0, 1200 ) );
          break;
        }
        if ( !result.success && expect.isMember( "error_code" ) &&
             result.errorCode != expect["error_code"].asString() )
        {
          failures.push_back( id + ": step '" + tool + "' error_code=" + result.errorCode +
                              " expected " + expect["error_code"].asString() );
          break;
        }
        if ( expect.isMember( "response_bytes_le" ) )
        {
          Json::StreamWriterBuilder builder;
          builder["indentation"] = "";
          const size_t bytes = Json::writeString( builder, result.output ).size();
          if ( bytes > static_cast<size_t>( expect["response_bytes_le"].asUInt64() ) )
          {
            failures.push_back( id + ": step '" + tool + "' response " +
                                std::to_string( bytes ) + " bytes exceeds budget" );
            break;
          }
        }
        bool asserted = true;
        for ( const Json::Value &assertion :
              expect.get( "asserts", Json::Value( Json::arrayValue ) ) )
        {
          const Json::Value found =
            resolveDottedPath( result.output, assertion.get( "path", "" ).asString() );
          if ( assertion.isMember( "exists" ) )
            asserted = asserted && found.isNull() != assertion["exists"].asBool();
          if ( assertion.isMember( "equals" ) )
          {
            const Json::Value &wanted = assertion["equals"];
            asserted = asserted &&
                       ( ( wanted.isNumeric() && found.isNumeric()
                             ? std::fabs( wanted.asDouble() - found.asDouble() ) < 1e-9
                             : found == wanted ) );
          }
          if ( assertion.isMember( "contains" ) )
            asserted = asserted && subtreeContains( found, assertion["contains"].asString() );
          if ( !asserted )
          {
            failures.push_back( id + ": step '" + tool + "' assertion failed: " +
                                assertion.toStyledString() + "\n  actual response: " +
                                result.output.toStyledString().substr( 0, 1200 ) );
            break;
          }
        }
        if ( !asserted )
          break;
        previousOutput = result.output;
      }
    }
  }

  INFO( "expanded cases executed: " << total );
  for ( const std::string &failure : failures )
    WARN( failure );
  REQUIRE( failures.empty() );
}
