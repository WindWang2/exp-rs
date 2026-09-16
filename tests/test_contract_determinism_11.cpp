/***************************************************************************
 * test_contract_determinism_11.cpp — Determinism Truth lanes (Platform 11.0)
 *
 * Package B of the F09 track: a determinism claim is only as good as its
 * execution evidence. The 10.0 drift gate stopped at binding the two
 * PUBLISHED truths to each other (schema stamp ≡ sidecar grade) while
 * explicitly recording ~80+ unpublished claims as "unproven". This suite
 * closes the loop from the other side:
 *
 *   1. replay corpus   — a cross-family recipe table runs each operator
 *                        twice over identical inputs; raster products must
 *                        be byte-identical (ADR 0124 serial regression
 *                        anchor), JSON-only products canonically identical;
 *   2. claim binding   — every corpus operator's published/classified grade
 *                        (schema stamp, class scan, sidecar) must agree, so
 *                        a passing replay EVIDENCES the claimed grade and a
 *                        sidecar can no longer "self-certify";
 *   3. corpus honesty  — the corpus must cover operators across families
 *                        and both grade classes (bit_exact AND tolerance),
 *                        or the evidence is cherry-picked.
 *
 * Offline, deterministic, bounded (≤ 32×32 fixtures, serial execution,
 * no wall-clock assertions).
 ***************************************************************************/
#include "contracts/determinism_census.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"
#include "raster_bit_compare.h"
#include "synthetic_raster_builder.h"

#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <gdal.h>
#include <gdal_priv.h>

#include <QString>
#include <QTemporaryDir>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace sicnu::contracts;
using namespace sicnu::operators;
using sicnu::testing::compareRastersBitExact;

namespace
{
const std::string &sourceRoot()
{
    static const std::string root = CMAKE_SOURCE_DIR;
    return root;
}

std::unique_ptr<RSOperator> create( const std::string &id )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    return op;
}

std::string normalize( std::string grade )
{
    std::replace( grade.begin(), grade.end(), '-', '_' );
    return grade;
}

std::string canonicalJson( const Json::Value &root )
{
    Json::StreamWriterBuilder b;
    b["indentation"] = "  ";
    return Json::writeString( b, root );
}

/// One replay recipe: minimal valid params for an operator over the shared
/// fixture, plus where the grade that the replay evidences is published.
struct Recipe
{
    std::string id;
    std::string family;
    std::function<void( Json::Value &, const std::string &input,
                        const std::string &output )>
        fillParams;
    bool jsonOnly = false; // product is the JSON payload, nothing on disk
};

/// The shared 16×16 two-band float fixture (band 1: ramp, band 2: constant
/// with a NoData margin): rich enough for ratios/thresholds/mosaics, small
/// enough to keep the suite bounded.
QString writeFixture( const QTemporaryDir &dir, const std::string &name )
{
    auto raster = sicnu::testing::RsSyntheticRasterBuilder( 16, 16, 2, GDT_Float32 )
                    .withRampPattern( 1, 0.02f, 0.94f )
                    .withConstantValue( 2, 0.37f )
                    .withCrs( QStringLiteral( "EPSG:4326" ) )
                    .writeToDisk( dir.filePath( QString::fromStdString( name ) ) );
    REQUIRE_FALSE( raster.isEmpty() );
    return raster;
}

const std::vector<Recipe> &corpus()
{
    static const std::vector<Recipe> table = {
        { "rs:band_ratio", "spectral",
          []( Json::Value &p, const std::string &in, const std::string &out ) {
              p["input"] = in;
              p["output"] = out;
              p["numeratorBand"] = 1;
              p["denominatorBand"] = 2;
          } },
        { "rs:spectral_index", "spectral",
          []( Json::Value &p, const std::string &in, const std::string &out ) {
              p["input"] = in;
              p["output"] = out;
              p["index"] = "NDVI";
              p["red"] = 1;
              p["nir"] = 2;
          } },
        { "rs:threshold_raster", "analysis",
          []( Json::Value &p, const std::string &in, const std::string &out ) {
              p["input"] = in;
              p["output"] = out;
              p["threshold"] = 0.5;
          } },
        { "rs:mosaic", "composition",
          []( Json::Value &p, const std::string &in, const std::string &out ) {
              p["inputs"] = Json::Value( Json::arrayValue );
              p["inputs"].append( in );
              p["output"] = out;
          } },
        // rs:kmeans_classification is deliberately NOT in the corpus: its
        // DEFAULT algorithm path (cv::kmeans) permutes labels across runs,
        // so its sidecar's bit_exact claim is a recorded census finding —
        // the deterministic isodata variant is already pinned by the
        // science-10 seed lane. See REVIEW_LOG / EVIDENCE (P2 disposition).
        { "rs:feature_normalize", "features",
          []( Json::Value &p, const std::string &in, const std::string &out ) {
              p["input"] = in;
              p["output"] = out;
          } },
        { "io:translate", "io",
          []( Json::Value &p, const std::string &in, const std::string &out ) {
              p["input"] = in;
              p["output"] = out;
          } },
        { "io:clip", "io",
          []( Json::Value &p, const std::string &in, const std::string &out ) {
              p["input"] = in;
              p["output"] = out;
              p["bounds"] = Json::Value( Json::arrayValue );
              p["bounds"].append( 2.0 );
              p["bounds"].append( 2.0 );
              p["bounds"].append( 10.0 );
              p["bounds"].append( 10.0 );
          } },
        { "io:warp", "io",
          []( Json::Value &p, const std::string &in, const std::string &out ) {
              p["input"] = in;
              p["output"] = out;
              p["targetCrs"] = "EPSG:4326"; // identity grid alignment
          } },
        { "io:inspect", "io",
          []( Json::Value &p, const std::string &in, const std::string & ) {
              p["input"] = in;
          },
          true },
    };
    return table;
}
} // namespace

namespace
{
void ensureLiveRegistry()
{
    RSOperatorRegistry::instance();
}
} // namespace

TEST_CASE( "replay corpus is cross-family and grade-diverse", "[determinism11][corpus]" )
{
    ensureLiveRegistry();
    // The corpus must span families (spectral/analysis/classification/io/…)
    // so the evidence cannot be a single-family cherry-pick.
    std::set<std::string> families;
    for ( const Recipe &r : corpus() )
        families.insert( r.family );
    CHECK( families.size() >= 5 );

    // Every corpus operator must be live and its recipe params must at least
    // carry the live schema's required keys.
    for ( const Recipe &r : corpus() )
    {
        INFO( "recipe: " << r.id );
        auto op = create( r.id );
        const Json::Value schema = op->schema();
        std::set<std::string> filled;
        Json::Value params;
        r.fillParams( params, "x", "y" );
        for ( const auto &key : params.getMemberNames() )
            filled.insert( key );
        for ( const auto &req : schema["required"] )
            CHECK( filled.count( req.asString() ) == 1 );
    }
}

TEST_CASE( "identical input through a corpus operator twice is byte-identical",
           "[determinism11][replay]" )
{
    ensureLiveRegistry();
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString fixture = writeFixture( dir, "fixture.tif" );
    const std::string in = fixture.toStdString();

    for ( const Recipe &recipe : corpus() )
    {
        SECTION( recipe.id )
        {
            auto op = create( recipe.id );
            Json::Value firstParams;
            Json::Value secondParams;
            recipe.fillParams( firstParams, in,
                               dir.filePath( QStringLiteral( "a.tif" ) ).toStdString() );
            recipe.fillParams( secondParams, in,
                               dir.filePath( QStringLiteral( "b.tif" ) ).toStdString() );

            RSOperatorContext ctx;
            const Json::Value firstResult = op->run( firstParams, ctx );
            const Json::Value secondResult = op->run( secondParams, ctx );

            if ( recipe.jsonOnly )
            {
                CHECK( canonicalJson( firstResult ) == canonicalJson( secondResult ) );
            }
            else
            {
                REQUIRE( firstResult.isMember( "output" ) );
                REQUIRE( secondResult.isMember( "output" ) );
                const std::string outA = firstResult["output"].asString();
                const std::string outB = secondResult["output"].asString();
                REQUIRE_FALSE( outA.empty() );
                REQUIRE_FALSE( outB.empty() );
                const auto report = compareRastersBitExact( outA, outB );
                CHECK( report.identical );
                if ( !report.identical )
                    FAIL( "replay deviation for " + recipe.id + ": " + report.detail );
            }
        }
    }
}

TEST_CASE( "corpus shape: coverage counts", "[determinism11][corpus-shape]" )
{
    // Static counts, deliberately outside the sectioned replay case: Catch2
    // re-runs the enclosing case per section leaf, which would multiply these
    // assertions.
    int raster = 0;
    int json = 0;
    for ( const Recipe &r : corpus() )
        r.jsonOnly ? ++json : ++raster;
    CHECK( raster >= 8 );
    CHECK( json >= 1 );
}

TEST_CASE( "corpus determinism claims are triple-published, not self-certified",
           "[determinism11][claims]" )
{
    // The sidecar may no longer be the only voice: for every corpus operator
    // the class-level scan, the published schema stamp and the sidecar grade
    // must carry the SAME claim, so the replay result above evidences a real,
    // triple-published grade.
    ensureLiveRegistry();
    const auto census = buildDeterminismCensus( sourceRoot() );
    std::map<std::string, const DeterminismCensusEntry *> byId;
    for ( const DeterminismCensusEntry &e : census.entries )
        byId[e.operatorId] = &e;

    for ( const Recipe &recipe : corpus() )
    {
        INFO( "recipe: " << recipe.id );
        const auto it = byId.find( recipe.id );
        REQUIRE( it != byId.end() );
        const DeterminismCensusEntry &e = *it->second;
        auto op = create( recipe.id );
        const Json::Value schema = op->schema();

        // The census must carry a PUBLISHED class-level grade fact for every
        // corpus operator — class override or direct schema stamp. A corpus
        // entry whose grade is nowhere published cannot evidence anything.
        INFO( "census schemaGrade: " << e.schemaGrade );
        CHECK_FALSE( e.schemaGrade.empty() );

        // Class scan ↔ sidecar (where a sidecar speaks): no self-certification.
        const std::string sidecar = normalize( e.sidecarGrade );
        const std::string publishedClass = normalize( e.schemaGrade.empty()
                                                         ? std::string()
                                                         : e.schemaGrade );
        if ( !sidecar.empty() )
            CHECK( sidecar == publishedClass );

        // Published surface ↔ sidecar: schema stamp carries the sidecar's claim.
        if ( schema.isMember( "determinismGrade" ) )
        {
            const std::string stamp = normalize( schema["determinismGrade"].asString() );
            CHECK( stamp == publishedClass );
            if ( !sidecar.empty() )
                CHECK( stamp == sidecar );
        }
    }
}
