/***************************************************************************
  tests/test_r23_d_ct6_lab_flow.cpp — R23-D CT-6 closure regression.

  The repo-documented lab-1 student flow (scripts/windows/run_lab.cmd ->
    sicnu_generate_samples  ->  rs:spectral_index NDVI (nir=5, red=4)  ->
    lab --lab <id> --grade) must grade PASS offline, reproducibly, against a
  rules authority calibrated to the scene it actually consumes.

  Three oracles:
    1. foundry flow PASS  — generate (seed 42, in-process foundry) -> the
       production spectral-index core -> OutputVerifier::gradeArtifact against
       the foundry-calibrated authority (data/labs/grading/ndvi_landsat.rules.
       json) -> verdict pass, score >= passing line; grading twice is
       byte-deterministic (same digest).
    2. wrong-CRS control FAIL — the same product re-stamped EPSG:4326 must
       fail with a typed grid localization (grid.compatible deduction naming
       both authorities), never silently pass.
    3. dual-authority guard — the fixture authority stays intact: the
       committed reference raster still scores exactly 100 under ndvi_basics,
       and the foundry product under ndvi_basics still scores BELOW the pass
       line (the two scenes are different known answers; neither rules file
       may drift toward the other scene).

  Before R23-D the first oracle had no passing authority on the repo side
  (CT-6, .planning/r21-e/CROSS_TRACK_FINDINGS.md): the only repo rules were
  fixture-calibrated, so the documented flow graded 45/100.
 ***************************************************************************/

#include "agent/output_verifier.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/rs/rs_spectral_index_operator.h"
#include "sample_foundry.h"

#include <catch2/catch_test_macros.hpp>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <json/json.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <string>

using sicnu::agent::OutputVerifier;

namespace
{

struct GdalInit
{
    GdalInit() { GDALAllRegister(); }
};
const GdalInit s_gdalInit;

/// Generates the foundry lab scene (seed 42, lab profile — the exact
/// documents the student flow generates) into @a dir and returns the path of
/// landsat_sample.tif.
QString generateFoundryScene( const QString &dir )
{
    sicnu::foundry::Options options; // profile = Lab (256x256, 30 m, EPSG:32648), seed = 42
    options.out_dir = dir.toStdString();
    options.products = { sicnu::foundry::Product::LandsatSample }; // truth companion rides along
    const sicnu::foundry::Outcome outcome = sicnu::foundry::generate( options, nullptr );
    REQUIRE( outcome.ok );
    const QString input = QDir( dir ).filePath( QStringLiteral( "landsat_sample.tif" ) );
    REQUIRE( QFile::exists( input ) );
    return input;
}

/// Runs the lab-1 pipeline step through the production spectral-index core
/// (the registered rs:spectral_index operator delegates here in production —
/// rs_spectral_index_operator.cpp). Params mirror the committed pipeline
/// packaging/bundle/labs/lab1/lab1_ndvi.pipeline.json exactly.
QString runLab1NdviStep( const QString &input, const QString &output )
{
    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = output.toStdString();
    params["index"] = "NDVI";
    params["nir"] = 5;
    params["red"] = 4;

    sicnu::operators::RSOperatorContext context;
    const Json::Value result = sicnu::operators::rs::spectral_index_detail::runSpectralIndexCore(
        "NDVI", params, context, /*allowIndexOverride=*/true, "rs:spectral_index", {} );
    REQUIRE( result["output"].asString() == output.toStdString() );
    REQUIRE( QFile::exists( output ) );
    return output;
}

/// Clones @a src to @a dst re-stamped with the given EPSG authority code
/// (everything else — grid, pixels, values — stays byte-equal).
QString cloneWithCrs( const QString &src, const QString &dst, int epsg )
{
    GDALDataset *in = static_cast<GDALDataset *>(
      GDALOpen( src.toUtf8().constData(), GA_ReadOnly ) );
    REQUIRE( in != nullptr );
    OGRSpatialReference srs;
    REQUIRE( srs.importFromEPSG( epsg ) == OGRERR_NONE );
    char *wkt = nullptr;
    REQUIRE( srs.exportToWkt( &wkt ) == OGRERR_NONE );
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDataset *out = driver->CreateCopy( dst.toUtf8().constData(), in, FALSE,
                                           nullptr, nullptr, nullptr );
    REQUIRE( out != nullptr );
    REQUIRE( out->SetProjection( wkt ) == CE_None );
    GDALClose( out );
    GDALClose( in );
    CPLFree( wkt );
    REQUIRE( QFile::exists( dst ) );
    return dst;
}

/// Finds the deduction record for @a assertionId, nullptr when absent.
const OutputVerifier::LabDeduction *findDeduction( const OutputVerifier::LabGradeResult &grade,
                                                   const char *assertionId )
{
    for ( const auto &d : grade.deductions )
        if ( d.assertionId == QLatin1String( assertionId ) )
            return &d;
    return nullptr;
}

} // namespace

TEST_CASE( "ct6: foundry lab-1 student flow grades PASS against the foundry-calibrated authority",
           "[lab][ct6][e2e]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString input = generateFoundryScene( dir.path() );
    const QString artifact = runLab1NdviStep(
      input, QDir( dir.path() ).filePath( QStringLiteral( "lab1_ndvi.tif" ) ) );

    OutputVerifier verifier;
    const auto grade = verifier.gradeArtifact( QStringLiteral( "ndvi_landsat" ), artifact, {} );
    REQUIRE( grade.graded );
    INFO( "score: " << grade.score << " verdict: " << grade.verdict.toStdString()
          << " error: " << grade.error.toStdString() );
    for ( const auto &d : grade.deductions )
        INFO( "deduction " << d.assertionId.toStdString() << ": " << d.message.toStdString() );
    REQUIRE( grade.verdict == QStringLiteral( "pass" ) );
    REQUIRE( grade.score >= grade.passingScore );
    // The calibration targets a clean 100 (all five assertions); a full pass
    // leaves no deductions. Kept as an equality pin: the foundry
    // determinism contract + float32 index arithmetic are byte-stable, so a
    // score != 100 means real drift, not noise.
    REQUIRE( grade.score == 100.0 );
    REQUIRE( grade.deductions.empty() );

    // Determinism: identical inputs grade to byte-identical reports (the
    // digest covers the canonical body; no wall clock inside).
    const auto again = verifier.gradeArtifact( QStringLiteral( "ndvi_landsat" ), artifact, {} );
    REQUIRE( again.graded );
    REQUIRE( again.digest == grade.digest );
    REQUIRE( again.score == grade.score );
}

TEST_CASE( "ct6: wrong-CRS submission fails with a typed grid localization",
           "[lab][ct6][negative]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString input = generateFoundryScene( dir.path() );
    const QString good = runLab1NdviStep(
      input, QDir( dir.path() ).filePath( QStringLiteral( "lab1_ndvi.tif" ) ) );

    // Sanity: the untouched product passes (control group).
    OutputVerifier verifier;
    const auto control = verifier.gradeArtifact( QStringLiteral( "ndvi_landsat" ), good, {} );
    REQUIRE( control.graded );
    REQUIRE( control.verdict == QStringLiteral( "pass" ) );

    // The same pixels re-stamped EPSG:4326 — a CRS mistake a student can
    // actually make — must FAIL and localize the mismatch to both codes.
    const QString wrongCrs = cloneWithCrs(
      good, QDir( dir.path() ).filePath( QStringLiteral( "lab1_ndvi_wrong_crs.tif" ) ), 4326 );
    const auto grade = verifier.gradeArtifact( QStringLiteral( "ndvi_landsat" ), wrongCrs, {} );
    REQUIRE( grade.graded );
    REQUIRE( grade.verdict == QStringLiteral( "fail" ) );
    REQUIRE( grade.score < grade.passingScore );
    REQUIRE( grade.cappedByBlocking );

    const OutputVerifier::LabDeduction *grid = findDeduction( grade, "grid.compatible" );
    REQUIRE( grid != nullptr );
    REQUIRE( grid->severity == QLatin1String( "blocking" ) );
    INFO( "grid message: " << grid->message.toStdString() );
    REQUIRE( grid->message == QStringLiteral( "CRS EPSG:4326 does not match expected EPSG:32648" ) );
    // Structured localization: observed vs expected, both machine-checkable.
    REQUIRE( grid->observed["epsg"].asString() == "EPSG:4326" );
    REQUIRE( grid->expected["crs_authid"].asString() == "EPSG:32648" );
    REQUIRE( grid->expected["width"].asInt() == 256 );
}

TEST_CASE( "ct6: dual authority guard - fixture ndvi_basics stays exact, foundry product stays below its line",
           "[lab][ct6][authority]" )
{
    // The committed reference raster is the closed-form scene the fixture
    // authority (ndvi_basics.rules.json) derives from: still exactly 100.
    const QString reference = QStringLiteral( CMAKE_SOURCE_DIR )
                                + QStringLiteral( "/tests/fixtures/lab/ndvi_basics_reference.tif" );
    REQUIRE( QFile::exists( reference ) );
    OutputVerifier verifier;
    const auto fixture = verifier.gradeArtifact( QStringLiteral( "ndvi_basics" ), reference, {} );
    REQUIRE( fixture.graded );
    REQUIRE( fixture.verdict == QStringLiteral( "pass" ) );
    REQUIRE( fixture.score == 100.0 );
    REQUIRE( fixture.deductions.empty() );

    // ...and the foundry product is a DIFFERENT known answer: under the
    // fixture authority it must stay below the pass line (the CT-6 measured
    // 45/100). If either file drifts toward the other scene, one of the two
    // oracles in this file goes red.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString input = generateFoundryScene( dir.path() );
    const QString artifact = runLab1NdviStep(
      input, QDir( dir.path() ).filePath( QStringLiteral( "lab1_ndvi.tif" ) ) );
    const auto cross = verifier.gradeArtifact( QStringLiteral( "ndvi_basics" ), artifact, {} );
    REQUIRE( cross.graded );
    REQUIRE( cross.verdict == QStringLiteral( "fail" ) );
    REQUIRE( cross.score < cross.passingScore );
}
