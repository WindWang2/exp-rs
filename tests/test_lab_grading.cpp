// test_lab_grading.cpp — D4 lab auto-grader acceptance tests.
//
// One seam under test: OutputVerifier::gradeArtifact (aka gradeForTeaching).
// Everything the teacher (and the CLI) sees flows through it, so every gate
// from the mission is asserted at that seam:
//
//   corpus gates      | every wrong-answer fixture lands strictly below its
//                     | declared score band (错就是错); every reference scores
//                     | exactly its declared score (100).
//   determinism gate  | grading the same artifact twice yields byte-identical
//                     | report bodies and identical digests.
//   evidence gate     | every deduction carries observed+expected; every rule
//                     | assertion has an evidence record.
//   memory gate       | a 2048x2048 artifact grades through windowed reads
//                     | inside a 1 MiB byte budget (#808 contract) and stays
//                     | numerically exact.
//   binary-path gate  | the pre-existing OutputVerifier::verify behaviour is
//                     | untouched (regression covered by ctest test_harness_evals).
//
// Numeric expectations are closed forms (see tests/fixtures/lab/README.md and
// the derivation notes in data/labs/grading/*.rules.json) — never "JSON
// non-null" weak asserts (issue #814 precedent avoided).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "agent/output_verifier.h"
#include "geospatial/gdal_guard.h"

#include <gdal.h>
#include <gdal_priv.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace
{

using sicnu::agent::OutputVerifier;

const char *fixturesDir() { return CMAKE_SOURCE_DIR "/tests/fixtures/lab"; }

const char *rulesDir() { return CMAKE_SOURCE_DIR "/data/labs/grading"; }

std::string fixturePath( const char *name )
{
    return std::string( fixturesDir() ) + "/" + name;
}

OutputVerifier::LabGradeResult grade( const std::string &lab, const std::string &artifact,
                                      std::size_t maxBytes = 64ull * 1024ull * 1024ull )
{
    sicnu::geo::ensureGdalRegistered();
    OutputVerifier::LabGradeOptions options;
    options.maxBytes = maxBytes;
    const OutputVerifier verifier;
    return verifier.gradeArtifact( QString::fromLatin1( lab.c_str() ),
                                   QString::fromLatin1( artifact.c_str() ), options );
}

OutputVerifier::LabGradeResult gradeFile( const std::string &rulesFile,
                                          const std::string &artifact,
                                          std::size_t maxBytes = 64ull * 1024ull * 1024ull )
{
    sicnu::geo::ensureGdalRegistered();
    OutputVerifier::LabGradeOptions options;
    options.maxBytes = maxBytes;
    const OutputVerifier verifier;
    return verifier.gradeArtifact( QString::fromLatin1( rulesFile.c_str() ),
                                   QString::fromLatin1( artifact.c_str() ), options );
}

Json::Value readJson( const std::string &path )
{
    std::ifstream stream( path );
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    CHECK( Json::parseFromStream( builder, stream, &value, &errors ) );
    return value;
}

bool hasDeduction( const OutputVerifier::LabGradeResult &result, const std::string &assertionId )
{
    for ( const auto &d : result.deductions )
    {
        if ( d.assertionId.toStdString() == assertionId )
            return true;
    }
    return false;
}

/// Writes a rules JSON document to <dir>/<labId>.rules.json and returns the path.
std::string writeRules( const QDir &dir, const std::string &labId, const Json::Value &rules )
{
    const std::string path = ( dir.filePath( QString::fromStdString( labId + ".rules.json" ) ) ).toStdString();
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::ofstream out( path );
    out << Json::writeString( builder, rules );
    return path;
}

/// Generates a WxH Float32 GeoTIFF with value = fill for every pixel.
std::string writeConstantTif( const QDir &dir, const std::string &name, int width, int height,
                              float fill, double scale = 0.0 )
{
    GDALAllRegister();
    const std::string path = dir.filePath( QString::fromStdString( name ) ).toStdString();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDatasetH ds = GDALCreate( driver, path.c_str(), width, height, 1, GDT_Float32, nullptr );
    REQUIRE( ds );
    double gt[6] = { 100.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
    GDALSetGeoTransform( ds, gt );
    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    OSRImportFromEPSG( srs, 4326 );
    char *wkt = nullptr;
    OSRExportToWkt( srs, &wkt );
    GDALSetProjection( ds, wkt );
    CPLFree( wkt );
    OSRDestroySpatialReference( srs );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    if ( scale != 0.0 )
        GDALSetRasterScale( band, scale );
    std::vector<float> line( static_cast<size_t>( width ), fill );
    for ( int y = 0; y < height; ++y )
        GDALRasterIO( band, GF_Write, 0, y, width, 1, line.data(), width, 1, GDT_Float32, 0, 0 );
    GDALClose( ds );
    return path;
}

} // namespace

TEST_CASE( "lab_grading.reference corpus scores exactly its declared score", "[lab_grading][corpus]" )
{
    const Json::Value corpus = readJson( std::string( fixturesDir() ) + "/reference_corpus.json" );
    // 6 classic grading labs + 4 labspec labs (teaching-lab-platform-11).
    REQUIRE( corpus["references"].size() == 10 );
    for ( const auto &entry : corpus["references"] )
    {
        const auto result = grade( entry["lab_id"].asString(), fixturePath( entry["fixture"].asString().c_str() ) );
        INFO( entry["fixture"].asString() << ": score=" << result.score
              << " verdict=" << result.verdict.toStdString() );
        REQUIRE( result.graded );
        CHECK( result.verdict == "pass" );
        CHECK( result.score == entry["expected_score"].asDouble() );
        CHECK( result.cappedByBlocking == false );
        CHECK( result.deductions.empty() );
    }
}

TEST_CASE( "lab_grading.wrong-answer corpus is discriminated below its declared bands", "[lab_grading][corpus]" )
{
    const Json::Value corpus = readJson( std::string( fixturesDir() ) + "/wrong_answer_corpus.json" );
    REQUIRE( corpus["wrong_answers"].size() >= 19 );
    for ( const auto &entry : corpus["wrong_answers"] )
    {
        const auto result = grade( entry["lab_id"].asString(), fixturePath( entry["fixture"].asString().c_str() ) );
        const double lo = entry["expected_score_band"][0].asDouble();
        const double hi = entry["expected_score_band"][1].asDouble();
        INFO( entry["fixture"].asString() << ": score=" << result.score
              << " band=[" << lo << "," << hi << "]" );
        REQUIRE( result.graded );
        CHECK( result.verdict == "fail" );
        CHECK( result.score >= lo );
        CHECK( result.score <= hi );
        // the declared failure reasons must all have fired (evidence-first)
        for ( const auto &assertionId : entry["expected_failures"] )
        {
            INFO( "expected deduction: " << assertionId.asString() );
            CHECK( hasDeduction( result, assertionId.asString() ) );
        }
        // blocking-capped results must never reach the pass line
        if ( result.cappedByBlocking )
            CHECK( result.score < result.passingScore );
    }
}

TEST_CASE( "lab_grading.grading is deterministic: identical digest and body", "[lab_grading][determinism]" )
{
    const auto artifact = fixturePath( "planck_temperature_reference.tif" );
    const auto first = grade( "planck_temperature", artifact );
    const auto second = grade( "planck_temperature", artifact );
    REQUIRE( first.graded );
    CHECK( first.digest == second.digest );
    CHECK( Json::writeString( Json::StreamWriterBuilder(), first.toBodyJson() )
           == Json::writeString( Json::StreamWriterBuilder(), second.toBodyJson() ) );

    // the emitted document puts the timestamp in a header that is NOT part
    // of the body/digest
    const Json::Value doc = first.toJson( QStringLiteral( "2030-01-01T00:00:00.000Z" ) );
    CHECK( doc["schema"] == "sicnu.lab.grade/1" );
    CHECK( doc["digest"].asString() == first.digest.toStdString() );
    CHECK( doc["generated_utc"].asString() == "2030-01-01T00:00:00.000Z" );
    CHECK( doc["report"] == first.toBodyJson() );
}

TEST_CASE( "lab_grading.every deduction carries evidence and every assertion has a record", "[lab_grading][evidence]" )
{
    const Json::Value corpus = readJson( std::string( fixturesDir() ) + "/wrong_answer_corpus.json" );
    for ( const auto &entry : corpus["wrong_answers"] )
    {
        const auto result = grade( entry["lab_id"].asString(), fixturePath( entry["fixture"].asString().c_str() ) );
        REQUIRE( result.graded );
        // an evidence-less deduction is a P0 defect
        for ( const auto &d : result.deductions )
        {
            INFO( d.assertionId.toStdString() );
            CHECK( !d.observed.isNull() );
            CHECK( !d.expected.isNull() );
            CHECK( !d.message.isEmpty() );
        }
        // evidence records exist for every rules assertion (passed or failed)
        const Json::Value rules =
          readJson( std::string( rulesDir() ) + "/" + entry["lab_id"].asString() + ".rules.json" );
        CHECK( result.evidence.size() == rules["assertions"].size() );
    }
}

TEST_CASE( "lab_grading.2048x2048 artifact grades inside a 1 MiB windowed budget", "[lab_grading][memory]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string bigRaster = writeConstantTif( QDir( dir.path() ), "big.tif", 2048, 2048, 1.0f );

    // rules: mean 1.0 (exact), sigma 0 — the whole raster at 1 MiB budget
    // must still be graded exactly through windowed reads.
    Json::Value rules;
    rules["schema_version"] = "sicnu.lab.rules/1";
    rules["lab_id"] = "lab_budget";
    rules["title"] = "budget probe";
    rules["artifact"]["kind"] = "raster";
    rules["passing_score"] = 60;
    Json::Value crs;
    crs["id"] = "grid";
    crs["kind"] = "crs_grid";
    crs["weight"] = 40;
    crs["severity"] = "blocking";
    crs["params"]["epsg"] = 4326;
    crs["params"]["width"] = 2048;
    crs["params"]["height"] = 2048;
    crs["params"]["pixel_size_x"] = 0.001;
    crs["params"]["pixel_size_y"] = 0.001;
    Json::Value stats;
    stats["id"] = "stats";
    stats["kind"] = "mean_sigma";
    stats["weight"] = 60;
    stats["params"]["band"] = 1;
    stats["params"]["mean"] = 1.0;
    stats["params"]["mean_tolerance"] = 1e-6;
    stats["params"]["sigma_max"] = 1e-6;
    stats["params"]["sigma_tolerance"] = 0.0;
    rules["assertions"].append( crs );
    rules["assertions"].append( stats );
    const auto rulesPath = writeRules( QDir( dir.path() ), "lab_budget", rules );

    const auto result = gradeFile( rulesPath, bigRaster, 1024ull * 1024ull );
    REQUIRE( result.graded );
    INFO( "score=" << result.score );
    CHECK( result.verdict == "pass" );
    CHECK( result.score == 100.0 );
    CHECK( result.summary["tile_height"].asInt() < 2048 );  // genuinely windowed (row tiles)
    CHECK( result.summary["tiles"].asInt64() > 1 );
}

TEST_CASE( "lab_grading.byte budget below a single pixel is a typed unverifiable error", "[lab_grading][memory]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string smallRaster = writeConstantTif( QDir( dir.path() ), "small.tif", 8, 8, 1.0f );
    const auto result = grade( "ndvi_basics", smallRaster, 4 );
    CHECK( !result.graded );
    CHECK( result.verdict == "unverifiable" );
    CHECK( result.errorClass == "artifact" );
    CHECK( !result.error.isEmpty() );
}

TEST_CASE( "lab_grading.rules validation failures are usage errors", "[lab_grading][rules]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    SECTION( "weights not summing to 100" )
    {
        Json::Value rules;
        rules["schema_version"] = "sicnu.lab.rules/1";
        rules["lab_id"] = "bad_weights";
        rules["title"] = "bad";
        rules["artifact"]["kind"] = "raster";
        Json::Value a;
        a["id"] = "x";
        a["kind"] = "range";
        a["weight"] = 50;
        a["params"]["band"] = 1;
        a["params"]["min"] = -1.0;
        a["params"]["max"] = 1.0;
        rules["assertions"].append( a );
        const auto path = writeRules( QDir( dir.path() ), "bad_weights", rules );
        const auto result = gradeFile( path, fixturePath( "ndvi_basics_reference.tif" ) );
        CHECK( !result.graded );
        CHECK( result.errorClass == "usage" );
    }

    SECTION( "unknown kernel kind" )
    {
        Json::Value rules;
        rules["schema_version"] = "sicnu.lab.rules/1";
        rules["lab_id"] = "bad_kind";
        rules["title"] = "bad";
        rules["artifact"]["kind"] = "raster";
        Json::Value a;
        a["id"] = "x";
        a["kind"] = "vibes";
        a["weight"] = 100;
        a["params"] = Json::objectValue;
        rules["assertions"].append( a );
        const auto path = writeRules( QDir( dir.path() ), "bad_kind", rules );
        const auto result = gradeFile( path, fixturePath( "ndvi_basics_reference.tif" ) );
        CHECK( !result.graded );
        CHECK( result.errorClass == "usage" );
    }
}

TEST_CASE( "lab_grading.unknown lab and missing artifact map to the usage class", "[lab_grading][cli_contract]" )
{
    auto result = grade( "no_such_lab", fixturePath( "ndvi_basics_reference.tif" ) );
    CHECK( !result.graded );
    CHECK( result.verdict == "unverifiable" );
    CHECK( result.errorClass == "usage" );

    result = grade( "ndvi_basics", fixturePath( "does_not_exist.tif" ) );
    CHECK( !result.graded );
    CHECK( result.errorClass == "usage" );
}

TEST_CASE( "lab_grading.corrupt artifact maps to the unverifiable class", "[lab_grading][cli_contract]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = dir.filePath( "corrupt.tif" );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "this is not a raster\n" );
    file.close();

    const auto result = grade( "ndvi_basics", path.toStdString() );
    CHECK( !result.graded );
    CHECK( result.verdict == "unverifiable" );
    CHECK( result.errorClass == "artifact" );
}

TEST_CASE( "lab_grading.monotone histogram shapes grade a ramp correctly", "[lab_grading][kernels]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    GDALAllRegister();
    const std::string ramp = [&dir]()
    {
        const std::string path = dir.filePath( "ramp.tif" ).toStdString();
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        GDALDatasetH ds = GDALCreate( driver, path.c_str(), 32, 32, 1, GDT_Float32, nullptr );
        double gt[6] = { 100.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
        GDALSetGeoTransform( ds, gt );
        OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
        OSRImportFromEPSG( srs, 4326 );
        char *wkt = nullptr;
        OSRExportToWkt( srs, &wkt );
        GDALSetProjection( ds, wkt );
        CPLFree( wkt );
        OSRDestroySpatialReference( srs );
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        std::vector<float> line( 32 );
        for ( int y = 0; y < 32; ++y )
        {
            for ( int x = 0; x < 32; ++x )
                line[static_cast<size_t>( x )] = static_cast<float>( x + y ); // triangular -> flat hist
            GDALRasterIO( band, GF_Write, 0, y, 32, 1, line.data(), 32, 1, GDT_Float32, 0, 0 );
        }
        GDALClose( ds );
        return path;
    }();

    // uniform-count ramp histogram: with 2 bins the counts are 496 vs 528 —
    // non-decreasing (monotone_increasing passes) and only one dominant mode
    // (bimodal fails).
    auto rampRules = [&]( const char *shape, const char *labId )
    {
        Json::Value rules;
        rules["schema_version"] = "sicnu.lab.rules/1";
        rules["lab_id"] = labId;
        rules["title"] = labId;
        rules["artifact"]["kind"] = "raster";
        Json::Value crs;
        crs["id"] = "grid";
        crs["kind"] = "crs_grid";
        crs["weight"] = 50;
        crs["severity"] = "blocking";
        crs["params"]["epsg"] = 4326;
        crs["params"]["width"] = 32;
        crs["params"]["height"] = 32;
        crs["params"]["pixel_size_x"] = 0.001;
        crs["params"]["pixel_size_y"] = 0.001;
        Json::Value hist;
        hist["id"] = "hist";
        hist["kind"] = "histogram_shape";
        hist["weight"] = 50;
        hist["params"]["band"] = 1;
        hist["params"]["bins"] = 2;
        hist["params"]["min"] = 0.0;
        hist["params"]["max"] = 62.0;
        hist["params"]["shape"] = shape;
        rules["assertions"].append( crs );
        rules["assertions"].append( hist );
        return writeRules( QDir( dir.path() ), labId, rules );
    };

    const auto increasing = gradeFile( rampRules( "monotone_increasing", "ramp_up" ), ramp );
    REQUIRE( increasing.graded );
    CHECK( increasing.verdict == "pass" );
    CHECK( increasing.score == 100.0 );

    const auto bimodal = gradeFile( rampRules( "bimodal", "ramp_bi" ), ramp );
    REQUIRE( bimodal.graded );
    CHECK( bimodal.verdict == "fail" );
    CHECK( bimodal.score == 50.0 );
    CHECK( hasDeduction( bimodal, "hist" ) );
}

TEST_CASE( "lab_grading.classification kernels accept inline truth grids", "[lab_grading][kernels]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 4x4 truth: 8 px class 1, 8 px class 2; artifact: one class-2 pixel
    // predicted as class 1 — confusion [[8,0],[1,7]], OA = 15/16 = 0.9375,
    // kappa = 0.875 exactly.
    sicnu::geo::ensureGdalRegistered();
    const std::string classified = [&dir]()
    {
        const std::string path = dir.filePath( "classes.tif" ).toStdString();
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        REQUIRE( driver );
        GDALDatasetH ds = GDALCreate( driver, path.c_str(), 4, 4, 1, GDT_Byte, nullptr );
        REQUIRE( ds );
        double gt[6] = { 100.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
        GDALSetGeoTransform( ds, gt );
        OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
        OSRImportFromEPSG( srs, 4326 );
        char *wkt = nullptr;
        OSRExportToWkt( srs, &wkt );
        GDALSetProjection( ds, wkt );
        CPLFree( wkt );
        OSRDestroySpatialReference( srs );
        GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
        std::vector<unsigned char> line( 4 );
        for ( int y = 0; y < 4; ++y )
        {
            for ( int x = 0; x < 4; ++x )
            {
                const bool truthOne = ( y * 4 + x ) < 8;
                const bool predOne = truthOne || ( ( y * 4 + x ) == 8 ); // one class-2 -> class-1 slip
                line[static_cast<size_t>( x )] = static_cast<unsigned char>( predOne ? 1 : 2 );
            }
            GDALRasterIO( band, GF_Write, 0, y, 4, 1, line.data(), 4, 1, GDT_Byte, 0, 0 );
        }
        GDALClose( ds );
        return path;
    }();

    Json::Value rules;
    rules["schema_version"] = "sicnu.lab.rules/1";
    rules["lab_id"] = "inline_truth";
    rules["title"] = "inline truth";
    rules["artifact"]["kind"] = "raster";
    rules["passing_score"] = 60;
    Json::Value kappa;
    kappa["id"] = "acc";
    kappa["kind"] = "classification_kappa";
    kappa["weight"] = 100;
    kappa["params"]["band"] = 1;
    kappa["params"]["labels"] = Json::Value( Json::arrayValue );
    kappa["params"]["labels"].append( 1 );
    kappa["params"]["labels"].append( 2 );
    kappa["params"]["truth"]["inline"] = Json::Value( Json::arrayValue );
    for ( int row = 0; row < 4; ++row )
    {
        Json::Value line( Json::arrayValue );
        for ( int col = 0; col < 4; ++col )
            line.append( ( row * 4 + col ) < 8 ? 1 : 2 );
        kappa["params"]["truth"]["inline"].append( line );
    }
    kappa["params"]["kappa_min"] = 0.5;
    kappa["params"]["oa_min"] = 0.8;
    rules["assertions"].append( kappa );
    const auto rulesPath = writeRules( QDir( dir.path() ), "inline_truth", rules );

    const auto result = gradeFile( rulesPath, classified );
    INFO( "score=" << result.score << " error=" << result.error.toStdString()
          << " errorClass=" << result.errorClass.toStdString() );
    REQUIRE( result.graded );
    CHECK( result.verdict == "pass" );
    CHECK( result.score == 100.0 );

    // tighten the kappa floor above the observed value -> graded fail
    rules["lab_id"] = "inline_truth_strict";   // file stem must match lab_id
    rules["assertions"][0]["params"]["kappa_min"] = 0.99;
    const auto strictPath = writeRules( QDir( dir.path() ), "inline_truth_strict", rules );
    const auto strict = gradeFile( strictPath, classified );
    REQUIRE( strict.graded );
    CHECK( strict.verdict == "fail" );
    CHECK( strict.score == 0.0 );
    CHECK( hasDeduction( strict, "acc" ) );
}

TEST_CASE( "lab_grading.existing binary verify path still works alongside teaching mode", "[lab_grading][binary_path]" )
{
    sicnu::geo::ensureGdalRegistered();
    const OutputVerifier verifier;
    const auto verification =
      verifier.verify( QString::fromLatin1( fixturePath( "ndvi_basics_reference.tif" ) ) );
    CHECK( verification.ok );
    CHECK( verification.kind == "raster" );
    CHECK( verification.issues.isEmpty() );
}

// ---------------------------------------------------------------------------
// lab platform 12.0 — adversarial rules corpus (Oracle O3). Every rule set
// below is malformed or hostile; the grader must reject it as a TYPED usage
// error before executing any assertion. A crash, hang or OOB here aborts the
// whole test binary, so a green run is the no-crash proof.
// ---------------------------------------------------------------------------

namespace
{

Json::Value minimalRules( const char *labId )
{
    Json::Value rules;
    rules["schema_version"] = "sicnu.lab.rules/1";
    rules["lab_id"] = labId;
    rules["title"] = "adversarial probe";
    rules["artifact"]["kind"] = "raster";
    return rules;
}

Json::Value rangeAssertion( const char *id )
{
    Json::Value a;
    a["id"] = id;
    a["kind"] = "range";
    a["weight"] = 100;
    a["params"]["band"] = 1;
    a["params"]["min"] = -1.0;
    a["params"]["max"] = 1.0;
    return a;
}

} // namespace

TEST_CASE( "lab_grading.adversarial rules corpus yields typed usage errors, never crashes",
           "[lab_grading][rules][adversarial]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const std::string artifact = fixturePath( "ndvi_basics_reference.tif" );

    struct Case
    {
        const char *label;
        std::function<std::string( const QDir &, const std::string & )> write;
        std::string fragment;
    };

    const auto writeWithAssertion = [ &dir ]( const std::string &stem, Json::Value assertion ) {
        Json::Value rules = minimalRules( stem.c_str() );
        rules["assertions"].append( std::move( assertion ) );
        return writeRules( QDir( dir.path() ), stem, rules );
    };

    const std::vector<Case> cases = {
        { "params is an array (type confusion)",
          [ & ]( const QDir &d, const std::string &stem ) {
              Json::Value a = rangeAssertion( "x" );
              a["params"] = Json::Value( Json::arrayValue );
              return writeWithAssertion( stem, std::move( a ) );
          },
          "params" },
        { "negative weight",
          [ & ]( const QDir &d, const std::string &stem ) {
              Json::Value a = rangeAssertion( "x" );
              a["weight"] = -5;
              return writeWithAssertion( stem, std::move( a ) );
          },
          "weight" },
        { "weight over 100",
          [ & ]( const QDir &d, const std::string &stem ) {
              Json::Value a = rangeAssertion( "x" );
              a["weight"] = 1e9;
              return writeWithAssertion( stem, std::move( a ) );
          },
          "weight" },
        { "duplicate assertion ids",
          [ & ]( const QDir &d, const std::string &stem ) {
              Json::Value rules = minimalRules( stem.c_str() );
              Json::Value a = rangeAssertion( "x" );
              a["weight"] = 50;
              rules["assertions"].append( a );
              rules["assertions"].append( a );
              return writeRules( QDir( dir.path() ), stem, rules );
          },
          "duplicate" },
        { "passing_score out of range",
          [ & ]( const QDir &d, const std::string &stem ) {
              Json::Value rules = minimalRules( stem.c_str() );
              rules["passing_score"] = 1000;
              rules["assertions"].append( rangeAssertion( "x" ) );
              return writeRules( QDir( dir.path() ), stem, rules );
          },
          "passing_score" },
        { "empty assertions array",
          [ & ]( const QDir &d, const std::string &stem ) {
              return writeRules( QDir( dir.path() ), stem, minimalRules( stem.c_str() ) );
          },
          "assertions" },
        { "provenance seed overflows uint32",
          [ & ]( const QDir &d, const std::string &stem ) {
              Json::Value a = rangeAssertion( "x" );
              a["kind"] = "provenance";
              a["params"] = Json::objectValue;
              a["params"]["seed"] = Json::Value( static_cast<Json::Int64>( 4294967296ll ) );
              return writeWithAssertion( stem, std::move( a ) );
          },
          "uint32" },
        { "provenance unknown metadata key",
          [ & ]( const QDir &d, const std::string &stem ) {
              Json::Value a = rangeAssertion( "x" );
              a["kind"] = "provenance";
              a["params"] = Json::objectValue;
              a["params"]["evil_key"] = "x";
              return writeWithAssertion( stem, std::move( a ) );
          },
          "unknown key" },
        { "provenance with no expectation at all",
          [ & ]( const QDir &d, const std::string &stem ) {
              Json::Value a = rangeAssertion( "x" );
              a["kind"] = "provenance";
              a["params"] = Json::objectValue;
              return writeWithAssertion( stem, std::move( a ) );
          },
          "requires at least one" },
        { "histogram_shape with negative bins",
          [ & ]( const QDir &d, const std::string &stem ) {
              Json::Value a = rangeAssertion( "x" );
              a["kind"] = "histogram_shape";
              a["params"] = Json::objectValue;
              a["params"]["band"] = 1;
              a["params"]["bins"] = -3;
              a["params"]["min"] = 0.0;
              a["params"]["max"] = 1.0;
              a["params"]["shape"] = "bimodal";
              return writeWithAssertion( stem, std::move( a ) );
          },
          "bins" },
    };

    int caseIndex = 0;
    for ( const auto &item : cases )
    {
        INFO( item.label );
        const std::string stem = "adv_" + std::to_string( caseIndex++ );
        const auto path = item.write( QDir( dir.path() ), stem );
        const auto result = gradeFile( path, artifact );
        CHECK( !result.graded );
        CHECK( result.errorClass == "usage" );
        if ( !item.fragment.empty() )
        {
            INFO( "error was: " << result.error.toStdString() );
            CHECK( result.error.toStdString().find( item.fragment ) != std::string::npos );
        }
    }

    SECTION( "lab_id does not match the file stem" )
    {
        Json::Value rules = minimalRules( "adv_stem" );
        rules["lab_id"] = "some_other_lab";
        rules["assertions"].append( rangeAssertion( "x" ) );
        const auto path = writeRules( QDir( dir.path() ), "adv_stem", rules );
        const auto result = gradeFile( path, artifact );
        CHECK( !result.graded );
        CHECK( result.errorClass == "usage" );
    }
}

TEST_CASE( "lab_grading.absurd band indices grade as failures without OOB",
           "[lab_grading][rules][adversarial]" )
{
    // Documented grader policy: out-of-range bands are GRADED failures, not
    // usage errors (output_verifier.cpp ContentWalk). The adversarial point
    // of INT_MAX is that resolution must clip cleanly instead of reading OOB.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // ndvi_basics_reference.tif is single-band: 0, 2 and INT_MAX are all
    // out of range.
    for ( const int band : { 0, 2, 2147483647 } )
    {
        Json::Value a = rangeAssertion( "x" );
        a["params"]["band"] = band;
        Json::Value rules = minimalRules( "adv_band" );
        rules["assertions"].append( a );
        const auto path = writeRules( QDir( dir.path() ), "adv_band", rules );
        const auto result = gradeFile(
            path, fixturePath( "ndvi_basics_reference.tif" ) );
        INFO( "band " << band << ": " << result.error.toStdString() );
        REQUIRE( result.graded );
        REQUIRE( result.verdict == QLatin1String( "fail" ) );
        REQUIRE( hasDeduction( result, "x" ) );
    }
}

// ---------------------------------------------------------------------------
// CLI / in-process transcript-contract parity (R3 teaching admin track).
//
// The CLI process shell and an in-process caller must produce the SAME grade
// from the SAME artifact: the engine's LabGradeResult is serialized to its
// canonical sicnu.lab.grade/1 transcript (exactly what `sicnu_geo_rs_cli lab
// --grade` writes to --out), handed to the teaching admin's ONE
// gradeFromTranscript mapper (the same function gradeViaCli uses for real
// subprocess transcripts), and the mapped verdict/status/score/digest/
// top-deduction must agree with the engine result. A drift here means the
// teacher console and the process-isolated CLI disagree about a student.
// ---------------------------------------------------------------------------

namespace
{

int exitCodeFor( const OutputVerifier::LabGradeResult &result )
{
    if ( result.graded )
        return result.verdict == QLatin1String( "pass" ) ? 0 : 1;
    return result.errorClass == QLatin1String( "usage" ) ? 2 : 3;
}

QString topDeductionOf( const OutputVerifier::LabGradeResult &result )
{
    QString top;
    double topWeight = -1.0;
    for ( const auto &d : result.deductions )
    {
        if ( top.isEmpty() || d.weight > topWeight )
        {
            top = d.assertionId;
            topWeight = d.weight;
        }
    }
    return top;
}

} // namespace

TEST_CASE( "lab_grading.in-process result and CLI transcript mapper agree on every artifact class",
           "[lab_grading][parity]" )
{
    using sicnu::teaching_admin::gradeFromTranscript;

    struct Case
    {
        const char *rules;
        const char *artifact;
    };
    // pass (reference corpus), fail (wrong-answer corpus), unverifiable
    // (corrupt bytes) — the three exit-contract classes.
    const std::vector<Case> cases = {
        { "change_detect.rules.json", "change_detect_reference.tif" },
        { "change_detect.rules.json", "change_detect_wrong_inverted.tif" },
        { "change_detect.rules.json", "change_detect_wrong_all_nodata.tif" },
    };
    for ( const Case &c : cases )
    {
        const auto result = gradeFile( std::string( rulesDir() ) + "/" + c.rules,
                                       fixturePath( c.artifact ) );
        INFO( c.artifact << " -> " << result.verdict.toStdString() );

        // The transcript the real CLI would have written for this grade.
        const Json::Value transcript = result.toJson( "parity-test-utc" );
        REQUIRE( transcript["schema"].asString() == "sicnu.lab.grade/1" );
        REQUIRE( transcript["digest"].asString() == result.digest.toStdString() );

        const auto mapped = gradeFromTranscript( exitCodeFor( result ), transcript.object() );

        // verdict/status parity
        if ( result.graded )
        {
            REQUIRE( ( mapped.status == "pass" || mapped.status == "fail" ) );
            REQUIRE( mapped.verdict == result.verdict );
            REQUIRE( mapped.score == Catch::Approx( result.score ).margin( 1e-9 ) );
            REQUIRE( mapped.unavailableReason.isEmpty() );
        }
        else
        {
            REQUIRE( result.errorClass == QLatin1String( "artifact" ) );
            REQUIRE( mapped.status == QLatin1String( "unavailable" ) );
            REQUIRE( mapped.unavailableReason == QLatin1String( "artifact_unverifiable" ) );
            REQUIRE( mapped.score < 0.0 );
        }
        // digest parity: the mapper forwards the transcript digest verbatim
        REQUIRE( mapped.reportDigest == result.digest );
        // top deduction parity: highest-weight failed assertion, same definition
        REQUIRE( mapped.topDeduction == topDeductionOf( result ) );
    }

    // usage class parity: an unknown lab maps to exit 2 / typed error.
    {
        const auto result = grade( "no_such_lab_parity", fixturePath( "change_detect_reference.tif" ) );
        REQUIRE_FALSE( result.graded );
        REQUIRE( result.errorClass == QLatin1String( "usage" ) );
        const auto mapped = gradeFromTranscript( 2, result.toJson( "parity-test-utc" ).object() );
        REQUIRE( mapped.status == QLatin1String( "error" ) );
        REQUIRE( mapped.score < 0.0 );
        REQUIRE_FALSE( mapped.message.isEmpty() );
    }

    // broken-authority guard parity: exit 0 with a fail transcript is refused
    // by the shared mapper (never trusted into a pass).
    {
        const auto result = gradeFile( std::string( rulesDir() ) + "/change_detect.rules.json",
                                       fixturePath( "change_detect_wrong_inverted.tif" ) );
        REQUIRE( result.graded );
        REQUIRE( result.verdict == QLatin1String( "fail" ) );
        const auto mapped = gradeFromTranscript( 0, result.toJson( "parity-test-utc" ).object() );
        REQUIRE( mapped.status == QLatin1String( "unavailable" ) );
        REQUIRE( mapped.unavailableReason == QLatin1String( "grader_exit_verdict_mismatch" ) );
        REQUIRE( mapped.score < 0.0 );
    }
}
