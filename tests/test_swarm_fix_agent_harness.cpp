// tests/test_swarm_fix_agent_harness.cpp
// swarm-review 2026-11-04 — regression anchors for the agent-harness fixes:
//   AG-H-1  confirmMapOutput: every repair round must feed cartography:repair's
//           REPAIRED mapspec forward (repair is a pure function whose only
//           result channel is output["mapspec"]).
//   AG-H-3  artifact_read text mode: a 256 KiB chunk boundary cutting a
//           multi-byte UTF-8 sequence is stripped and counted, not rejected;
//           whole-file reads stay strictly validated.
//   AG-H-4  the harness error-code table is duplicate-free (the static_assert
//           in harness_error.cpp guards this at compile time; this is the
//           runtime anchor for the listing surfaces).
//   AG-H-6  gain_invariance bands are 1-based: a 0/negative band is a usage
//           error at rules-parse time instead of an out_of_range throw at
//           grading time.
// AG-H-2 / AG-H-5 (lock discipline and insert-before-evict on the bounded
// ledgers) are invariant fixes whose observable contract is pinned by the
// consuming surfaces (harness:run_status repair_attempts, harness:diagnose_run
// budget); the compile-time guards live next to the tables.
//
// Runs with its own harness main(): QgsApplication is required because the
// map-confirmation path compiles a real QgsPrintLayout through
// cartography:compose.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <qgis.h>
#include <qgsapplication.h>
#include <qgsproject.h>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "agent/cartography/cartography_tools.h"
#include "agent/harness/agent_plan.h"
#include "agent/harness/harness_error.h"
#include "agent/harness/plan_tools.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mcp_server.h"
#include "agent/output_verifier.h"
#include "workflow/workflow_run.h"

#ifndef SICNU_CARTOGRAPHY_DATA_DIR
#define SICNU_CARTOGRAPHY_DATA_DIR "data/cartography"
#endif

using namespace sicnu::agent::harness;
using namespace sicnu::agent::mapspec;
using namespace sicnu::workflow;

namespace {

/// Minimal probe over the protected handler so the test exercises the real
/// bounded reader (same pattern as TestMcpServer in test_mcp_server.cpp).
class ArtifactReadProbe : public McpServer
{
  public:
    using McpServer::McpServer;
    QVariantMap read( const QVariantMap &arguments ) { return handleArtifactRead( arguments ); }
};

/// Restores SICNU_MCP_WORKSPACE (the artifact_read sandbox root) afterwards.
class WorkspaceEnvGuard
{
  public:
    explicit WorkspaceEnvGuard( const QString &root )
    {
        m_had = qEnvironmentVariableIsSet( "SICNU_MCP_WORKSPACE" );
        m_old = QString::fromUtf8( qgetenv( "SICNU_MCP_WORKSPACE" ) );
        qputenv( "SICNU_MCP_WORKSPACE", root.toUtf8() );
    }
    ~WorkspaceEnvGuard()
    {
        if ( m_had )
            qputenv( "SICNU_MCP_WORKSPACE", m_old.toUtf8() );
        else
            qunsetenv( "SICNU_MCP_WORKSPACE" );
    }

  private:
    bool m_had = false;
    QString m_old;
};

/// A structurally valid, otherwise-clean mapspec (map frame + full furniture,
/// no declared margins) — the proven-clean baseline of the cartography
/// suites. `withTitle == false` drops the title, leaving exactly ONE
/// repairable defect (MAP_MISSING_TITLE, repaired by adding the furniture).
Json::Value cleanMapSpec( const std::string &layoutName, bool withTitle = true )
{
    Json::Value spec = makeMapSpec( layoutName, Json::Value() );

    Json::Value frame( Json::objectValue );
    frame["id"] = "map-1";
    Json::Value frameRect( Json::arrayValue );
    frameRect.append( 12 );
    frameRect.append( 24 );
    frameRect.append( 190 );
    frameRect.append( 160 );
    frame["rect_mm"] = frameRect;
    Json::Value extent( Json::arrayValue );
    extent.append( 116.0 );
    extent.append( 39.0 );
    extent.append( 117.0 );
    extent.append( 40.0 );
    frame["extent"] = extent;
    spec["map_frames"].append( frame );

    if ( withTitle )
    {
        Json::Value title( Json::objectValue );
        title["id"] = "title-1";
        title["text"] = "A reasonably short title";
        Json::Value titleRect( Json::arrayValue );
        titleRect.append( 12 );
        titleRect.append( 6 );
        titleRect.append( 200 );
        titleRect.append( 12 );
        title["rect_mm"] = titleRect;
        spec["titles"].append( title );
    }

    Json::Value legend( Json::objectValue );
    legend["id"] = "legend-1";
    Json::Value legendRect( Json::arrayValue );
    legendRect.append( 210 );
    legendRect.append( 30 );
    legendRect.append( 70 );
    legendRect.append( 80 );
    legend["rect_mm"] = legendRect;
    legend["map_ref"] = "map-1";
    spec["legends"].append( legend );

    Json::Value scaleBar( Json::objectValue );
    scaleBar["id"] = "scalebar-1";
    Json::Value barRect( Json::arrayValue );
    barRect.append( 14 );
    barRect.append( 188 );
    barRect.append( 48 );
    barRect.append( 10 );
    scaleBar["rect_mm"] = barRect;
    scaleBar["map_ref"] = "map-1";
    spec["scale_bars"].append( scaleBar );

    Json::Value arrow( Json::objectValue );
    arrow["id"] = "arrow-1";
    arrow["semantic_role"] = "north_arrow.primary";
    Json::Value arrowRect( Json::arrayValue );
    arrowRect.append( 68 );
    arrowRect.append( 188 );
    arrowRect.append( 12 );
    arrowRect.append( 12 );
    arrow["rect_mm"] = arrowRect;
    arrow["map_ref"] = "map-1";
    spec["north_arrows"].append( arrow );

    Json::Value note( Json::objectValue );
    note["semantic_role"] = "source.primary";
    note["id"] = "note-1";
    note["text"] = "Source: bench";
    Json::Value noteRect( Json::arrayValue );
    noteRect.append( 120 );
    noteRect.append( 190 );
    noteRect.append( 130 );
    noteRect.append( 8 );
    note["rect_mm"] = noteRect;
    spec["source_notes"].append( note );

    return spec;
}

/// A small, real, verifiable raster so the run verifies clean and the map
/// confirmation path is reached at all. Empty string on failure.
QString writeVerifiableRaster( const QTemporaryDir &dir, const QString &name )
{
    static const bool kRegistered = [] { GDALAllRegister(); return true; }();
    ( void )kRegistered;
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    if ( driver == nullptr )
        return QString();
    const QString path = dir.filePath( name );
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    if ( ds == nullptr )
        return QString();
    double gt[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
    ds->SetGeoTransform( gt );
    OGRSpatialReference srs;
    if ( srs.importFromEPSG( 32650 ) == OGRERR_NONE )
    {
        char *wkt = nullptr;
        srs.exportToWkt( &wkt );
        ds->SetProjection( wkt );
        CPLFree( wkt );
    }
    float row[8] = {};
    for ( int y = 0; y < 8; ++y )
        ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, 8, 1, row, 8, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return path;
}

} // namespace

int main( int argc, char *argv[] )
{
    qputenv( "SICNU_CARTOGRAPHY_DIR", SICNU_CARTOGRAPHY_DATA_DIR );
    QgsApplication application( argc, argv, true );
    QgsApplication::initQgis();
    // confirmMapOutput resolves cartography:compose/preflight/repair through
    // the spatial-tool registry (idempotent registration).
    sicnu::agent::cartography::registerCartographyTools();
    const int result = Catch::Session().run( argc, argv );
    QgsProject::instance()->clear();
    QgsApplication::exitQgis();
    return result;
}

TEST_CASE( "confirmMapOutput converges: every repair round feeds the repaired mapspec forward",
           "[swarm-fix][harness][ag-h-1]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString artifact = writeVerifiableRaster( dir, QStringLiteral( "map-output.tif" ) );
    REQUIRE_FALSE( artifact.isEmpty() );
    REQUIRE( QFileInfo::exists( artifact ) );

    // The ONLY defect is the missing title: MAP_MISSING_TITLE is repairable
    // (the repair adds the furniture), so one repair pass converges.
    Json::Value spec = cleanMapSpec( "swarm-fix-map", /*withTitle=*/false );

    // Self-validating fixture: the defect fires and the public repair
    // contract converges — the map-confirmation loop below must reproduce
    // exactly this outcome.
    const Json::Value before = sicnu::agent::cartography::preflightMapSpec( spec );
    CHECK_FALSE( before["passed"].asBool() );
    Json::Value repairedSpec = spec;
    CHECK( sicnu::agent::cartography::repairMapSpec( repairedSpec, before ) >= 1 );
    CHECK( sicnu::agent::cartography::preflightMapSpec( repairedSpec )["passed"].asBool() );

    AgentPlan plan;
    plan.planId = "plan-swarm-fix-map";
    plan.goal = "map confirmation repair convergence";
    plan.mapOutput = Json::Value( Json::objectValue );
    plan.mapOutput["from_step"] = "map-step";
    plan.mapOutput["layout_name"] = "swarm-fix-map";
    plan.mapOutput["mapspec"] = spec;

    auto run = std::make_shared<WorkflowRun>();
    REQUIRE( run->setRunId( "run-swarm-fix-map" ) );
    StepPlan step;
    step.stepId = "map-step";
    step.operatorId = "rs:contrast_stretch";
    step.status = "Completed";
    step.outputLayerPath = artifact.toStdString();
    run->setStepPlans( { step } );
    run->forceSetState( WorkflowRunState::Completed );

    const Json::Value doc = runResultDocument( run, &plan );

    // Precondition: the run itself verifies clean, so the confirmation runs.
    CHECK( doc["verification"]["verdict"].asString() != "FAIL" );
    REQUIRE( doc.isMember( "map_confirmation" ) );
    const Json::Value confirmation = doc["map_confirmation"];

    // AG-H-1: the repair loop re-fed the ORIGINAL spec every round, so the
    // missing title survived all three attempts and the verdict was
    // permanently FAIL with repair_passes == 3. Feeding the repaired spec
    // forward makes the loop converge.
    CHECK( confirmation["verdict"].asString() == "PASS" );
    CHECK( confirmation["repair_passes"].asInt() >= 1 );
    CHECK( confirmation["repair_passes"].asInt() <= 3 );
    CHECK( confirmation.isMember( "quality_score" ) );
}

TEST_CASE( "artifact_read pages CJK text across 256 KiB chunk boundaries losslessly",
           "[swarm-fix][mcp][ag-h-3]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const WorkspaceEnvGuard workspace( dir.path() );

    // 15000 x "中文测试数据\n" = 285000 bytes of valid UTF-8 whose byte
    // layout is NOT aligned to the 256 KiB chunk: byte 262144 lands inside
    // the 3-byte sequence for the first character of unit 13797, so chunk 1
    // ends mid-character.
    const QByteArray unit = QString::fromUtf8( "中文测试数据\n" ).toUtf8();
    REQUIRE( unit.size() == 19 );
    QByteArray fileBytes;
    fileBytes.reserve( unit.size() * 15000 );
    for ( int i = 0; i < 15000; ++i )
        fileBytes.append( unit );
    REQUIRE( fileBytes.size() == 285000 );

    QFile file( dir.filePath( QStringLiteral( "cjk-big.txt" ) ) );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( fileBytes ) == fileBytes.size() );
    file.close();

    ArtifactReadProbe server;

    // Cursor walk: before the fix page 1 was rejected outright as "slice is
    // not valid UTF-8" (the chunk boundary cut a CJK sequence).
    QString reassembled;
    qint64 offset = 0;
    int pages = 0;
    for ( ;; )
    {
        QVariantMap args;
        args[QStringLiteral( "path" )] = QStringLiteral( "cjk-big.txt" );
        if ( offset > 0 )
            args[QStringLiteral( "offset" )] = offset;
        const QVariantMap page = server.read( args );
        ++pages;
        REQUIRE( pages <= 4 ); // a spin would fail here
        reassembled += page.value( QStringLiteral( "content" ) ).toString();
        if ( !page.value( QStringLiteral( "truncated" ) ).toBool() )
        {
            CHECK_FALSE( page.contains( QStringLiteral( "next_offset" ) ) );
            break;
        }
        const qint64 next = page.value( QStringLiteral( "next_offset" ) ).toLongLong();
        REQUIRE( next > offset ); // next_offset strictly advances
        offset = next;
    }
    // Pagination is lossless: stripping the boundary-incomplete sequence
    // counts into length/next_offset, so nothing is dropped or duplicated.
    CHECK( reassembled == QString::fromUtf8( fileBytes ) );

    // A cursor parked mid-sequence (262144 is the 2nd byte of a CJK
    // character): the leading continuation bytes are stripped and counted,
    // so the page is valid text starting at the next complete character
    // instead of a rejection.
    QVariantMap midArgs;
    midArgs[QStringLiteral( "path" )] = QStringLiteral( "cjk-big.txt" );
    midArgs[QStringLiteral( "offset" )] = static_cast<qint64>( 262144 );
    const QVariantMap mid = server.read( midArgs );
    CHECK( mid.value( QStringLiteral( "content" ) ).toString().startsWith(
               QString::fromUtf8( "文" ) ) );
    CHECK( mid.value( QStringLiteral( "length" ) ).toLongLong() ==
           static_cast<qint64>( 285000 - 262146 ) );
}

TEST_CASE( "artifact_read text mode still rejects non-UTF-8 whole reads",
           "[swarm-fix][mcp][ag-h-3]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const WorkspaceEnvGuard workspace( dir.path() );

    const char bytes[] = { char( 0xff ), char( 0xfe ), 'a', 'b' };
    QFile file( dir.filePath( QStringLiteral( "binary.bin" ) ) );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    REQUIRE( file.write( bytes, static_cast<qint64>( sizeof( bytes ) ) ) ==
             static_cast<qint64>( sizeof( bytes ) ) );
    file.close();

    ArtifactReadProbe server;
    QVariantMap args;
    args[QStringLiteral( "path" )] = QStringLiteral( "binary.bin" );
    // Strict validation is reserved for whole-file reads (offset==0 &&
    // !truncated); a genuinely binary file must still be refused there.
    REQUIRE_THROWS_AS( server.read( args ), std::runtime_error );

    // base64 remains the escape hatch and stays byte-exact.
    args[QStringLiteral( "encoding" )] = QStringLiteral( "base64" );
    const QVariantMap b64 = server.read( args );
    const QByteArray decoded =
        QByteArray::fromBase64( b64.value( QStringLiteral( "content" ) ).toString().toLatin1() );
    CHECK( decoded == QByteArray( bytes, static_cast<int>( sizeof( bytes ) ) ) );
}

TEST_CASE( "the harness error code table is duplicate-free", "[swarm-fix][harness][ag-h-4]" )
{
    const std::vector<std::string> codes = allErrorCodes();
    const std::set<std::string> unique( codes.begin(), codes.end() );
    // AG-H-4: WAVELENGTH_INCOMPATIBLE was listed in both the Compiler 10.0
    // and the InSAR 11.0 sections, so every listing surface emitted it
    // twice. (harness_error.cpp also static_asserts table uniqueness.)
    CHECK( unique.size() == codes.size() );
    CHECK( std::count( codes.begin(), codes.end(), "WAVELENGTH_INCOMPATIBLE" ) == 1 );
    CHECK( isKnownErrorCode( "WAVELENGTH_INCOMPATIBLE" ) );
    CHECK( errorCategoryForCode( "WAVELENGTH_INCOMPATIBLE" ) == "validation" );
}

TEST_CASE( "gain_invariance rejects 0-based band indices before grading",
           "[swarm-fix][verifier][ag-h-6]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // parseRules requires the file stem to be <lab_id>.rules.
    const auto writeRules = [ &dir ]( const QString &labId, int bandA ) {
        const QString path = dir.filePath( labId + QStringLiteral( ".rules.json" ) );
        QFile file( path );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
            return QString();
        const QByteArray body = QStringLiteral(
            "{ \"schema_version\": \"sicnu.lab.rules/1\", \"lab_id\": \"%1\","
            "  \"title\": \"swarm-fix gain_invariance probe\","
            "  \"artifact\": { \"kind\": \"raster\" }, \"passing_score\": 60,"
            "  \"assertions\": [ { \"id\": \"calib.gain_invariance\","
            "    \"kind\": \"gain_invariance\", \"weight\": 100, \"severity\": \"normal\","
            "    \"params\": { \"bands\": [ %2, %3 ], \"tolerance\": 0.5 } } ] }" )
                .arg( labId ).arg( bandA ).arg( bandA + 1 ).toUtf8();
        if ( file.write( body ) != body.size() )
            return QString();
        file.close();
        return path;
    };

    sicnu::agent::OutputVerifier verifier;
    const sicnu::agent::OutputVerifier::LabGradeOptions options;
    const QString missingArtifact = QStringLiteral( "__swarm_fix_missing__.tif" );

    // band 0 used to pass validation and reach meta.bands.at(size_t(-1))
    // at grading time — an std::out_of_range escaping the grader.
    const QString badRules = writeRules( QStringLiteral( "swarm_fix_gain_zero" ), 0 );
    REQUIRE_FALSE( badRules.isEmpty() );
    const auto bad = verifier.gradeForTeaching( badRules, missingArtifact, options );
    CHECK_FALSE( bad.graded );
    CHECK( bad.errorClass == QStringLiteral( "usage" ) );
    CHECK( bad.error.contains( QStringLiteral( "1-based" ) ) );

    // Control: 1-based bands parse; grading then fails on the missing
    // artifact, never on the band indices.
    const QString goodRules = writeRules( QStringLiteral( "swarm_fix_gain_one" ), 1 );
    REQUIRE_FALSE( goodRules.isEmpty() );
    const auto good = verifier.gradeForTeaching( goodRules, missingArtifact, options );
    CHECK_FALSE( good.graded );
    CHECK( good.errorClass == QStringLiteral( "usage" ) );
    CHECK_FALSE( good.error.contains( QStringLiteral( "1-based" ) ) );
    CHECK( good.error.contains( QStringLiteral( "Artifact does not exist" ) ) );
}
