// tests/test_verify_adapters_grid.cpp
//
// ADR 0172 real provider adapters — the GDAL grid probe over REAL datasets.
// Every fixture below is an actual file written through GDAL (GTiff driver)
// or a hand-written VRT: the probe is judged against bytes on disk, not
// against a fake grid provider.
//
// Contract under test (gdal_grid_probe.h):
//   - size/bandCount/CRS come from the dataset itself;
//   - NoData via the platform's one sentinel authority (bandSentinelMatches),
//     non-finite pixels tracked separately — both as bounded lattice samples
//     (exact when the 3x32 lattice tiles the raster, as in these fixtures);
//   - no implicit resample/reproject: overview-free native windows only;
//   - open failure / unreadable window -> nullopt -> engine Indeterminate;
//   - a CRS-less raster answers crs "" (a real mismatch, not a gap);
//   - no NaN ever enters GridInfo — the report digest stays sealable.
//
// Light target: sicnu_verify_adapters_gdal + Catch2 (GDAL arrives through
// Sicnu::Geospatial); no Qt.

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_srs_api.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "geospatial/gdal_guard.h"
#include "verify/verify_engine.h"
#include "verify/verify_error_codes.h"
#include "verify/verify_sha256.h"
#include "verify/verify_types.h"
#include "verify_adapters/fs_artifact_probe.h"
#include "verify_adapters/gdal_grid_probe.h"

using namespace sicnu::verify;
namespace adapters = sicnu::verify_adapters;

namespace
{

class TempDir
{
  public:
    TempDir()
    {
        std::error_code ec;
        for ( int attempt = 0; attempt < 64 && mPath.empty(); ++attempt )
        {
            const std::filesystem::path candidate =
                std::filesystem::temp_directory_path() /
                ( "verify_grid_" + std::to_string( attempt ) + "_" + std::to_string( rand() ) );
            if ( std::filesystem::create_directories( candidate, ec ); !ec )
                mPath = candidate;
        }
        REQUIRE( !mPath.empty() );
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all( mPath, ec );
    }
    std::string file( const char *name ) const { return ( mPath / name ).generic_string(); }

    static void writeFile( const std::string &path, const std::string &content )
    {
        std::FILE *file = std::fopen( path.c_str(), "wb" );
        REQUIRE( file );
        REQUIRE( std::fwrite( content.data(), 1, content.size(), file ) == content.size() );
        std::fclose( file );
    }
    static std::string readFile( const std::string &path )
    {
        std::FILE *file = std::fopen( path.c_str(), "rb" );
        REQUIRE( file );
        std::string out;
        char buffer[4096];
        std::size_t read = 0;
        while ( ( read = std::fread( buffer, 1, sizeof( buffer ), file ) ) > 0 )
            out.append( buffer, read );
        std::fclose( file );
        return out;
    }

  private:
    std::filesystem::path mPath;
};

constexpr int kEdge = 96; ///< 3 x 32 sampling lattice tiles this exactly
constexpr double kQuarterNodata = 0.25;

std::vector<float> constantPixels( int width, int height, float value )
{
    return std::vector<float>( static_cast<std::size_t>( width ) * height, value );
}

std::vector<float> quarterNodataPixels( int width, int height, float data, float sentinel )
{
    std::vector<float> pixels( static_cast<std::size_t>( width ) * height, data );
    for ( int y = 0; y < height / 2; ++y )
        for ( int x = 0; x < width / 2; ++x )
            pixels[static_cast<std::size_t>( y ) * width + x] = sentinel;
    return pixels;
}

/// Writes a REAL single-band Float32 GTiff; @p crsUserInput may be null for
/// a CRS-less dataset; @p setNoData controls whether a sentinel is declared.
bool writeGtiff( const std::string &path, int width, int height, const char *crsUserInput,
                 bool setNoData, const std::vector<float> &pixels )
{
    sicnu::geo::ensureGdalRegistered();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH dataset =
        GDALCreate( driver, path.c_str(), width, height, 1, GDT_Float32, nullptr );
    if ( !dataset )
        return false;
    double geotransform[6] = { 0.0, 10.0, 0.0, 0.0, 0.0, -10.0 };
    GDALSetGeoTransform( dataset, geotransform );
    if ( crsUserInput )
    {
        OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
        char *wkt = nullptr;
        const bool ok = srs && OSRSetFromUserInput( srs, crsUserInput ) == OGRERR_NONE &&
                        OSRExportToWkt( srs, &wkt ) == OGRERR_NONE && wkt != nullptr &&
                        GDALSetProjection( dataset, wkt ) == CE_None;
        CPLFree( wkt );
        if ( srs )
            OSRDestroySpatialReference( srs );
        if ( !ok )
        {
            GDALClose( dataset );
            return false;
        }
    }
    GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
    if ( setNoData )
        GDALSetRasterNoDataValue( band, 0.0 );
    const CPLErr writeError =
        GDALRasterIO( band, GF_Write, 0, 0, width, height, const_cast<float *>( pixels.data() ),
                      width, height, GDT_Float32, 0, 0 );
    GDALClose( dataset );
    return writeError == CE_None;
}

} // namespace

TEST_CASE( "GdalGridProbe reads real GTiff grid facts", "[verify_adapters][grid][gdal]" )
{
    TempDir dir;
    sicnu::geo::ensureGdalRegistered();
    adapters::GdalGridProbe probe;

    const std::string raster = dir.file( "ndvi.tif" );
    REQUIRE( writeGtiff( raster, kEdge, kEdge, "EPSG:4326", true,
                         quarterNodataPixels( kEdge, kEdge, 1.0f, 0.0f ) ) );

    const std::optional<GridInfo> info = probe.grid( raster );
    REQUIRE( info.has_value() );
    CHECK( info->width == kEdge );
    CHECK( info->height == kEdge );
    CHECK( info->bandCount == 1 );
    CHECK( info->crs == "EPSG:4326" );
    // The lattice tiles the fixture exactly: a quarter of the pixels carry
    // the declared sentinel.
    CHECK( info->nodataFraction == Catch::Approx( kQuarterNodata ) );
    CHECK( info->finiteFraction == Catch::Approx( 1.0 ) );

    SECTION( "CRS-less raster: an empty crs is an honest answer, not a gap" )
    {
        const std::string bare = dir.file( "bare.tif" );
        REQUIRE( writeGtiff( bare, kEdge, kEdge, nullptr, true,
                             constantPixels( kEdge, kEdge, 1.0f ) ) );
        const std::optional<GridInfo> bareInfo = probe.grid( bare );
        REQUIRE( bareInfo.has_value() );
        CHECK( bareInfo->crs.empty() );
        CHECK( bareInfo->nodataFraction == Catch::Approx( 0.0 ) );
    }
    SECTION( "NaN pixels: tracked as non-finite, never as nodata, never stored" )
    {
        const std::string nanRaster = dir.file( "nan.tif" );
        std::vector<float> pixels = constantPixels( kEdge, kEdge, 1.0f );
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for ( int y = 0; y < kEdge / 4; ++y )
            for ( int x = 0; x < kEdge; ++x )
                pixels[static_cast<std::size_t>( y ) * kEdge + x] = nan;
        REQUIRE( writeGtiff( nanRaster, kEdge, kEdge, "EPSG:4326", true, pixels ) );
        const std::optional<GridInfo> nanInfo = probe.grid( nanRaster );
        REQUIRE( nanInfo.has_value() );
        CHECK( nanInfo->finiteFraction == Catch::Approx( 0.75 ) );
        CHECK( nanInfo->nodataFraction == Catch::Approx( 0.0 ) );
    }
    SECTION( "VRT: projected facts of its real source" )
    {
        const std::string vrt = dir.file( "ndvi.vrt" );
        TempDir::writeFile( vrt,
                            "<?xml version=\"1.0\"?>\n"
                            "<VRTDataset rasterXSize=\"96\" rasterYSize=\"96\">\n    <SRS>EPSG:4326</SRS>\n"
                            "  <VRTRasterBand dataType=\"Float32\" band=\"1\">\n"
                            "    <NoDataValue>0</NoDataValue>\n"
                            "    <SimpleSource>\n"
                            "      <SourceFilename relativeToVRT=\"1\">ndvi.tif</SourceFilename>\n"
                            "      <SourceBand>1</SourceBand>\n"
                            "    </SimpleSource>\n"
                            "  </VRTRasterBand>\n"
                            "</VRTDataset>\n" );
        const std::optional<GridInfo> vrtInfo = probe.grid( vrt );
        REQUIRE( vrtInfo.has_value() );
        CHECK( vrtInfo->width == kEdge );
        CHECK( vrtInfo->crs == "EPSG:4326" );
        CHECK( vrtInfo->nodataFraction == Catch::Approx( kQuarterNodata ) );
    }
    SECTION( "open failure and unreadable grids answer nullopt" )
    {
        CHECK_FALSE( probe.grid( dir.file( "missing.tif" ) ).has_value() );
        const std::string text = dir.file( "text.tif" );
        TempDir::writeFile( text, "this is not a raster\n" );
        CHECK_FALSE( probe.grid( text ).has_value() );
    }
    SECTION( "truncated data section: the window read refuses loudly" )
    {
        // A GTiff whose tail is cut still opens (the IFD survives), but the
        // probe's window read cannot be served honestly — nullopt either way.
        std::string bytes = TempDir::readFile( raster );
        REQUIRE( bytes.size() > 1024 );
        bytes.resize( bytes.size() / 2 );
        const std::string truncated = dir.file( "truncated.tif" );
        TempDir::writeFile( truncated, bytes );
        CHECK_FALSE( probe.grid( truncated ).has_value() );
    }
    SECTION( "multi-band grid counts every band, samples band 1" )
    {
        const std::string two = dir.file( "two.tif" );
        {
            GDALDriverH driver = GDALGetDriverByName( "GTiff" );
            REQUIRE( driver != nullptr );
            GDALDatasetH dataset =
                GDALCreate( driver, two.c_str(), kEdge, kEdge, 2, GDT_Float32, nullptr );
            REQUIRE( dataset != nullptr );
            double geotransform[6] = { 0.0, 10.0, 0.0, 0.0, 0.0, -10.0 };
            GDALSetGeoTransform( dataset, geotransform );
            for ( int bandIndex : { 1, 2 } )
            {
                GDALRasterBandH band = GDALGetRasterBand( dataset, bandIndex );
                GDALSetRasterNoDataValue( band, 0.0 );
                const std::vector<float> data = constantPixels( kEdge, kEdge, 1.0f );
                REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, kEdge, kEdge,
                                       const_cast<float *>( data.data() ), kEdge, kEdge,
                                       GDT_Float32, 0, 0 ) == CE_None );
            }
            GDALClose( dataset );
        }
        const std::optional<GridInfo> twoInfo = probe.grid( two );
        REQUIRE( twoInfo.has_value() );
        CHECK( twoInfo->bandCount == 2 );
        CHECK( twoInfo->nodataFraction == Catch::Approx( 0.0 ) );
    }
}

TEST_CASE( "Grid facts drive the engine over a real dataset",
           "[verify_adapters][grid][engine]" )
{
    TempDir dir;
    adapters::GdalGridProbe gridProbe;
    adapters::FsArtifactProbe artifactProbe;

    const std::string raster = dir.file( "ndvi.tif" );
    REQUIRE( writeGtiff( raster, kEdge, kEdge, "EPSG:4326", true,
                         quarterNodataPixels( kEdge, kEdge, 1.0f, 0.0f ) ) );

    VerificationCheckSpec grid;
    grid.checkId = "grid-matches";
    grid.kind = "artifact.grid";
    grid.params["path"] = raster;
    grid.params["width"] = kEdge;
    grid.params["height"] = kEdge;
    grid.params["bandCount"] = 1;
    grid.params["crs"] = "EPSG:4326";
    grid.params["maxNodataFraction"] = kQuarterNodata;
    grid.params["minFiniteFraction"] = 1.0;

    VerificationSpec spec;
    spec.specId = "spec.grid.e2e";
    spec.scope = "node";
    spec.checks.push_back( grid );

    const VerificationContext context{ &artifactProbe, &gridProbe, nullptr, nullptr, nullptr };
    const VerificationReport report = evaluate( spec, context );
    CHECK( report.overall == VerificationStatus::Pass );
    // No NaN reached the evidence: the report seals.
    CHECK_FALSE( report.digest().empty() );

    SECTION( "tampered bytes break the pinned digest, loudly" )
    {
        // Record the pristine whole-file digest, flip one late byte (a data
        // byte — the IFD survives), then pin the PRISTINE digest: the
        // mismatch is a typed Fail, never silence.
        const std::string pristine = TempDir::readFile( raster );
        const std::string pristineDigest = sha256Hex( pristine );
        std::string mutated = pristine;
        REQUIRE( mutated.size() > 512 );
        mutated[mutated.size() - 256] =
            static_cast<char>( mutated[mutated.size() - 256] ^ 0x55 );
        TempDir::writeFile( raster, mutated );

        VerificationCheckSpec digestCheck;
        digestCheck.checkId = "digest";
        digestCheck.kind = "reproducibility.digest";
        digestCheck.params["path"] = raster;
        digestCheck.params["expectedDigest"] = pristineDigest;

        VerificationSpec tamperSpec;
        tamperSpec.specId = "spec.grid.tamper";
        tamperSpec.scope = "node";
        tamperSpec.checks.push_back( digestCheck );

        const VerificationReport tampered = evaluate( tamperSpec, context );
        CHECK( tampered.overall == VerificationStatus::Fail );
        CHECK( tampered.checks[0].status == VerificationStatus::Fail );
        CHECK( tampered.checks[0].code == std::string( kCodeDigestMismatch ) );
    }

    SECTION( "probe cannot answer -> Indeterminate, never Pass" )
    {
        const std::string missing = dir.file( "missing.tif" );
        VerificationCheckSpec missingGrid;
        missingGrid.checkId = "missing-grid";
        missingGrid.kind = "artifact.grid";
        missingGrid.params["path"] = missing;
        missingGrid.params["width"] = kEdge;

        VerificationSpec missingSpec;
        missingSpec.specId = "spec.grid.missing";
        missingSpec.scope = "node";
        missingSpec.checks.push_back( missingGrid );

        const VerificationReport cannot = evaluate( missingSpec, context );
        CHECK( cannot.checks[0].status == VerificationStatus::Indeterminate );
        CHECK( cannot.checks[0].code == std::string( kCodeArtifactUnreadable ) );
        CHECK( cannot.overall == VerificationStatus::Indeterminate );
    }

    SECTION( "CRS-less raster fails a pinned crs as a mismatch, not a gap" )
    {
        const std::string bare = dir.file( "bare.tif" );
        REQUIRE( writeGtiff( bare, kEdge, kEdge, nullptr, true,
                             constantPixels( kEdge, kEdge, 1.0f ) ) );
        VerificationCheckSpec bareGrid;
        bareGrid.checkId = "bare-grid";
        bareGrid.kind = "artifact.grid";
        bareGrid.params["path"] = bare;
        bareGrid.params["crs"] = "EPSG:4326";

        VerificationSpec bareSpec;
        bareSpec.specId = "spec.grid.bare";
        bareSpec.scope = "node";
        bareSpec.checks.push_back( bareGrid );

        const VerificationReport mismatch = evaluate( bareSpec, context );
        CHECK( mismatch.checks[0].status == VerificationStatus::Fail );
        CHECK( mismatch.checks[0].code == std::string( kCodeGridMismatch ) );
    }
}
