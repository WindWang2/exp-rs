// tests/test_algorithm_search.cpp — Track I (ds41-capability-search-13):
// authoritative algorithm-search contract. Golden corpus over an injected
// descriptor universe + live registry, mutation potency, bounds/typed
// refusal, deterministic ordering, and CLI<->MCP cross-surface parity
// (mcp_server.cpp is compiled into this binary; the CLI runs as a real
// subprocess, so both surfaces share one engine over one universe).
#include <catch2/catch_test_macros.hpp>

#include "processing/framework/algorithm_search.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"

#include "agent/mcp_server.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QString>
#include <QVariantMap>
#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include "processing/providers/qgis_algorithms/provider.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#ifndef SICNU_TEST_SEARCH_CLI
#error "SICNU_TEST_SEARCH_CLI must point at the sicnu_geo_rs_cli binary"
#endif

using namespace sicnu;

namespace {

// --------------------------------------------------------------------------
// Synthetic corpus: every fact below is DECLARED metadata — the corpus never
// relies on prose for structure.
// --------------------------------------------------------------------------
processing::PortDescriptor port( const std::string &name, processing::DataType type,
                                 const std::string &modality = {} )
{
  processing::PortDescriptor p;
  p.name = name;
  p.type = type;
  if ( !modality.empty() )
  {
    Json::Value contract( Json::objectValue );
    contract["modality"] = modality;
    p.rsContract = contract;
  }
  return p;
}

processing::AlgorithmDescriptor makeDesc( const std::string &id, const std::string &group,
                                          const std::string &displayName,
                                          const std::string &description )
{
  processing::AlgorithmDescriptor d;
  d.id = id;
  d.group = group;
  d.displayName = displayName;
  d.description = description;
  return d;
}

/// Corpus spanning preprocess / SAR / spectral / temporal / vector / dem —
/// the domains the track's golden queries must cover.
std::vector<processing::AlgorithmDescriptor> corpus()
{
  using processing::DataType;
  std::vector<processing::AlgorithmDescriptor> u;

  {
    auto d = makeDesc( "rs:apply_mask", "preprocess", "Apply Mask",
                       "Masks pixels of the input raster." );
    d.agentMetadata.tags = { "mask", "preprocess", "quality" };
    d.agentMetadata.purpose = "Exclude pixels flagged by a mask band.";
    d.agentMetadata.taskFamily = "preprocess";
    d.agentMetadata.largeRasterSafe = true;
    d.inputs = { port( "input", DataType::Raster, "optical" ),
                 port( "mask", DataType::Raster ) };
    d.outputs = { port( "output", DataType::Raster ) };
    u.push_back( d );
  }
  {
    auto d = makeDesc( "rs:sar_calibrate", "sar", "SAR Calibrate",
                       "Radiometric calibration of SAR scenes." );
    d.agentMetadata.tags = { "sar", "calibration" };
    d.agentMetadata.purpose = "Convert SAR DN to beta/sigma nought.";
    d.agentMetadata.taskFamily = "preprocess";
    d.agentMetadata.largeRasterSafe = true;
    d.inputs = { port( "input", DataType::Raster, "sar" ) };
    d.outputs = { port( "output", DataType::Raster ) };
    u.push_back( d );
  }
  {
    auto d = makeDesc( "rs:sar_speckle", "sar", "SAR Speckle Filter",
                       "Speckle filtering for SAR amplitude." );
    d.agentMetadata.tags = { "sar", "speckle", "filter" };
    d.agentMetadata.purpose = "Reduce speckle noise in SAR imagery.";
    d.agentMetadata.taskFamily = "preprocess";
    d.inputs = { port( "input", DataType::Raster, "sar" ) };
    d.outputs = { port( "output", DataType::Raster ) };
    u.push_back( d );
  }
  {
    auto d = makeDesc( "rs:spectral_index", "spectral", "Spectral Index",
                       "Band-math spectral index (NDVI, ...)." );
    d.agentMetadata.tags = { "spectral", "index", "vegetation" };
    d.agentMetadata.purpose = "Compute a per-pixel spectral index.";
    d.agentMetadata.taskFamily = "spectral";
    d.agentMetadata.largeRasterSafe = true;
    d.inputs = { port( "input", DataType::Raster, "optical" ) };
    d.outputs = { port( "output", DataType::Raster ) };
    u.push_back( d );
  }
  {
    auto d = makeDesc( "rs:temporal_composite", "temporal", "Temporal Composite",
                       "Composite a time series into one raster." );
    d.agentMetadata.tags = { "temporal", "composite" };
    d.agentMetadata.purpose = "Aggregate a temporal raster collection.";
    d.agentMetadata.taskFamily = "temporal";
    d.inputs = { port( "collection", DataType::Json ) };
    d.outputs = { port( "output", DataType::Raster ) };
    u.push_back( d );
  }
  {
    auto d = makeDesc( "rs:rasterize", "vector", "Rasterize",
                       "Burn vector features into a raster." );
    d.agentMetadata.tags = { "vector", "rasterize" };
    d.agentMetadata.purpose = "Convert vector geometries to raster cells.";
    d.agentMetadata.taskFamily = "convert";
    d.inputs = { port( "input", DataType::Vector ) };
    d.outputs = { port( "output", DataType::Raster ) };
    u.push_back( d );
  }
  {
    auto d = makeDesc( "rs:dem_slope", "terrain", "DEM Slope",
                       "Slope from a DEM raster." );
    d.agentMetadata.tags = { "dem", "terrain", "slope" };
    d.agentMetadata.purpose = "Derive slope angle from elevation.";
    d.agentMetadata.taskFamily = "terrain";
    d.agentMetadata.largeRasterSafe = true;
    d.inputs = { port( "input", DataType::Raster, "dem" ) };
    d.outputs = { port( "output", DataType::Raster ) };
    u.push_back( d );
  }
  {
    auto d = makeDesc( "rs:classify", "classification", "Classify",
                       "Supervised classification of imagery." );
    d.agentMetadata.tags = { "classification", "ml" };
    d.agentMetadata.purpose = "Assign class labels from a trained model.";
    d.agentMetadata.taskFamily = "classification";
    d.inputs = { port( "input", DataType::Raster, "optical" ) };
    d.outputs = { port( "output", DataType::Raster ),
                  port( "report", DataType::Json ) };
    u.push_back( d );
  }
  return u;
}

std::vector<std::string> hitIds( const std::vector<processing::AlgorithmDescriptor> &u,
                                 const processing::AlgorithmSearchResult &r )
{
  std::vector<std::string> ids;
  for ( const auto &hit : r.hits )
    ids.push_back( u[hit.index].id );
  return ids;
}

std::set<std::string> idSet( const std::vector<std::string> &ids )
{
  return { ids.begin(), ids.end() };
}

// --------------------------------------------------------------------------
// MCP probe (mcp_server.cpp is compiled into this binary — same pattern as
// test_capability_surface_parity) so the tools/call path is exercised for
// real, argument parsing included.
// --------------------------------------------------------------------------
class SearchProbeServer : public McpServer
{
  public:
    QVariantMap lastResponseResult;
    int lastErrorCode = -1;

    void request( const QVariantMap &req ) { handleRequest( req ); }

    void sendResponse( const QVariant &, const QVariantMap &result ) override
    {
      lastResponseResult = result;
      lastErrorCode = 0;
    }
    void sendError( const QVariant &, int code, const QString & ) override
    {
      lastErrorCode = code;
    }
    void sendError( const QVariant &, int code, const QString &, const QVariantMap & ) override
    {
      lastErrorCode = code;
    }
    void sendNotification( const QString &, const QVariantMap & ) override {}

    bool callTool( const QString &name, const QVariantMap &args )
    {
      QVariantMap params;
      params[QStringLiteral( "name" )] = name;
      params[QStringLiteral( "arguments" )] = args;
      request( QVariantMap{
          { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
          { QStringLiteral( "id" ), 1 },
          { QStringLiteral( "method" ), QStringLiteral( "tools/call" ) },
          { QStringLiteral( "params" ), params } } );
      if ( lastErrorCode != 0 )
        return false;
      return !lastResponseResult.value( QStringLiteral( "isError" ) ).toBool();
    }

    QVariantMap toolOutput() const
    {
      const QVariantList content = lastResponseResult.value( QStringLiteral( "content" ) ).toList();
      if ( content.isEmpty() )
        return {};
      const QString text = content.first().toMap().value( QStringLiteral( "text" ) ).toString();
      const QJsonDocument doc = QJsonDocument::fromJson( text.toUtf8() );
      return doc.isObject() ? doc.object().toVariantMap() : QVariantMap{};
    }
};

SearchProbeServer &mcp()
{
  static SearchProbeServer *instance = [] {
    static int argc = 1;
    static char arg0[] = "test_algorithm_search";
    static char *argv[] = { arg0 };
    if ( !QCoreApplication::instance() )
      new QCoreApplication( argc, argv );
    auto *s = new SearchProbeServer();
    s->request( QVariantMap{
        { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
        { QStringLiteral( "id" ), 0 },
        { QStringLiteral( "method" ), QStringLiteral( "initialize" ) },
        { QStringLiteral( "params" ), QVariantMap{ { QStringLiteral( "protocolVersion" ),
                                                      QStringLiteral( "2024-11-05" ) } } } } );
    return s;
  }();
  return *instance;
}

QVariantMap runCliSearch( const QStringList &args, int *exitCode )
{
  QProcess cli;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert( QStringLiteral( "OPENCV_LOG_LEVEL" ), QStringLiteral( "warning" ) );
  env.insert( QStringLiteral( "QT_QPA_PLATFORM" ), QStringLiteral( "offscreen" ) );
  cli.setProcessEnvironment( env );
  cli.start( QString::fromUtf8( SICNU_TEST_SEARCH_CLI ), args );
  REQUIRE( cli.waitForStarted( 10000 ) );
  REQUIRE( cli.waitForFinished( 180000 ) );
  if ( exitCode )
    *exitCode = cli.exitCode();
  const QByteArray raw = cli.readAllStandardOutput();
  const int jsonStart = raw.indexOf( '{' );
  REQUIRE( jsonStart >= 0 );
  const QJsonDocument doc = QJsonDocument::fromJson( raw.mid( jsonStart ) );
  REQUIRE( doc.isObject() );
  return doc.object().toVariantMap();
}

QStringList entryIds( const QVariantMap &searchOut )
{
  QStringList ids;
  for ( const QVariant &entry : searchOut.value( QStringLiteral( "algorithms" ) ).toList() )
    ids.push_back( entry.toMap().value( QStringLiteral( "id" ) ).toString() );
  return ids;
}

} // namespace

// ===========================================================================
// Engine contract over the synthetic corpus
// ===========================================================================

TEST_CASE( "algorithm search: free text tokens are ANDed and scored deterministically",
           "[search][engine]" )
{
  const auto u = corpus();
  processing::AlgorithmSearchQuery q;
  q.text = "sar";
  q.limit = 100;
  const auto result = processing::searchAlgorithms( u, q );
  REQUIRE_FALSE( result.error.has_value() );
  // "sar" hits: rs:sar_calibrate + rs:sar_speckle (id/tag/description/modality)
  // — and rs:sar_ratio absent, so the set is exactly the two SAR operators.
  CHECK( idSet( hitIds( u, result ) ) == idSet( { "rs:sar_calibrate", "rs:sar_speckle" } ) );
  // Deterministic re-run: identical order.
  CHECK( hitIds( u, processing::searchAlgorithms( u, q ) ) == hitIds( u, result ) );
}

TEST_CASE( "algorithm search: structured filters use declared metadata (golden corpus)",
           "[search][engine][golden]" )
{
  const auto u = corpus();
  const auto idsOf = [&u]( processing::AlgorithmSearchQuery q ) {
    q.limit = 100;
    const auto r = processing::searchAlgorithms( u, q );
    REQUIRE_FALSE( r.error.has_value() );
    return hitIds( u, r );
  };

  // tag (exact, declared)
  {
    processing::AlgorithmSearchQuery q;
    q.tags = { "sar" };
    CHECK( idsOf( q ) == std::vector<std::string>{ "rs:sar_calibrate", "rs:sar_speckle" } );
  }
  // tag list is ANY-of
  {
    processing::AlgorithmSearchQuery q;
    q.tags = { "sar", "dem" };
    CHECK( idSet( idsOf( q ) )
           == idSet( { "rs:sar_calibrate", "rs:sar_speckle", "rs:dem_slope" } ) );
  }
  // tag filter must NOT substring-match ("veg" is not a declared tag)
  {
    processing::AlgorithmSearchQuery q;
    q.tags = { "veg" };
    CHECK( idsOf( q ).empty() );
  }
  // task family (exact)
  {
    processing::AlgorithmSearchQuery q;
    q.taskFamily = "temporal";
    CHECK( idsOf( q ) == std::vector<std::string>{ "rs:temporal_composite" } );
  }
  // purpose substring
  {
    processing::AlgorithmSearchQuery q;
    q.purpose = "speckle";
    CHECK( idsOf( q ) == std::vector<std::string>{ "rs:sar_speckle" } );
  }
  // modality from declared input-port contracts (exact)
  {
    processing::AlgorithmSearchQuery q;
    q.modalities = { "sar" };
    CHECK( idSet( idsOf( q ) ) == idSet( { "rs:sar_calibrate", "rs:sar_speckle" } ) );
  }
  {
    processing::AlgorithmSearchQuery q;
    q.modalities = { "optical", "dem" };
    CHECK( idSet( idsOf( q ) )
           == idSet( { "rs:apply_mask", "rs:spectral_index", "rs:classify", "rs:dem_slope" } ) );
  }
  // input/output types (case-insensitive exact vs dataTypeToString)
  {
    processing::AlgorithmSearchQuery q;
    q.inputType = "vector";
    CHECK( idsOf( q ) == std::vector<std::string>{ "rs:rasterize" } );
  }
  {
    processing::AlgorithmSearchQuery q;
    q.inputType = "json"; // temporal_composite reads a Json collection port
    CHECK( idsOf( q ) == std::vector<std::string>{ "rs:temporal_composite" } );
  }
  {
    processing::AlgorithmSearchQuery q;
    q.outputType = "Json"; // only classify emits a Json report port
    CHECK( idsOf( q ) == std::vector<std::string>{ "rs:classify" } );
  }
  // group exact
  {
    processing::AlgorithmSearchQuery q;
    q.group = "spectral";
    CHECK( idsOf( q ) == std::vector<std::string>{ "rs:spectral_index" } );
  }
  // large-raster safety
  {
    processing::AlgorithmSearchQuery q;
    q.largeRasterSafeOnly = true;
    CHECK( idSet( idsOf( q ) )
           == idSet( { "rs:apply_mask", "rs:sar_calibrate", "rs:spectral_index",
                        "rs:dem_slope" } ) );
  }
}

TEST_CASE( "algorithm search: AND across dimensions (OR-swap mutation is detectable)",
           "[search][engine][mutation]" )
{
  const auto u = corpus();
  // tag=sar AND modality=optical must intersect to EMPTY in this corpus —
  // an implementation that ORs dimensions would return the union (4 ids).
  processing::AlgorithmSearchQuery q;
  q.tags = { "sar" };
  q.modalities = { "optical" };
  q.limit = 100;
  const auto r = processing::searchAlgorithms( u, q );
  REQUIRE_FALSE( r.error.has_value() );
  CHECK( r.hits.empty() );

  // tag=sar AND task=preprocess intersects to the two SAR preprocessors,
  // strictly smaller than the tag-only set's union with task-only.
  processing::AlgorithmSearchQuery q2;
  q2.tags = { "sar" };
  q2.taskFamily = "preprocess";
  q2.limit = 100;
  const auto r2 = processing::searchAlgorithms( u, q2 );
  CHECK( idSet( hitIds( u, r2 ) ) == idSet( { "rs:sar_calibrate", "rs:sar_speckle" } ) );

  processing::AlgorithmSearchQuery taskOnly;
  taskOnly.taskFamily = "preprocess";
  taskOnly.limit = 100;
  const auto rTask = processing::searchAlgorithms( u, taskOnly );
  // task=preprocess alone covers apply_mask + the two SAR ops — an OR impl
  // would return that union for q2 as well (5 ids, incl. sar ops again).
  CHECK( idSet( hitIds( u, rTask ) )
         == idSet( { "rs:apply_mask", "rs:sar_calibrate", "rs:sar_speckle" } ) );
  CHECK( r2.hits.size() < rTask.hits.size() + 2 ); // AND strictly narrows
}

TEST_CASE( "algorithm search: case-fold and accent-insensitive normalization",
           "[search][engine][normalization]" )
{
  const auto u = corpus();
  {
    processing::AlgorithmSearchQuery q;
    q.text = "SAR"; // upper-case query must hit lower-case declarations
    q.limit = 100;
    CHECK( idSet( hitIds( u, processing::searchAlgorithms( u, q ) ) )
           == idSet( { "rs:sar_calibrate", "rs:sar_speckle" } ) );
  }
  {
    processing::AlgorithmSearchQuery q;
    q.group = "SPECTRAL"; // group filter is case-folded exact
    q.limit = 100;
    CHECK( hitIds( u, processing::searchAlgorithms( u, q ) )
           == std::vector<std::string>{ "rs:spectral_index" } );
  }
  {
    processing::AlgorithmSearchQuery q;
    q.tags = { "DeM" };
    q.limit = 100;
    CHECK( hitIds( u, processing::searchAlgorithms( u, q ) )
           == std::vector<std::string>{ "rs:dem_slope" } );
  }
  {
    // tokenizer keeps '_' and ':' — the full id is one token
    processing::AlgorithmSearchQuery q;
    q.text = "rs:spectral_index";
    q.limit = 100;
    const auto r = processing::searchAlgorithms( u, q );
    REQUIRE( r.hits.size() == 1 );
    CHECK( u[r.hits.front().index].id == "rs:spectral_index" );
    CHECK( r.hits.front().score > 100 ); // exact-id pin
  }
  {
    // multi-token text ANDs tokens: "sar calibrate" needs BOTH
    processing::AlgorithmSearchQuery q;
    q.text = "sar calibrate";
    q.limit = 100;
    CHECK( hitIds( u, processing::searchAlgorithms( u, q ) )
           == std::vector<std::string>{ "rs:sar_calibrate" } );
  }
  {
    // Accent folding: NFKD strips the combining mark from Í, and CJK
    // letters tokenize like any other letters — a fold implementation
    // that no-ops on non-ASCII would fail both.
    std::vector<processing::AlgorithmDescriptor> u2;
    u2.push_back( makeDesc( "zz:idx", "g", "Índice Espectral", "x" ) );
    u2.push_back( makeDesc( "zz:resample", "g", "重采样工具", "x" ) );
    processing::AlgorithmSearchQuery q;
    q.text = "indice";
    q.limit = 10;
    const auto r = processing::searchAlgorithms( u2, q );
    REQUIRE( r.hits.size() == 1 );
    CHECK( u2[r.hits.front().index].id == "zz:idx" );
    processing::AlgorithmSearchQuery cjk;
    cjk.text = "重采";
    cjk.limit = 10;
    const auto r2 = processing::searchAlgorithms( u2, cjk );
    REQUIRE( r2.hits.size() == 1 );
    CHECK( u2[r2.hits.front().index].id == "zz:resample" );
  }
}

TEST_CASE( "algorithm search: field weights drive a differential ranking",
           "[search][engine][ordering]" )
{
  // One token, three single-field hits: id (8) > tags/displayName (4) >
  // description (1). Asserting exact scores + order anchors the weight
  // magnitudes — a weight permutation would reorder these hits while
  // leaving every set-based oracle green.
  std::vector<processing::AlgorithmDescriptor> u;
  u.push_back( makeDesc( "zz:needle_id", "g", "Plain", "text" ) );
  {
    auto d = makeDesc( "zz:other_a", "g", "Plain", "text" );
    d.agentMetadata.tags = { "needle" };
    u.push_back( d );
  }
  u.push_back( makeDesc( "zz:other_b", "g", "Plain", "the needle lives here" ) );

  processing::AlgorithmSearchQuery q;
  q.text = "needle";
  q.limit = 100;
  const auto r = processing::searchAlgorithms( u, q );
  REQUIRE( r.hits.size() == 3 );
  CHECK( u[r.hits[0].index].id == "zz:needle_id" );
  CHECK( u[r.hits[1].index].id == "zz:other_a" );
  CHECK( u[r.hits[2].index].id == "zz:other_b" );
  CHECK( r.hits[0].score == 8 );
  CHECK( r.hits[1].score == 4 );
  CHECK( r.hits[2].score == 1 );
}

TEST_CASE( "algorithm search: empty query returns every filter-passing entry in id order",
           "[search][engine]" )
{
  const auto u = corpus();
  processing::AlgorithmSearchQuery q;
  q.limit = 100;
  const auto r = processing::searchAlgorithms( u, q );
  REQUIRE( r.hits.size() == u.size() );
  std::vector<std::string> ids = hitIds( u, r );
  CHECK( std::is_sorted( ids.begin(), ids.end() ) );
}

TEST_CASE( "algorithm search: pagination is deterministic and bounded",
           "[search][engine][pagination]" )
{
  const auto u = corpus();
  processing::AlgorithmSearchQuery q;
  q.limit = 3;
  std::vector<std::string> paged;
  for ( int cursor = 0; cursor >= 0; )
  {
    q.cursor = cursor;
    const auto page = processing::searchAlgorithms( u, q );
    for ( const auto &hit : page.hits )
      paged.push_back( u[hit.index].id );
    cursor = page.nextCursor;
  }
  CHECK( paged.size() == u.size() );
  processing::AlgorithmSearchQuery all;
  all.limit = 100;
  CHECK( paged == hitIds( u, processing::searchAlgorithms( u, all ) ) );

  // limit is hard-capped at kMaxLimit
  processing::AlgorithmSearchQuery big;
  big.limit = 100000;
  const auto capped = processing::searchAlgorithms( u, big );
  CHECK( capped.limit == processing::AlgorithmSearchQuery::kMaxLimit );

  // limit=0 falls back to the default page size
  processing::AlgorithmSearchQuery def;
  def.limit = 0;
  const auto defaulted = processing::searchAlgorithms( u, def );
  CHECK( defaulted.limit == processing::AlgorithmSearchQuery::kDefaultLimit );

  // negative cursor clamps to the first page (same hits as cursor=0)
  processing::AlgorithmSearchQuery neg;
  neg.limit = 3;
  neg.cursor = -7;
  const auto clamped = processing::searchAlgorithms( u, neg );
  CHECK( clamped.cursor == 0 );
  processing::AlgorithmSearchQuery zero;
  zero.limit = 3;
  zero.cursor = 0;
  CHECK( hitIds( u, clamped ) == hitIds( u, processing::searchAlgorithms( u, zero ) ) );

  // cursor past the end: empty page, nextCursor -1, total still honest
  processing::AlgorithmSearchQuery over;
  over.limit = 3;
  over.cursor = 9999;
  const auto pastEnd = processing::searchAlgorithms( u, over );
  CHECK( pastEnd.hits.empty() );
  CHECK( pastEnd.nextCursor == -1 );
  CHECK( pastEnd.total == static_cast<int>( u.size() ) );
}

TEST_CASE( "algorithm search: oversized/malformed queries get a typed refusal",
           "[search][engine][bounds]" )
{
  const auto u = corpus();
  {
    processing::AlgorithmSearchQuery q;
    q.text = std::string( processing::AlgorithmSearchQuery::kMaxTextLength + 1, 'x' );
    const auto r = processing::searchAlgorithms( u, q );
    REQUIRE( r.error.has_value() );
    CHECK( r.hits.empty() );
    CHECK( r.total == 0 );
  }
  {
    processing::AlgorithmSearchQuery q;
    q.group = std::string( processing::AlgorithmSearchQuery::kMaxFilterLength + 1, 'g' );
    const auto r = processing::searchAlgorithms( u, q );
    REQUIRE( r.error.has_value() );
  }
  {
    processing::AlgorithmSearchQuery q;
    q.tags.assign( processing::AlgorithmSearchQuery::kMaxListValues + 1, "t" );
    const auto r = processing::searchAlgorithms( u, q );
    REQUIRE( r.error.has_value() );
  }
}

TEST_CASE( "algorithm search: zero hits surface the honest filter vocabulary + id suggestions",
           "[search][engine][hints]" )
{
  const auto u = corpus();
  processing::AlgorithmSearchQuery q;
  q.text = "spekcle"; // typo of "speckle"
  q.limit = 100;
  const auto r = processing::searchAlgorithms( u, q );
  REQUIRE( r.hits.empty() );
  CHECK( r.vocabulary.tags.size() > 5 );
  CHECK( std::find( r.vocabulary.tags.begin(), r.vocabulary.tags.end(), "sar" )
         != r.vocabulary.tags.end() );
  CHECK( std::find( r.vocabulary.taskFamilies.begin(), r.vocabulary.taskFamilies.end(),
                    "temporal" ) != r.vocabulary.taskFamilies.end() );
  CHECK( std::find( r.vocabulary.modalities.begin(), r.vocabulary.modalities.end(), "sar" )
         != r.vocabulary.modalities.end() );
  // id suggestions: a misspelled token maps to the closest id segment
  // (bounded edit distance) — "spekcle" → "speckle".
  CHECK( std::find( r.suggestions.begin(), r.suggestions.end(), "rs:sar_speckle" )
         != r.suggestions.end() );
  CHECK( r.suggestions.size() <= 5 );
}

// ===========================================================================
// Mutation potency: the SAME engine call must produce different results when
// declared metadata changes — proving filters (not prose) drive results.
// ===========================================================================

TEST_CASE( "algorithm search: deleting a declared tag breaks the tag query (mutation)",
           "[search][engine][mutation]" )
{
  auto u = corpus();
  processing::AlgorithmSearchQuery q;
  q.tags = { "calibration" };
  q.limit = 100;
  REQUIRE( hitIds( u, processing::searchAlgorithms( u, q ) )
           == std::vector<std::string>{ "rs:sar_calibrate" } );

  // Mutate the corpus: strip the tag from the descriptor. The tag query
  // must now return empty — if the engine inferred from prose it would
  // still "find" it.
  for ( auto &d : u )
    if ( d.id == "rs:sar_calibrate" )
      d.agentMetadata.tags.erase(
          std::remove( d.agentMetadata.tags.begin(), d.agentMetadata.tags.end(),
                       "calibration" ),
          d.agentMetadata.tags.end() );
  CHECK( hitIds( u, processing::searchAlgorithms( u, q ) ).empty() );
}

TEST_CASE( "algorithm search: clearing declared purpose breaks the purpose filter (mutation)",
           "[search][engine][mutation]" )
{
  auto u = corpus();
  processing::AlgorithmSearchQuery q;
  q.purpose = "speckle";
  q.limit = 100;
  REQUIRE( hitIds( u, processing::searchAlgorithms( u, q ) )
           == std::vector<std::string>{ "rs:sar_speckle" } );

  for ( auto &d : u )
    if ( d.id == "rs:sar_speckle" )
      d.agentMetadata.purpose.clear();
  CHECK( hitIds( u, processing::searchAlgorithms( u, q ) ).empty() );
  // ...yet the free text still finds it via description — text and purpose
  // filters are independent dimensions.
  processing::AlgorithmSearchQuery text;
  text.text = "speckle";
  text.limit = 100;
  CHECK( hitIds( u, processing::searchAlgorithms( u, text ) )
         == std::vector<std::string>{ "rs:sar_speckle" } );
}

// ===========================================================================
// Live registry: golden queries whose expected ids derive from structured
// metadata (never from prose matching).
// ===========================================================================

TEST_CASE( "algorithm search: live-registry golden queries match declared metadata exactly",
           "[search][golden][live]" )
{
  operators::RSOperatorRegistry::instance();
  operators::rs::initBuiltinRsOperators();
  operators::rs::installRsOperatorProvider();
  auto &registry = processing::AtomicAlgorithmRegistry::instance();
  registry.initialize();
  const auto universe = registry.listDescriptors();
  REQUIRE( universe.size() > 100 );

  // Reference expectation: the contract recomputed naively over declared
  // fields only. This double-checks the engine AND derives the expected
  // set from metadata (not from hand-picked prose assumptions).
  const auto expectedForTag = [&universe]( const std::string &foldedTag ) {
    std::set<std::string> expected;
    for ( const auto &d : universe )
      for ( const auto &t : d.agentMetadata.tags )
        if ( QString::fromStdString( t ).toLower()
             == QString::fromStdString( foldedTag ).toLower() )
          expected.insert( d.id );
    return expected;
  };

  // SAR domain — every descriptor that DECLARES a sar tag, no more.
  {
    processing::AlgorithmSearchQuery q;
    q.tags = { "sar" };
    q.limit = 500;
    const auto r = processing::searchAlgorithms( universe, q );
    REQUIRE_FALSE( r.error.has_value() );
    const auto expected = expectedForTag( "sar" );
    REQUIRE( expected.size() >= 3 ); // sar domain is non-trivial
    std::set<std::string> got;
    for ( const auto &hit : r.hits )
      got.insert( universe[hit.index].id );
    CHECK( got == expected );
  }

  // classification task family — declared taskFamily equality.
  {
    processing::AlgorithmSearchQuery q;
    q.taskFamily = "classification";
    q.limit = 500;
    const auto r = processing::searchAlgorithms( universe, q );
    std::set<std::string> expected;
    for ( const auto &d : universe )
      if ( QString::fromStdString( d.agentMetadata.taskFamily ).toLower()
           == "classification" )
        expected.insert( d.id );
    REQUIRE( !expected.empty() );
    std::set<std::string> got;
    for ( const auto &hit : r.hits )
      got.insert( universe[hit.index].id );
    CHECK( got == expected );
  }

  // sar-insar — the largest declared task family in the live registry.
  {
    processing::AlgorithmSearchQuery q;
    q.taskFamily = "sar-insar";
    q.limit = 500;
    const auto r = processing::searchAlgorithms( universe, q );
    std::set<std::string> expected;
    for ( const auto &d : universe )
      if ( QString::fromStdString( d.agentMetadata.taskFamily ).toLower() == "sar-insar" )
        expected.insert( d.id );
    std::set<std::string> got;
    for ( const auto &hit : r.hits )
      got.insert( universe[hit.index].id );
    CHECK( got == expected );
    CHECK( expected.size() >= 5 ); // sar-insar is non-trivial
  }

  // temporal domain — declared task family (hyphenated live vocabulary).
  {
    processing::AlgorithmSearchQuery q;
    q.taskFamily = "temporal-monitoring";
    q.limit = 500;
    const auto r = processing::searchAlgorithms( universe, q );
    std::set<std::string> expected;
    for ( const auto &d : universe )
      if ( QString::fromStdString( d.agentMetadata.taskFamily ).toLower()
           == "temporal-monitoring" )
        expected.insert( d.id );
    std::set<std::string> got;
    for ( const auto &hit : r.hits )
      got.insert( universe[hit.index].id );
    CHECK( got == expected );
    CHECK( !expected.empty() ); // temporal family exists (temporal12 track)
  }

  // vector domain — declared input port type.
  {
    processing::AlgorithmSearchQuery q;
    q.inputType = "vector";
    q.limit = 500;
    const auto r = processing::searchAlgorithms( universe, q );
    std::set<std::string> expected;
    for ( const auto &d : universe )
      for ( const auto &port : d.inputs )
        if ( port.type == processing::DataType::Vector )
          expected.insert( d.id );
    REQUIRE( !expected.empty() );
    std::set<std::string> got;
    for ( const auto &hit : r.hits )
      got.insert( universe[hit.index].id );
    CHECK( got == expected );
  }

  // preprocess intent via free text on declared fields.
  {
    processing::AlgorithmSearchQuery q;
    q.text = "preprocess";
    q.limit = 500;
    const auto r = processing::searchAlgorithms( universe, q );
    CHECK( r.hits.size() >= 1 );
  }
}

// ===========================================================================
// Cross-surface parity: MCP tools/call and the real CLI subprocess must
// return identical id sequences for the same query (same engine, same
// universe).
// ===========================================================================

TEST_CASE( "algorithm search: CLI and MCP return identical ids and ordering",
           "[search][parity][cli][mcp]" )
{
  operators::RSOperatorRegistry::instance();
  operators::rs::initBuiltinRsOperators();
  operators::rs::installRsOperatorProvider();
  processing::AtomicAlgorithmRegistry::instance().initialize();

  const auto mcpIds = []( const QVariantMap &args ) {
    QStringList all;
    int cursor = 0;
    for ( ;; )
    {
      QVariantMap callArgs = args;
      callArgs[QStringLiteral( "limit" )] = 500;
      callArgs[QStringLiteral( "cursor" )] = cursor;
      REQUIRE( mcp().callTool( QStringLiteral( "search_algorithms" ), callArgs ) );
      const QVariantMap out = mcp().toolOutput();
      const QStringList page = entryIds( out );
      all.append( page );
      const QVariant next = out.value( QStringLiteral( "nextCursor" ) );
      if ( page.isEmpty() || !next.isValid() || next.toInt() < 0 )
        break;
      cursor = next.toInt();
    }
    return all;
  };

  const auto cliIds = []( const QStringList &flagArgs ) {
    QStringList args{ QStringLiteral( "algorithms" ), QStringLiteral( "search" ) };
    args << flagArgs << QStringLiteral( "--json" ) << QStringLiteral( "--limit" )
         << QStringLiteral( "500" );
    const QVariantMap envelope = runCliSearch( args, nullptr );
    REQUIRE( envelope.value( QStringLiteral( "ok" ) ).toBool() );
    const QVariantMap data = envelope.value( QStringLiteral( "data" ) ).toMap();
    QStringList ids;
    for ( const QVariant &entry : data.value( QStringLiteral( "algorithms" ) ).toList() )
      ids.push_back( entry.toMap().value( QStringLiteral( "id" ) ).toString() );
    return ids;
  };

  const auto rsOnly = []( const QStringList &ids ) {
    QStringList out;
    for ( const QString &id : ids )
      if ( id.startsWith( QStringLiteral( "rs:" ) ) )
        out.push_back( id );
    return out;
  };

  // 1. free text
  CHECK( rsOnly( mcpIds( { { QStringLiteral( "query" ), QStringLiteral( "sar" ) } } ) )
         == rsOnly( cliIds( { QStringLiteral( "sar" ) } ) ) );
  // 2. tag filter — the documented-but-unimplemented parameter this track lands
  CHECK( rsOnly( mcpIds( { { QStringLiteral( "tag" ), QStringLiteral( "sar" ) } } ) )
         == rsOnly( cliIds( { QStringLiteral( "--tag" ), QStringLiteral( "sar" ) } ) ) );
  // 3. task + modality combination
  CHECK( rsOnly( mcpIds( { { QStringLiteral( "task" ), QStringLiteral( "temporal" ) },
                           { QStringLiteral( "modality" ), QStringLiteral( "raster" ) } } ) )
         == rsOnly( cliIds( { QStringLiteral( "--task" ), QStringLiteral( "temporal" ),
                              QStringLiteral( "--modality" ), QStringLiteral( "raster" ) } ) ) );
  // 4. structured type filter
  CHECK( rsOnly( mcpIds( { { QStringLiteral( "input_type" ), QStringLiteral( "vector" ) } } ) )
         == rsOnly( cliIds( { QStringLiteral( "--input-type" ),
                              QStringLiteral( "vector" ) } ) ) );
  // 5. ordering is identical, not merely set-equal
  const QStringList m = mcpIds( { { QStringLiteral( "query" ), QStringLiteral( "rs:sar" ) } } );
  const QStringList c = cliIds( { QStringLiteral( "rs:sar" ) } );
  REQUIRE( !m.isEmpty() );
  CHECK( rsOnly( m ) == rsOnly( c ) );
}

TEST_CASE( "algorithm search: MCP surface maps query bounds to a typed tool error",
           "[search][mcp][bounds]" )
{
  operators::RSOperatorRegistry::instance();
  operators::rs::initBuiltinRsOperators();
  operators::rs::installRsOperatorProvider();
  processing::AtomicAlgorithmRegistry::instance().initialize();

  // Oversized free text -> typed refusal (isError tool result), not a crash
  // or a silent truncation.
  const QString huge( processing::AlgorithmSearchQuery::kMaxTextLength + 10,
                      QLatin1Char( 'x' ) );
  const bool ok = mcp().callTool( QStringLiteral( "search_algorithms" ),
                                  { { QStringLiteral( "query" ), huge } } );
  CHECK( !ok );
  CHECK( mcp().lastResponseResult.value( QStringLiteral( "isError" ) ).toBool() );
  CHECK( mcp().lastResponseResult.value( QStringLiteral( "errorCode" ) ).toString()
         == QStringLiteral( "INVALID_QUERY" ) );

  // Zero hits still succeed and carry the honest vocabulary hint.
  REQUIRE( mcp().callTool( QStringLiteral( "search_algorithms" ),
                           { { QStringLiteral( "tag" ),
                               QStringLiteral( "no_such_tag_exists" ) } } ) );
  const QVariantMap out = mcp().toolOutput();
  CHECK( out.value( QStringLiteral( "count" ) ).toInt() == 0 );
  CHECK( out.value( QStringLiteral( "total" ) ).toInt() == 0 );
  const QVariantMap hints = out.value( QStringLiteral( "hints" ) ).toMap();
  CHECK( hints.contains( QStringLiteral( "vocabulary" ) ) );
  const QVariantMap vocabulary = hints.value( QStringLiteral( "vocabulary" ) ).toMap();
  CHECK( vocabulary.contains( QStringLiteral( "taskFamilies" ) ) );
  CHECK( vocabulary.contains( QStringLiteral( "dataTypes" ) ) );
}

// ===========================================================================
// Provider union: MCP searches registry descriptors + QGIS processing
// providers through adapter descriptors — the path list_algorithms has
// always exposed and search must not silently drop.
// ===========================================================================

TEST_CASE( "algorithm search: MCP union covers provider algorithms via adapter descriptors",
           "[search][mcp][provider]" )
{
  operators::RSOperatorRegistry::instance();
  operators::rs::initBuiltinRsOperators();
  operators::rs::installRsOperatorProvider();
  processing::AtomicAlgorithmRegistry::instance().reset();
  processing::AtomicAlgorithmRegistry::instance().initialize();

  // A provider registered straight into the processing registry — never
  // mirrored into the AtomicAlgorithmRegistry — so its algorithms only
  // reach search through the provider-union branch.
  if ( !QgsApplication::processingRegistry()->providerById( QStringLiteral( "qgis_algorithms" ) ) )
    QgsApplication::processingRegistry()->addProvider( new QgisAlgorithmsProvider() );

  const auto allPages = []( const QVariantMap &args ) {
    QVariantList all;
    int cursor = 0;
    for ( ;; )
    {
      QVariantMap callArgs = args;
      callArgs[QStringLiteral( "limit" )] = 500;
      callArgs[QStringLiteral( "cursor" )] = cursor;
      REQUIRE( mcp().callTool( QStringLiteral( "search_algorithms" ), callArgs ) );
      const QVariantMap out = mcp().toolOutput();
      all.append( out.value( QStringLiteral( "algorithms" ) ).toList() );
      const QVariant next = out.value( QStringLiteral( "nextCursor" ) );
      if ( !next.isValid() || next.toInt() < 0 )
        break;
      cursor = next.toInt();
    }
    return all;
  };

  // Whole-union scan: provider entries appear (source=provider), every id
  // unique (registry-wins dedup keeps one entry per id).
  const QVariantList all = allPages( {} );
  REQUIRE_FALSE( all.isEmpty() );
  QSet<QString> ids;
  int providerCount = 0;
  for ( const QVariant &entryVar : all )
  {
    const QVariantMap entry = entryVar.toMap();
    const QString id = entry.value( QStringLiteral( "id" ) ).toString();
    CHECK( !ids.contains( id ) );
    ids.insert( id );
    if ( entry.value( QStringLiteral( "source" ) ).toString() == QStringLiteral( "provider" ) )
      ++providerCount;
  }
  CHECK( providerCount > 0 );

  // Facet coverage restored: provider adapter descriptors carry typed
  // ports, so input_type=vector matches native provider algorithms — the
  // same contract the old findAdapter-based filter produced.
  const QVariantList vecHits = allPages( { { QStringLiteral( "input_type" ),
                                           QStringLiteral( "vector" ) } } );
  bool providerVectorHit = false;
  for ( const QVariant &entryVar : vecHits )
    if ( entryVar.toMap().value( QStringLiteral( "source" ) ).toString()
         == QStringLiteral( "provider" ) )
    {
      providerVectorHit = true;
      break;
    }
  CHECK( providerVectorHit );
}

// ===========================================================================
// CLI hostile paths: malformed invocations are typed refusals, zero hits
// still carry the honest vocabulary hints.
// ===========================================================================

TEST_CASE( "algorithm search: CLI rejects malformed invocations and surfaces hints",
           "[search][cli][bounds]" )
{
  const auto runFor = []( const QStringList &tail ) {
    QStringList args{ QStringLiteral( "algorithms" ), QStringLiteral( "search" ) };
    args << tail << QStringLiteral( "--json" );
    int code = -1;
    const QVariantMap env = runCliSearch( args, &code );
    return std::make_pair( env, code );
  };

  // bare search: no text, no filters -> usage error
  {
    const auto [env, code] = runFor( {} );
    CHECK_FALSE( env.value( QStringLiteral( "ok" ) ).toBool() );
    CHECK( code != 0 );
  }
  // non-integer --limit -> typed refusal
  {
    const auto [env, code] = runFor( { QStringLiteral( "sar" ),
                                       QStringLiteral( "--limit" ),
                                       QStringLiteral( "abc" ) } );
    CHECK_FALSE( env.value( QStringLiteral( "ok" ) ).toBool() );
    CHECK( code != 0 );
  }
  // unknown flag -> typed refusal
  {
    const auto [env, code] = runFor( { QStringLiteral( "sar" ),
                                       QStringLiteral( "--bogus" ) } );
    CHECK_FALSE( env.value( QStringLiteral( "ok" ) ).toBool() );
    CHECK( code != 0 );
  }
  // value-less flag must not eat the next flag as its value
  {
    const auto [env, code] = runFor( { QStringLiteral( "--group" ),
                                       QStringLiteral( "--tag" ),
                                       QStringLiteral( "sar" ) } );
    CHECK_FALSE( env.value( QStringLiteral( "ok" ) ).toBool() );
    CHECK( code != 0 );
  }
  // zero hits stay a success and carry vocabulary hints
  {
    const auto [env, code] = runFor( { QStringLiteral( "--tag" ),
                                       QStringLiteral( "no_such_tag_exists" ) } );
    CHECK( env.value( QStringLiteral( "ok" ) ).toBool() );
    CHECK( code == 0 );
    const QVariantMap data = env.value( QStringLiteral( "data" ) ).toMap();
    CHECK( data.value( QStringLiteral( "count" ) ).toInt() == 0 );
    const QVariantMap hints = data.value( QStringLiteral( "hints" ) ).toMap();
    CHECK( hints.contains( QStringLiteral( "vocabulary" ) ) );
    CHECK( hints.value( QStringLiteral( "vocabulary" ) )
               .toMap()
               .value( QStringLiteral( "task_families" ) )
               .isValid() );
  }
}
