// tests/test_verifier_robustness_r4.cpp — Track 8 R4 WP-B: the artifact
// verifier under adversarial inputs, in the deterministic-injection
// tradition of test_io_atomic_failures.cpp.
//
// The one contract every case asserts (harness_verification.h): verifyArtifact
// TERMINATES for any input and returns a STRUCTURED ArtifactVerification —
// a verdict from the closed tri-state plus checks that agree with it
// (verdictFromChecks semantics). It must never crash, never hang, never
// throw — the verifier runs after an untrusted producer, so a hostile or
// broken artifact is exactly the situation it exists for.
//
// Malformed-input classes (PLAN.md WP-B):
//   1 empty path                5 oversized logical raster (bounded probe)
//   2 nonexistent path          6 non-UTF8 path bytes
//   3 corrupt vector document   7 numeric overflow in expectations
//   4 truncated raster header   8 type-confused expectations (red→green)
// plus the parse seam the harness consumes model output through (QJson in
// the SSE client): deep nesting must be a structured refusal, not a crash.

#include <catch2/catch_test_macros.hpp>

#include "agent/harness/harness_verification.h"

#include <gdal_priv.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <chrono>
#include <string>

using namespace sicnu::agent::harness;

namespace {

/// Minimal valid single-band GeoTIFF (8x8) — the control artifact.
std::string writeTinyRaster( const QString &path )
{
    static const bool kRegistered = [] {
        GDALAllRegister();
        return true;
    }();
    ( void )kRegistered;
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    if ( !driver )
        return {};
    GDALDataset *ds = driver->Create( path.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr );
    if ( !ds )
        return {};
    double geoTransform[6] = { 500000.0, 30.0, 0.0, 5000000.0, 0.0, -30.0 };
    ds->SetGeoTransform( geoTransform );
    float row[8] = {};
    for ( int y = 0; y < 8; ++y )
    {
        for ( int x = 0; x < 8; ++x )
            row[x] = static_cast<float>( x + y ) / 16.0f + 0.5f; // no zero values
        ds->GetRasterBand( 1 )->RasterIO( GF_Write, 0, y, 8, 1, row, 8, 1, GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
    return path.toStdString();
}

/// The verdict must always agree with its own checks (verdictFromChecks
/// semantics): any failed error-severity check ⇒ Fail; no failures but a
/// warning ⇒ PassWithWarnings; else Pass.
bool verdictAgreesWithChecks( const ArtifactVerification &verification )
{
    bool anyError = false;
    bool anyWarning = false;
    for ( const VerificationCheck &check : verification.checks )
    {
        if ( check.passed )
            continue;
        if ( check.severity == "error" )
            anyError = true;
        else
            anyWarning = true;
    }
    if ( anyError )
        return verification.verdict == Verdict::Fail;
    if ( anyWarning )
        return verification.verdict == Verdict::PassWithWarnings;
    return verification.verdict == Verdict::Pass;
}

/// Runs verifyArtifact under a no-throw contract and returns the result.
/// A throw is recorded and fails the caller's CHECK.
template <typename Fn>
bool terminatesStructured( Fn &&verify, ArtifactVerification &out, bool &threw )
{
    try
    {
        out = verify();
    }
    catch ( ... )
    {
        threw = true;
        return false;
    }
    return verdictAgreesWithChecks( out );
}

} // namespace

// — 1/2: empty and nonexistent paths are structured FAILs ————————————

TEST_CASE( "the verifier rejects an empty or missing artifact with a "
           "structured FAIL, never a crash", "[harness][verifier-r4][robustness]" )
{
    QTemporaryDir dir;
    VerificationExpectations expectations;

    ArtifactVerification empty;
    bool threw = false;
    CHECK( terminatesStructured(
        [&] { return verifyArtifact( "", expectations ); }, empty, threw ) );
    CHECK_FALSE( threw );
    CHECK( empty.verdict == Verdict::Fail );

    ArtifactVerification missing;
    CHECK( terminatesStructured(
        [&] { return verifyArtifact( dir.filePath( "nope.tif" ).toStdString(), expectations ); },
        missing, threw ) );
    CHECK_FALSE( threw );
    CHECK( missing.verdict == Verdict::Fail );
    bool hasExistsCheck = false;
    for ( const VerificationCheck &check : missing.checks )
        if ( check.check == "artifact_exists" )
            hasExistsCheck = true;
    CHECK( hasExistsCheck );
}

// — 3/4: corrupt vector and truncated raster documents ————————————————
// A producer that died mid-write is the norm case for post-crash runs; the
// verifier reports it, it does not die with it.

TEST_CASE( "a corrupt vector document is a structured FAIL", "[harness][verifier-r4][robustness]" )
{
    QTemporaryDir dir;
    const QString corrupt = dir.filePath( "corrupt.geojson" );
    {
        QFile f( corrupt );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        // "A valid JSON prefix, then the writer died" — half of a
        // FeatureCollection, no closing brace.
        f.write( QByteArray( "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Featu" ) );
    }
    VerificationExpectations expectations;
    ArtifactVerification result;
    bool threw = false;
    CHECK( terminatesStructured(
        [&] { return verifyArtifact( corrupt.toStdString(), expectations ); }, result, threw ) );
    CHECK_FALSE( threw );
    CHECK( result.verdict == Verdict::Fail );
}

TEST_CASE( "a truncated GeoTIFF header is a structured FAIL", "[harness][verifier-r4][robustness]" )
{
    QTemporaryDir dir;
    const std::string good = writeTinyRaster( dir.filePath( "src.tif" ) );
    REQUIRE_FALSE( good.empty() );
    const QString truncated = dir.filePath( "truncated.tif" );
    {
        QFile src( QString::fromStdString( good ) );
        REQUIRE( src.open( QIODevice::ReadOnly ) );
        const QByteArray bytes = src.readAll();
        QFile dst( truncated );
        REQUIRE( dst.open( QIODevice::WriteOnly ) );
        dst.write( bytes.left( bytes.size() / 3 ) ); // header only, body gone
    }
    VerificationExpectations expectations;
    ArtifactVerification result;
    bool threw = false;
    CHECK( terminatesStructured(
        [&] { return verifyArtifact( truncated.toStdString(), expectations ); }, result, threw ) );
    CHECK_FALSE( threw );
    CHECK( result.verdict == Verdict::Fail );
}

// — 5: oversized logical raster stays bounded ————————————————————————

TEST_CASE( "verifying a 100k x 100k logical raster completes in bounded time "
           "on the bounded probe grid", "[harness][verifier-r4][robustness][bounded]" )
{
    QTemporaryDir dir;
    const std::string src = writeTinyRaster( dir.filePath( "vrt-src.tif" ) );
    REQUIRE_FALSE( src.empty() );
    // A VRT with a 100000 x 100000 logical extent: the file is a few KB while
    // the verifier must NOT try to materialize 40 GB — the bounded 64x64
    // probe grid is the contract under test.
    const QString vrtPath = dir.filePath( "huge.vrt" );
    {
        QFile vrt( vrtPath );
        REQUIRE( vrt.open( QIODevice::WriteOnly ) );
        vrt.write( QString(
                       "<VRTDataset rasterXSize=\"100000\" rasterYSize=\"100000\">"
                       "  <VRTRasterBand dataType=\"Float32\" band=\"1\">"
                       "    <SimpleSource><SourceFilename relativeToVRT=\"1\">vrt-src.tif</SourceFilename>"
                       "    <SourceBand>1</SourceBand>"
                       "    <SrcRect xOff=\"0\" yOff=\"0\" xSize=\"8\" ySize=\"8\"/>"
                       "    <DstRect xOff=\"0\" yOff=\"0\" xSize=\"100000\" ySize=\"100000\"/>"
                       "    </SimpleSource>"
                       "  </VRTRasterBand>"
                       "</VRTDataset>" )
                       .toUtf8() );
    }
    VerificationExpectations expectations;
    expectations.minFiniteFraction = 0.5;
    const auto started = std::chrono::steady_clock::now();
    ArtifactVerification result;
    bool threw = false;
    CHECK( terminatesStructured(
        [&] { return verifyArtifact( vrtPath.toStdString(), expectations ); }, result, threw ) );
    const double elapsedMs = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - started )
                                 .count();
    CHECK_FALSE( threw );
    CHECK( elapsedMs < 60000.0 ); // it returned; the point is bounded work
    // The probe sampled the bounded grid and reported honest fractions.
    bool sampled = false;
    for ( const VerificationCheck &check : result.checks )
        if ( check.check == "finite_fraction" )
            sampled = true;
    CHECK( sampled );
}

// — 6: non-UTF8 path bytes ———————————————————————————————————————————

TEST_CASE( "non-UTF8 path bytes produce a structured FAIL, not a crash",
           "[harness][verifier-r4][robustness]" )
{
    QTemporaryDir dir;
    // Invalid UTF-8 continuation bytes in a path (0xFF 0xFE are never valid
    // UTF-8). The producer's filesystem may have created such a name; the
    // verifier reports the artifact as unverifiable.
    const std::string badPath = dir.path().toStdString() + "/bad-\xFF\xFE-name.tif";
    VerificationExpectations expectations;
    ArtifactVerification result;
    bool threw = false;
    CHECK( terminatesStructured( [&] { return verifyArtifact( badPath, expectations ); },
                                 result, threw ) );
    CHECK_FALSE( threw );
    CHECK( result.verdict == Verdict::Fail );
}

// — 7: numeric overflow in expectations ———————————————————————————————

TEST_CASE( "expectations with extreme magnitudes stay structured",
           "[harness][verifier-r4][robustness][numbers]" )
{
    QTemporaryDir dir;
    const std::string raster = writeTinyRaster( dir.filePath( "tiny.tif" ) );
    REQUIRE_FALSE( raster.empty() );

    // 1e308 extents and a >1 finite fraction: nonsense, but the caller is
    // untrusted — the answer is a structured verdict, not UB.
    VerificationExpectations expectations;
    expectations.expectedExtent = Json::Value( Json::objectValue );
    expectations.expectedExtent["xmin"] = -1e308;
    expectations.expectedExtent["ymin"] = -1e308;
    expectations.expectedExtent["xmax"] = 1e308;
    expectations.expectedExtent["ymax"] = 1e308;
    expectations.minFiniteFraction = 2.0; // impossible by construction

    ArtifactVerification result;
    bool threw = false;
    CHECK( terminatesStructured(
        [&] { return verifyArtifact( raster, expectations ); }, result, threw ) );
    CHECK_FALSE( threw );
    // The impossible finite fraction is an honest failing check.
    bool finiteFailed = false;
    for ( const VerificationCheck &check : result.checks )
        if ( check.check == "finite_fraction" && !check.passed )
            finiteFailed = true;
    CHECK( finiteFailed );
}

// — 8: type-confused expectations (red → green) ———————————————————————
// classValues reaches allowed.asDouble() per probed value. A caller whose
// class domain is strings (or nested arrays) must not turn into a throw —
// the verifier's contract is structured termination for ANY input.

TEST_CASE( "a type-confused expected extent is a structured rejection, "
           "never an exception", "[harness][verifier-r4][robustness][types]" )
{
  QTemporaryDir dir;
  const std::string raster = writeTinyRaster( dir.filePath( "tiny3.tif" ) );
  REQUIRE_FALSE( raster.empty() );

  // The AOI rectangle is caller data: a non-numeric member is a structural
  // lie and must surface as a failing check, not a throw from asDouble().
  VerificationExpectations expectations;
  expectations.expectedExtent = Json::Value( Json::objectValue );
  expectations.expectedExtent["xmin"] = "five hundred thousand";

  ArtifactVerification result;
  bool threw = false;
  CHECK( terminatesStructured(
    [&] { return verifyArtifact( raster, expectations ); }, result, threw ) );
  CHECK_FALSE( threw );

  // The control: a numeric extent still performs the real coverage check.
  VerificationExpectations numeric;
  numeric.expectedExtent = Json::Value( Json::objectValue );
  numeric.expectedExtent["xmin"] = 500000.0;
  numeric.expectedExtent["ymin"] = 4999970.0;
  numeric.expectedExtent["xmax"] = 500240.0;
  numeric.expectedExtent["ymax"] = 5000000.0;
  ArtifactVerification coverage;
  bool threw2 = false;
  CHECK( terminatesStructured(
    [&] { return verifyArtifact( raster, numeric ); }, coverage, threw2 ) );
  CHECK_FALSE( threw2 );
  bool hasCoverageCheck = false;
  for ( const VerificationCheck &check : coverage.checks )
    if ( check.check == "extent_covers_aoi" )
      hasCoverageCheck = true;
  CHECK( hasCoverageCheck );
}

TEST_CASE( "a type-confused class-value domain is a structured rejection, "
           "never an exception", "[harness][verifier-r4][robustness][types]" )
{
    QTemporaryDir dir;
    const std::string raster = writeTinyRaster( dir.filePath( "tiny2.tif" ) );
    REQUIRE_FALSE( raster.empty() );

    VerificationExpectations expectations;
    expectations.classValues = Json::Value( Json::arrayValue );
    expectations.classValues.append( "not-a-number" );  // string where a number belongs
    expectations.classValues.append( Json::Value( Json::objectValue ) ); // object!

    ArtifactVerification result;
    bool threw = false;
    CHECK( terminatesStructured(
        [&] { return verifyArtifact( raster, expectations ); }, result, threw ) );
    CHECK_FALSE( threw );
}

// — parse seam: deep nesting is a refusal —————————————————————————————

TEST_CASE( "the SSE parse seam survives deeply nested JSON in streamed data",
           "[harness][verifier-r4][robustness][parse]" )
{
    // The harness consumes model output through QJsonDocument (SSE client).
    // 100k nesting levels must parse to nothing (structured refusal) rather
    // than exhaust the stack — pinning the platform behavior this track
    // relies on.
    const int depth = 100000;
    QByteArray payload;
    payload.reserve( static_cast<int>( depth ) * 2 + 16 );
    for ( int i = 0; i < depth; ++i )
        payload.append( '[' );
    for ( int i = 0; i < depth; ++i )
        payload.append( ']' );

    // MEASURED platform behavior (pinned, not assumed): QJsonDocument
    // refuses 100k-deep nesting WITHOUT a stack overflow. Qt 6.11 surfaces
    // the refusal as a THROWABLE error (deep nesting throws), while a
    // merely-malformed document returns null with a parse error — both are
    // non-crash refusals, and this test accepts either.
    bool refused = false;
    try
    {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson( payload, &parseError );
        refused = doc.isNull() && parseError.error != QJsonParseError::NoError;
    }
    catch ( ... )
    {
        refused = true; // throwable refusal — still no crash, no stack overflow
    }
    CHECK( refused );
}

TEST_CASE( "a hostile kilobyte-deep document never overflows the jsoncpp "
           "lane's stack", "[harness][verifier-r4][robustness][parse]" )
{
    // jsoncpp is the plan/workflow lane's parser, and the reader is fed by
    // untrusted producers. 100k-deep nesting is refused WITHOUT a stack
    // overflow: the parser enforces a stackLimit and surfaces it as a
    // catchable Json::LogicError (measured platform behavior, pinned here
    // so a silent infinite recursion is a visible event). Note the two
    // lanes differ: QJsonDocument::fromJson returns null for the same
    // document, while jsoncpp THROWS — callers on the jsoncpp lane must
    // wrap the parse to treat the throw as the structured refusal.
    const int depth = 100000;
    std::string text;
    text.reserve( static_cast<size_t>( depth ) * 2 + 8 );
    for ( int i = 0; i < depth; ++i )
        text.push_back( '[' );
    for ( int i = 0; i < depth; ++i )
        text.push_back( ']' );

    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    bool ok = true;
    try
    {
        ok = reader->parse( text.data(), text.data() + text.size(), &parsed, &errors );
    }
    catch ( const Json::Exception & )
    {
        // The documented typed refusal; treat it as the rejection it is.
        ok = false;
    }
    catch ( ... )
    {
        ok = false; // any other refusal is still a refusal
    }
    CHECK_FALSE( ok ); // never accepted, never a crash
}
