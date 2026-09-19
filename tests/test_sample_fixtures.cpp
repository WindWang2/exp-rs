// test_sample_fixtures.cpp — Lab Sample Data Foundry (goal D1) acceptance
// suite. Lean target: links only Catch2 + sicnu_sample_foundry (GDAL + jsoncpp
// transitively) — no Qt/QGIS — per the sicnu_add_io_test pattern. Fixtures are
// generated at runtime into temp dirs (repo convention: no tracked rasters).
//
// Covers:
//   * SHA-256 (FIPS 180-4 vectors) — the manifest's hash primitive
//   * deterministic kernels: class map (golden counts), analytic DEM surface +
//     closed-form gradient (finite-difference cross-check), slope/aspect truth,
//     change model, portable noise
//   * E2E lab generation: 12 emitted files, grids, CRS, geotransform,
//     SICNU_BAND_ROLE / wavelength metadata, NoData, truth coherence
//     (class counts, change area, DEM closed form, ROI-on-class), DBF date pin
//   * manifest + --verify (clean pass, drift detection, self-fingerprint)
//   * CLI: byte-identical determinism, seed behavior (truth content is
//     seed-independent; provenance stamps are not), typed exit codes
//     (2 usage / 3 spec refusal / 4 verify drift), spec-directory intake
//   * stress profile smoke (2048x2048 grid + closed-form spot checks)

#include "sample_foundry.h"
#include "sha256.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>
#include <json/json.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace sicnu::foundry;
using Catch::Approx;

namespace fs = std::filesystem;

namespace
{

// ---------------------------------------------------------------------------
// Golden constants — lab grid (256 x 256). The class map and change mask are
// pure analytic functions of the pixel grid, so these counts hold for EVERY
// seed; they pin the ground truth against silent generator drift.
// ---------------------------------------------------------------------------
constexpr long kGoldenLabClassCount[7] = {
  0, 9728, 32842, 7854, 8242, 4814, 2056, // index = class id 1..6
};
constexpr long kGoldenLabTotalChanged = 6180;

struct TempDir
{
    fs::path path;
    TempDir()
    {
        static std::atomic<int> counter{ 0 };
        const int n = counter.fetch_add( 1 );
#ifdef _WIN32
        const int pid = _getpid();
#else
        const int pid = static_cast<int>( getpid() );
#endif
        path = fs::temp_directory_path() / ( "sicnu-foundry-test-" + std::to_string( pid ) +
                                             "-" + std::to_string( n ) );
        fs::remove_all( path );
        fs::create_directories( path );
    }
    ~TempDir() { std::error_code ec; fs::remove_all( path, ec ); }
    std::string str() const { return path.string(); }
};

std::string readBinary( const fs::path &path )
{
    std::ifstream in( path, std::ios::binary );
    REQUIRE( in );
    return std::string( ( std::istreambuf_iterator<char>( in ) ),
                        std::istreambuf_iterator<char>() );
}

/// Quote one argument for the platform shell. Test paths are ASCII; this is
/// for robustness, not adversarial input.
std::string shellQuote( const std::string &word )
{
#ifdef _WIN32
    // cmd.exe treats single quotes as literal characters; double quotes are
    // the argument quoting there. std::system already routes through cmd.
    std::string out = "\"";
    for ( char c : word )
    {
        if ( c == '"' )
            out += "\"\""; // cmd doubling is imperfect but fine for our args
        else
            out += c;
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for ( char c : word )
    {
        if ( c == '\'' )
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
#endif
}

int runCli( const std::vector<std::string> &args )
{
    std::string cmd = shellQuote( SICNU_GENERATE_SAMPLES_BIN );
    for ( const std::string &arg : args )
        cmd += " " + shellQuote( arg );
    const int status = std::system( cmd.c_str() );
#ifdef _WIN32
    return status; // std::system via cmd.exe returns the exit code directly
#else
    return WIFEXITED( status ) ? WEXITSTATUS( status ) : status;
#endif
}

Json::Value parseJsonBytes( const std::string &bytes )
{
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream stream( bytes );
    REQUIRE( Json::parseFromStream( builder, stream, &root, &errs ) );
    return root;
}

GDALDataset *openRaster( const fs::path &path )
{
    return static_cast<GDALDataset *>( GDALOpenEx( path.string().c_str(),
                                                   GDAL_OF_RASTER | GDAL_OF_READONLY,
                                                   nullptr, nullptr, nullptr ) );
}

void checkGrid( GDALDataset *ds, int width, int height )
{
    REQUIRE( ds != nullptr );
    REQUIRE( ds->GetRasterXSize() == width );
    REQUIRE( ds->GetRasterYSize() == height );
    const OGRSpatialReference *srs = ds->GetSpatialRef();
    REQUIRE( srs != nullptr );
    const char *code = srs->GetAuthorityCode( nullptr );
    REQUIRE( code != nullptr );
    CHECK( std::string( code ) == "32648" );
    double gt[6] = {};
    CHECK( ds->GetGeoTransform( gt ) == CE_None );
    CHECK( gt[0] == Approx( 500000.0 ) );
    CHECK( gt[1] == Approx( 30.0 ) );
    CHECK( gt[2] == Approx( 0.0 ) );
    CHECK( gt[3] == Approx( 4060000.0 ) );
    CHECK( gt[4] == Approx( 0.0 ) );
    CHECK( gt[5] == Approx( -30.0 ) );
    CHECK( std::string( ds->GetMetadataItem( "SICNU_GENERATOR" ) ) ==
           std::string( kGeneratorName ) );
    CHECK( ds->GetMetadataItem( "SICNU_SEED" ) != nullptr );
}

/// Independent re-derivation of the documented surface (ADR 0164): three
/// compact polynomial bumps on a tilted plane with a bilinear ripple.
double referenceElevation( double nx, double ny )
{
    auto bump = []( double dx, double dy, double amplitude, double r2 ) {
        const double q = dx * dx + dy * dy;
        if ( q >= r2 )
            return 0.0;
        const double t = 1.0 - q / r2;
        return amplitude * t * t * t;
    };
    return 100.0 + 200.0 * ny + 8.0 * ( nx - 0.5 ) * ( ny - 0.5 ) +
           bump( nx - 0.3, ny - 0.4, 150.0, 0.0225 ) +
           bump( nx - 0.7, ny - 0.6, 100.0, 0.04 ) +
           bump( nx - 0.5, ny - 0.5, -80.0, 0.0144 );
}

/// Finite-difference gradient of the reference surface (validates that the
/// closed-form gradient really differentiates the emitted elevation).
void referenceGradientFd( double nx, double ny, double *gx, double *gy )
{
    const double h = 1.0e-5;
    *gx = ( referenceElevation( nx + h, ny ) - referenceElevation( nx - h, ny ) ) / ( 2.0 * h );
    *gy = ( referenceElevation( nx, ny + h ) - referenceElevation( nx, ny - h ) ) / ( 2.0 * h );
}

GridSpec labGrid() { return gridForProfile( Profile::Lab ); }

std::vector<uint8_t> readBytePlane( GDALDataset *ds )
{
    std::vector<uint8_t> plane( static_cast<std::size_t>( ds->GetRasterXSize() ) *
                                ds->GetRasterYSize() );
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Read, 0, 0, ds->GetRasterXSize(),
                                               ds->GetRasterYSize(), plane.data(),
                                               ds->GetRasterXSize(), ds->GetRasterYSize(),
                                               GDT_Byte, 0, 0 ) == CE_None );
    return plane;
}

float readFloatPixel( GDALDataset *ds, int x, int y )
{
    float v = 0.0f;
    REQUIRE( ds->GetRasterBand( 1 )->RasterIO( GF_Read, x, y, 1, 1, &v, 1, 1, GDT_Float32,
                                               0, 0 ) == CE_None );
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. hash primitive
// ---------------------------------------------------------------------------

TEST_CASE( "SHA-256 matches FIPS 180-4 test vectors", "[foundry][sha256]" )
{
    CHECK( sha256Hex( "", 0 ) ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" );
    CHECK( sha256Hex( "abc", 3 ) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" );
    CHECK( sha256Hex( "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56 ) ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" );
    const std::string million_a( 1000000, 'a' );
    CHECK( sha256Hex( million_a.data(), million_a.size() ) ==
           "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" );
}

// ---------------------------------------------------------------------------
// 2. deterministic kernels
// ---------------------------------------------------------------------------

TEST_CASE( "classifyPixel follows the docs/labs land-cover table", "[foundry][kernels]" )
{
    CHECK( classifyPixel( 0.5, 0.9 ) == 1 );  // water (south strip)
    CHECK( classifyPixel( 0.45, 0.15 ) == 2 );// vegetation (default)
    CHECK( classifyPixel( 0.1, 0.1 ) == 3 );  // urban (north-west)
    CHECK( classifyPixel( 0.7, 0.5 ) == 4 );  // bare soil (disc)
    CHECK( classifyPixel( 0.95, 0.5 ) == 5 ); // forest (east block)
    CHECK( classifyPixel( 0.3, 0.6 ) == 6 );  // shadow (valley disc)
}

TEST_CASE( "Lab class map reproduces the golden class counts", "[foundry][truth]" )
{
    const GridSpec grid = labGrid();
    std::vector<long> counts( 7, 0 );
    for ( int y = 0; y < grid.height; ++y )
    {
        for ( int x = 0; x < grid.width; ++x )
        {
            const uint8_t klass = classifyPixel( ( x + 0.5 ) / grid.width,
                                                 ( y + 0.5 ) / grid.height );
            ++counts[klass];
        }
    }
    long total = 0;
    for ( int k = 1; k <= 6; ++k )
    {
        CHECK( counts[k] == kGoldenLabClassCount[k] );
        total += counts[k];
    }
    CHECK( total == 65536 );
}

TEST_CASE( "DEM surface matches its closed form and its gradient matches it",
           "[foundry][truth]" )
{
    for ( double ny = 0.05; ny < 1.0; ny += 0.15 )
    {
        for ( double nx = 0.05; nx < 1.0; nx += 0.15 )
        {
            INFO( "nx=" << nx << " ny=" << ny );
            CHECK( demElevation( nx, ny ) == Approx( referenceElevation( nx, ny ) ).margin( 1e-9 ) );

            double gx = 0.0;
            double gy = 0.0;
            demGradient( nx, ny, &gx, &gy );
            double fdx = 0.0;
            double fdy = 0.0;
            referenceGradientFd( nx, ny, &fdx, &fdy );
            CHECK( gx == Approx( fdx ).margin( 1e-4 ) );
            CHECK( gy == Approx( fdy ).margin( 1e-4 ) );
        }
    }
}

TEST_CASE( "Slope/aspect truth converts grid gradients with the documented convention",
           "[foundry][truth]" )
{
    const GridSpec grid = labGrid();
    double slope = 0.0;
    double aspect = 0.0;
    slopeAspectDegrees( 0.0, 0.0, grid, &slope, &aspect );
    CHECK( slope == 0.0 );
    CHECK( aspect == kAspectNoData );

    // West-facing gradient (+east, no north component): descent points west.
    slopeAspectDegrees( 1.0, 0.0, grid, &slope, &aspect );
    CHECK( slope > 0.0 );
    CHECK( aspect == Approx( 270.0 ).margin( 0.01 ) );

    // North-facing descent (elevation rises southward => descent points north).
    slopeAspectDegrees( 0.0, 1.0, grid, &slope, &aspect );
    CHECK( aspect == Approx( 0.0 ).margin( 0.01 ) );

    // Quantization to 0.01 degrees.
    slopeAspectDegrees( 0.123456, 0.654321, grid, &slope, &aspect );
    CHECK( slope == Approx( std::llround( slope * 100.0 ) / 100.0 ).margin( 1e-12 ) );
    CHECK( aspect >= 0.0 );
    CHECK( aspect <= 360.0 );
}

TEST_CASE( "Change model: deforestation disc and value ranges", "[foundry][kernels]" )
{
    CHECK( changePixel( 0.4, 0.5 ) );
    CHECK( changePixel( 0.42, 0.52 ) );
    CHECK_FALSE( changePixel( 0.8, 0.5 ) );
    CHECK_FALSE( changePixel( 0.3, 0.9 ) ); // also water: order irrelevant here

    const double before = changeBefore( 0.31, 0.44 );
    CHECK( before == Approx( 0.40 + 0.40 * ( 0.31 - 0.5 ) * ( 0.44 - 0.5 ) ).margin( 1e-12 ) );
    CHECK( changeAfter( 0.4, 0.5, true ) < 0.2 );
    CHECK( changeAfter( 0.4, 0.5, true ) < changeBefore( 0.4, 0.5 ) - 0.2 );
    CHECK( changeAfter( 0.8, 0.2, false ) ==
           Approx( changeBefore( 0.8, 0.2 ) + 0.01 * 0.6 ).margin( 1e-12 ) );
}

TEST_CASE( "uniformNoise is the documented portable transform", "[foundry][kernels]" )
{
    CHECK( uniformNoise( 0u, 0.02 ) == Approx( -0.02 ).margin( 1e-15 ) );
    CHECK( uniformNoise( 4294967295u, 0.02 ) < 0.02 );
    CHECK( uniformNoise( 4294967295u, 0.02 ) > 0.02 - 1e-8 );
    CHECK( uniformNoise( 2147483648u, 0.02 ) == 0.0 );
    // Monotone in the raw draw (one draw in, one value out).
    CHECK( uniformNoise( 100u, 0.02 ) < uniformNoise( 200u, 0.02 ) );
}

// ---------------------------------------------------------------------------
// 3. E2E: lab generation, metadata, truth coherence, manifest
// ---------------------------------------------------------------------------

TEST_CASE( "generate(lab) emits the full docs/labs set with truth companions",
           "[foundry][e2e]" )
{
    GDALAllRegister();
    CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );

    TempDir dir;
    Options options;
    options.out_dir = dir.str();
    options.seed = 42;
    GenerateResult result;
    REQUIRE( generate( options, &result ).ok );

    REQUIRE( result.files.size() == 12 );
    REQUIRE( fs::exists( dir.path / "manifest.json" ) );

    // Manifest shape.
    const Json::Value manifest = parseJsonBytes( readBinary( dir.path / "manifest.json" ) );
    {
        CHECK( manifest["manifest_version"].asInt() == 1 );
        CHECK( manifest["seed"].asUInt() == 42 );
        CHECK( std::string( manifest["profile"].asString() ) == "lab" );
        CHECK( manifest["grid"]["width"].asInt() == 256 );
        CHECK( std::string( manifest["grid"]["crs"].asString() ) == "EPSG:32648" );
        CHECK( manifest["files"].size() == 12 );
        CHECK( manifest["products"].size() == 9 );
        const std::string fp = manifest["self_fingerprint"].asString();
        REQUIRE( fp.size() == 64 );
        for ( char c : fp )
            CHECK( ( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) ) );
        // Files sorted by name, every sha256 well-formed.
        std::string prev;
        for ( const Json::Value &entry : manifest["files"] )
        {
            const std::string name = entry["name"].asString();
            CHECK( name > prev );
            prev = name;
            CHECK( entry["sha256"].asString().size() == 64 );
        }
    }

    // Manifest hashes must match the files on disk (foundry hashes itself).
    for ( const EmittedFile &file : result.files )
    {
        const std::string bytes = readBinary( dir.path / file.name );
        CHECK( bytes.size() == file.bytes );
        CHECK( sha256Hex( bytes.data(), bytes.size() ) == file.sha256 );
    }

    // Clean verify passes.
    VerifyReport report;
    REQUIRE( verifyDirectory( dir.str(), &report ).ok );
    CHECK( report.problems.empty() );
    CHECK( report.files_checked == 12 );

    // Rasters: grids, CRS, geotransform, provenance metadata.
    GDALDataset *landsat = openRaster( dir.path / "landsat_sample.tif" );
    checkGrid( landsat, 256, 256 );
    REQUIRE( landsat->GetRasterCount() == 7 );
    CHECK( landsat->GetRasterBand( 1 )->GetRasterDataType() == GDT_Float32 );
    static const char *kRoles[7] = { "coastal", "blue", "green", "red", "nir", "swir1", "swir2" };
    static const char *kWave[7] = { "440", "480", "560", "660", "870", "1610", "2200" };
    for ( int b = 1; b <= 7; ++b )
    {
        CHECK( std::string( landsat->GetRasterBand( b )->GetMetadataItem( "SICNU_BAND_ROLE" ) ) ==
               std::string( kRoles[b - 1] ) );
        CHECK( std::string( landsat->GetRasterBand( b )->GetMetadataItem( "WAVELENGTH" ) ) ==
               std::string( kWave[b - 1] ) );
        CHECK( std::string( landsat->GetRasterBand( b )->GetMetadataItem( "WAVELENGTH_UNITS" ) ) ==
               "nm" );
        CHECK( landsat->GetRasterBand( b )->GetMetadataItem( "FWHM" ) != nullptr );
    }
    CHECK( std::string( landsat->GetMetadataItem( "SICNU_PRODUCT" ) ) == "landsat_sample" );
    CHECK( std::string( landsat->GetMetadataItem( "SICNU_SEED" ) ) == "42" );

    // Optical values sit within noise of the class signature at truth pixels.
    GDALDataset *truth = openRaster( dir.path / "landsat_truth.tif" );
    checkGrid( truth, 256, 256 );
    const std::vector<uint8_t> cls = readBytePlane( truth );
    static const double kSignatures[6][7] = {
      { 0.05, 0.06, 0.08, 0.04, 0.02, 0.01, 0.005 },
      { 0.04, 0.05, 0.08, 0.04, 0.45, 0.15, 0.08 },
      { 0.12, 0.13, 0.15, 0.17, 0.20, 0.25, 0.22 },
      { 0.15, 0.18, 0.22, 0.30, 0.35, 0.40, 0.38 },
      { 0.03, 0.04, 0.06, 0.03, 0.50, 0.12, 0.06 },
      { 0.01, 0.01, 0.02, 0.01, 0.01, 0.01, 0.005 },
    };
    static const double kSigma = 0.02;
    std::vector<float> optical_pixel( 7 );
    for ( int y = 0; y < 256; y += 8 )
    {
        for ( int x = 0; x < 256; x += 8 )
        {
            const uint8_t klass = cls[static_cast<std::size_t>( y ) * 256 + x];
            REQUIRE( klass >= 1 );
            REQUIRE( klass <= 6 );
            for ( int b = 0; b < 7; ++b )
            {
                const float v = [&] {
                    float pv = 0.0f;
                    REQUIRE( landsat->GetRasterBand( b + 1 )->RasterIO(
                               GF_Read, x, y, 1, 1, &pv, 1, 1, GDT_Float32, 0, 0 ) == CE_None );
                    return pv;
                }();
                // Noise is uniform in [-sigma, sigma), clamped at [0,1].
                CHECK( v >= kSignatures[klass - 1][b] - kSigma - 1e-6 );
                CHECK( v <= kSignatures[klass - 1][b] + kSigma + 1e-6 );
            }
        }
    }
    GDALClose( landsat );

    // Sampled counts must be consistent with the full golden counts.
    // (We assert the exact totals by reading the whole plane once more.)
    {
        long full_counts[7] = {};
        for ( uint8_t klass : cls )
            ++full_counts[klass];
        for ( int k = 1; k <= 6; ++k )
            CHECK( full_counts[k] == kGoldenLabClassCount[k] );
    }
    CHECK( truth->GetRasterBand( 1 )->GetNoDataValue() == Approx( 0.0 ) );
    GDALClose( truth );

    // DEM: emitted pixels equal the closed form exactly (same double -> float).
    GDALDataset *dem = openRaster( dir.path / "dem_sample.tif" );
    checkGrid( dem, 256, 256 );
    for ( const auto &[px, py] : std::vector<std::pair<int, int>>{ { 10, 10 }, { 128, 128 }, { 200, 100 }, { 64, 200 } } )
    {
        const float emitted = readFloatPixel( dem, px, py );
        const float expected = static_cast<float>(
          demElevation( ( px + 0.5 ) / 256.0, ( py + 0.5 ) / 256.0 ) );
        CHECK( emitted == expected );
    }
    CHECK( dem->GetRasterBand( 1 )->GetNoDataValue() == Approx( -9999.0 ) );
    GDALClose( dem );

    // Slope/aspect truth: exact float equality with the kernel, sane ranges.
    GDALDataset *slope_ds = openRaster( dir.path / "dem_slope_truth.tif" );
    GDALDataset *aspect_ds = openRaster( dir.path / "dem_aspect_truth.tif" );
    checkGrid( slope_ds, 256, 256 );
    checkGrid( aspect_ds, 256, 256 );
    for ( int y = 0; y < 256; y += 17 )
    {
        for ( int x = 0; x < 256; x += 17 )
        {
            const double nx = ( x + 0.5 ) / 256.0;
            const double ny = ( y + 0.5 ) / 256.0;
            double gx = 0.0;
            double gy = 0.0;
            demGradient( nx, ny, &gx, &gy );
            double s = 0.0;
            double a = 0.0;
            slopeAspectDegrees( gx, gy, labGrid(), &s, &a );
            CHECK( readFloatPixel( slope_ds, x, y ) == static_cast<float>( s ) );
            CHECK( readFloatPixel( aspect_ds, x, y ) == static_cast<float>( a ) );
            CHECK( s >= 0.0 );
            CHECK( ( a == kAspectNoData || ( a >= 0.0 && a <= 360.0 ) ) );
        }
    }
    GDALClose( slope_ds );
    GDALClose( aspect_ds );

    // Change pair + change truth.
    GDALDataset *before = openRaster( dir.path / "change_before.tif" );
    GDALDataset *after = openRaster( dir.path / "change_after.tif" );
    GDALDataset *change_truth = openRaster( dir.path / "change_truth.tif" );
    checkGrid( before, 256, 256 );
    checkGrid( after, 256, 256 );
    checkGrid( change_truth, 256, 256 );
    CHECK( before->GetRasterBand( 1 )->GetNoDataValue() == Approx( -9999.0 ) );
    CHECK( after->GetRasterBand( 1 )->GetNoDataValue() == Approx( -9999.0 ) );
    CHECK( change_truth->GetRasterBand( 1 )->GetNoDataValue() == Approx( 255.0 ) );
    const std::vector<uint8_t> changed = readBytePlane( change_truth );
    long changed_count = 0;
    for ( uint8_t v : changed )
        changed_count += v;
    CHECK( changed_count == kGoldenLabTotalChanged );
    GDALClose( before );
    GDALClose( after );
    GDALClose( change_truth );

    // ROIs: 6 features, one per class, ids ascending, each ROI's
    // interior pixels actually belong to its class in the truth mask.
    GDALDataset *roi_ds = static_cast<GDALDataset *>(
      GDALOpenEx( ( dir.path / "training_samples.shp" ).string().c_str(),
                  GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr ) );
    REQUIRE( roi_ds != nullptr );
    OGRLayer *layer = roi_ds->GetLayer( 0 ); // Shapefile driver: layer is named
    REQUIRE( layer != nullptr );             // after the file, not CreateLayer().
    CHECK( layer->GetFeatureCount() == 6 );
    int seen_ids = 0;
    for ( const OGRFeatureUniquePtr &feature : layer )
    {
        const int class_id = feature->GetFieldAsInteger( "class_id" );
        REQUIRE( class_id >= 1 );
        REQUIRE( class_id <= 6 );
        seen_ids |= 1 << ( class_id - 1 );
        CHECK( std::string( feature->GetFieldAsString( "class_name" ) ).size() > 3 );
        const OGRGeometry *geom = feature->GetGeometryRef();
        REQUIRE( geom != nullptr );
        REQUIRE( wkbFlatten( geom->getGeometryType() ) == wkbPolygon );
        OGREnvelope envelope;
        geom->getEnvelope( &envelope );
        // Sample the ROI center pixel through the truth mask.
        const double center_east = ( envelope.MinX + envelope.MaxX ) / 2.0;
        const double center_north = ( envelope.MinY + envelope.MaxY ) / 2.0;
        const int px = static_cast<int>( ( center_east - 500000.0 ) / 30.0 );
        const int py = static_cast<int>( ( 4060000.0 - center_north ) / 30.0 );
        REQUIRE( px >= 0 );
        REQUIRE( px < 256 );
        REQUIRE( py >= 0 );
        REQUIRE( py < 256 );
        CHECK( cls[static_cast<std::size_t>( py ) * 256 + px] == class_id );
        // All four corners of the envelope must map inside the raster too.
        CHECK( envelope.MaxX <= 507680.0 + 1e-9 );
        CHECK( envelope.MinY >= 4052320.0 - 1e-9 );
    }
    CHECK( seen_ids == 0b111111 );
    GDALClose( roi_ds );

    // DBF date bytes pinned to 2000-01-01 (byte-determinism across days).
    const std::string dbf = readBinary( dir.path / "training_samples.dbf" );
    REQUIRE( dbf.size() > 4 );
    CHECK( dbf[1] == static_cast<char>( 100 ) );
    CHECK( dbf[2] == 1 );
    CHECK( dbf[3] == 1 );

    // Drift detection: corrupt one file; verify must flag it.
    {
        std::ofstream out( dir.path / "change_after.tif", std::ios::binary | std::ios::app );
        out << 'x';
    }
    VerifyReport drift;
    REQUIRE( verifyDirectory( dir.str(), &drift ).ok );
    REQUIRE( drift.problems.size() == 2 ); // size drift + sha256 drift
    CHECK( drift.problems[0] == "sha256 drift: change_after.tif" );
    CHECK( drift.problems[1] == "size drift: change_after.tif" );
}

TEST_CASE( "generate(lab) landsat truth carries class-name metadata", "[foundry][e2e]" )
{
    GDALAllRegister();
    TempDir dir;
    Options options;
    options.out_dir = dir.str();
    options.seed = 1;
    GenerateResult result;
    REQUIRE( generate( options, &result ).ok );
    GDALDataset *truth = openRaster( dir.path / "landsat_truth.tif" );
    REQUIRE( truth != nullptr );
    const char *names = truth->GetRasterBand( 1 )->GetMetadataItem( "SICNU_CLASS_NAMES" );
    REQUIRE( names != nullptr );
    CHECK( std::string( names ) == std::string( classNamesCsv() ) );
    GDALClose( truth );
}

TEST_CASE( "generate twice into the same directory replaces the shapefile",
           "[foundry][e2e][overwrite]" )
{
    GDALAllRegister();
    TempDir dir;
    Options options;
    options.out_dir = dir.str();
    options.seed = 42;

    GenerateResult first;
    REQUIRE( generate( options, &first ).ok );
    std::map<std::string, std::string> first_bytes;
    for ( const EmittedFile &file : first.files )
        first_bytes[file.name] = readBinary( dir.path / file.name );

    // The second generate into the SAME non-empty directory must replace the
    // shapefile (the driver refuses to create over an existing dataset) instead
    // of failing after the rasters were already rewritten (F-1032-P1-shp).
    GenerateResult second;
    REQUIRE( generate( options, &second ).ok );
    CHECK( second.files.size() == first.files.size() );
    for ( const EmittedFile &file : second.files )
        CHECK( first_bytes[file.name] == readBinary( dir.path / file.name ) );

    VerifyReport report;
    REQUIRE( verifyDirectory( dir.str(), &report ).ok );
    CHECK( report.problems.empty() );

    GDALDataset *roi_ds = static_cast<GDALDataset *>(
        GDALOpenEx( ( dir.path / "training_samples.shp" ).string().c_str(),
                    GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr ) );
    REQUIRE( roi_ds != nullptr );
    OGRLayer *layer = roi_ds->GetLayer( 0 );
    REQUIRE( layer != nullptr );
    CHECK( layer->GetFeatureCount() == 6 );
    GDALClose( roi_ds );
}

TEST_CASE( "subset generate prunes leftover catalog files so verify still passes",
           "[foundry][e2e][stale]" )
{
    GDALAllRegister();
    TempDir dir;
    Options full;
    full.out_dir = dir.str();
    full.seed = 42;
    GenerateResult first;
    REQUIRE( generate( full, &first ).ok );

    // A file that is not a known catalog basename must never be touched.
    {
        std::ofstream out( dir.path / "keepme.txt" );
        out << "operator notes";
        REQUIRE( out );
    }

    Options subset;
    subset.out_dir = dir.str();
    subset.products = { Product::DemSample };
    GenerateResult second;
    REQUIRE( generate( subset, &second ).ok );
    REQUIRE( second.files.size() == 1 );

    // Catalog basenames outside this selection are pruned (they would fail
    // --verify as unlisted data files); the selection survives; anything else
    // is left exactly as it was (F-1032-P1-stale).
    CHECK( fs::exists( dir.path / "dem_sample.tif" ) );
    CHECK_FALSE( fs::exists( dir.path / "landsat_sample.tif" ) );
    CHECK_FALSE( fs::exists( dir.path / "change_before.tif" ) );
    CHECK_FALSE( fs::exists( dir.path / "training_samples.shp" ) );
    CHECK_FALSE( fs::exists( dir.path / "training_samples.dbf" ) );
    CHECK( fs::exists( dir.path / "keepme.txt" ) );

    VerifyReport report;
    REQUIRE( verifyDirectory( dir.str(), &report ).ok );
    CHECK( report.problems.empty() );
    CHECK( report.files_checked == 1 );
}

TEST_CASE( "generate is byte-identical under a hostile GDAL environment",
           "[foundry][cli][determinism]" )
{
    GDALAllRegister();

    // Hostile ambient environment: a multi-threaded DEFLATE pool and an
    // encoding that used to make the shapefile driver emit training_samples.cpg
    // (ADR 0164: emit bytes must not depend on the host environment).
    CPLSetConfigOption( "GDAL_NUM_THREADS", "8" );
    CPLSetConfigOption( "SHAPE_ENCODING", "UTF-8" );
    struct RestoreGdalEnv
    {
        ~RestoreGdalEnv()
        {
            CPLSetConfigOption( "GDAL_NUM_THREADS", nullptr );
            CPLSetConfigOption( "SHAPE_ENCODING", nullptr );
        }
    } restore;

    TempDir hostile;
    Options options;
    options.out_dir = hostile.str();
    options.seed = 42;
    GenerateResult first;
    REQUIRE( generate( options, &first ).ok );
    CHECK_FALSE( fs::exists( hostile.path / "training_samples.cpg" ) );

    // Drop the hostile options: the reference run sees a clean environment.
    CPLSetConfigOption( "GDAL_NUM_THREADS", nullptr );
    CPLSetConfigOption( "SHAPE_ENCODING", nullptr );

    TempDir clean;
    Options reference;
    reference.out_dir = clean.str();
    reference.seed = 42;
    GenerateResult second;
    REQUIRE( generate( reference, &second ).ok );

    CHECK( first.manifest_bytes == second.manifest_bytes );
    for ( const EmittedFile &file : second.files )
        CHECK( readBinary( hostile.path / file.name ) == readBinary( clean.path / file.name ) );

    VerifyReport report;
    REQUIRE( verifyDirectory( hostile.str(), &report ).ok );
    CHECK( report.problems.empty() );
}

// ---------------------------------------------------------------------------
// 4. CLI subprocess: determinism, seeds, typed exits, spec intake
// ---------------------------------------------------------------------------

TEST_CASE( "CLI: same seed twice produces byte-identical outputs and manifests",
           "[foundry][cli][determinism]" )
{
    TempDir dir_a;
    TempDir dir_b;
    REQUIRE( runCli( { "--out=" + dir_a.str(), "--seed=42" } ) == 0 );
    REQUIRE( runCli( { "--out=" + dir_b.str(), "--seed=42" } ) == 0 );
    const std::string manifest_a = readBinary( dir_a.path / "manifest.json" );
    const std::string manifest_b = readBinary( dir_b.path / "manifest.json" );
    CHECK( manifest_a == manifest_b );
    const Json::Value manifest = parseJsonBytes( manifest_a );
    for ( const Json::Value &entry : manifest["files"] )
    {
        const std::string name = entry["name"].asString();
        CHECK( readBinary( dir_a.path / name ) == readBinary( dir_b.path / name ) );
    }
}

TEST_CASE( "CLI: seed changes noise-bearing products but not truth content",
           "[foundry][cli][determinism]" )
{
    TempDir dir_a;
    TempDir dir_b;
    REQUIRE( runCli( { "--out=" + dir_a.str(), "--seed=42" } ) == 0 );
    REQUIRE( runCli( { "--out=" + dir_b.str(), "--seed=7" } ) == 0 );

    GDALAllRegister();
    // Truth MASK CONTENT is seed-independent (pixel-identical), even though
    // file bytes differ via the SICNU_SEED provenance stamp.
    for ( const char *name : { "landsat_truth.tif", "change_truth.tif" } )
    {
        GDALDataset *a = openRaster( dir_a.path / name );
        GDALDataset *b = openRaster( dir_b.path / name );
        REQUIRE( a != nullptr );
        REQUIRE( b != nullptr );
        CHECK( readBytePlane( a ) == readBytePlane( b ) );
        GDALClose( a );
        GDALClose( b );
    }
    // Optical raster bytes differ with the seed.
    CHECK( readBinary( dir_a.path / "landsat_sample.tif" ) !=
           readBinary( dir_b.path / "landsat_sample.tif" ) );
    // Manifests differ (seed + hashes).
    CHECK( readBinary( dir_a.path / "manifest.json" ) !=
           readBinary( dir_b.path / "manifest.json" ) );
    // Vector output is seed-independent byte-for-byte.
    CHECK( readBinary( dir_a.path / "training_samples.shp" ) ==
           readBinary( dir_b.path / "training_samples.shp" ) );
}

TEST_CASE( "CLI: typed exit codes", "[foundry][cli]" )
{
    TempDir dir;
    // verify on an empty directory -> verify failure (missing manifest).
    CHECK( runCli( { "--verify", "--out=" + dir.str() } ) == 4 );
    // usage errors -> 2
    CHECK( runCli( { "--out=" + dir.str(), "--profile=bogus" } ) == 2 );
    CHECK( runCli( { "--out=" + dir.str(), "--seed=nope" } ) == 2 );
    CHECK( runCli( { "--verify", "--out=" + dir.str(), "--seed=5" } ) == 2 );
    CHECK( runCli( { "--frobnicate" } ) == 2 );

    // spec refusals -> 3
    CHECK( runCli( { "--out=" + dir.str(), "--spec=" + ( dir.path / "missing.json" ).string() } ) == 3 );
    const std::string bad = ( dir.path / "bad.json" ).string();
    {
        std::ofstream out( bad );
        out << "{\"experiment\":\"x\",\"products\":[\"not_a_product\"]}";
    }
    CHECK( runCli( { "--out=" + dir.str(), "--spec=" + bad } ) == 3 );
    {
        std::ofstream out( bad );
        out << "{\"experiment\":\"x\"}";
    }
    CHECK( runCli( { "--out=" + dir.str(), "--spec=" + bad } ) == 3 );
    {
        std::ofstream out( bad );
        out << "not json at all";
    }
    CHECK( runCli( { "--out=" + dir.str(), "--spec=" + bad } ) == 3 );
    // Unknown spec keys are refused, never silently ignored.
    {
        std::ofstream out( bad );
        out << "{\"experiment\":\"x\",\"products\":[\"dem_sample\"],\"auto_run\":true}";
    }
    CHECK( runCli( { "--out=" + dir.str(), "--spec=" + bad } ) == 3 );

    // Seed boundaries: 0 and 4294967295 are valid, 2^32 is not; --out= is a
    // usage error (no silent default for an explicitly empty value).
    TempDir seeds;
    CHECK( runCli( { "--out=" + seeds.str(), "--seed=0" } ) == 0 );
    TempDir seeds_max;
    CHECK( runCli( { "--out=" + seeds_max.str(), "--seed=4294967295" } ) == 0 );
    CHECK( runCli( { "--out=" + seeds.str(), "--seed=4294967296" } ) == 2 );
    CHECK( runCli( { "--out=" } ) == 2 );
}

TEST_CASE( "CLI: spec intake drives the selection (single file and directory)",
           "[foundry][cli][spec]" )
{
    GDALAllRegister();
    TempDir dir;
    TempDir spec_dir;

    const std::string spec1 = ( spec_dir.path / "b-second.json" ).string();
    const std::string spec2 = ( spec_dir.path / "a-first.json" ).string();
    {
        std::ofstream out( spec1 );
        out << "{\"experiment\":\"lab-3\",\"products\":[\"change_truth\",\"dem_sample\"]}";
    }
    {
        std::ofstream out( spec2 );
        out << "{\"experiment\":\"lab-3\",\"products\":[\"dem_slope_truth\"]}";
    }
    REQUIRE( runCli( { "--out=" + dir.str(), "--spec=" + spec_dir.str() } ) == 0 );

    const Json::Value manifest =
      parseJsonBytes( readBinary( dir.path / "manifest.json" ) );
    // Union of both specs, canonical catalog order.
    REQUIRE( manifest["products"].size() == 3 );
    CHECK( manifest["products"][0].asString() == "dem_sample" );
    CHECK( manifest["products"][1].asString() == "change_truth" );
    CHECK( manifest["products"][2].asString() == "dem_slope_truth" );
    REQUIRE( manifest["files"].size() == 3 );
    CHECK( manifest["files"][0]["name"].asString() == "change_truth.tif" );
    CHECK( manifest["files"][1]["name"].asString() == "dem_sample.tif" );
    CHECK( manifest["files"][2]["name"].asString() == "dem_slope_truth.tif" );

    // Verify accepts the subset bundle.
    VerifyReport report;
    REQUIRE( verifyDirectory( dir.str(), &report ).ok );
    CHECK( report.problems.empty() );

    // Mismatched experiments across spec files are a typed refusal.
    {
        std::ofstream out( spec1 );
        out << "{\"experiment\":\"other\",\"products\":[\"dem_sample\"]}";
    }
    TempDir dir2;
    CHECK( runCli( { "--out=" + dir2.str(), "--spec=" + spec_dir.str() } ) == 3 );

    // Duplicate products across two spec files are a typed refusal too.
    {
        std::ofstream out( spec1 );
        out << "{\"experiment\":\"lab-3\",\"products\":[\"dem_sample\"]}";
        std::ofstream out2( spec2 );
        out2 << "{\"experiment\":\"lab-3\",\"products\":[\"dem_sample\"]}";
    }
    TempDir dir3;
    CHECK( runCli( { "--out=" + dir3.str(), "--spec=" + spec_dir.str() } ) == 3 );
}

TEST_CASE( "CLI: --verify detects drift", "[foundry][cli]" )
{
    TempDir dir;
    REQUIRE( runCli( { "--out=" + dir.str(), "--seed=42" } ) == 0 );
    CHECK( runCli( { "--verify", "--out=" + dir.str() } ) == 0 );
    // Remove a file -> missing.
    fs::remove( dir.path / "dem_sample.tif" );
    CHECK( runCli( { "--verify", "--out=" + dir.str() } ) == 4 );
}

TEST_CASE( "verifyDirectory flags a manifest edited after generation",
           "[foundry][manifest]" )
{
    TempDir dir;
    Options options;
    options.out_dir = dir.str();
    options.seed = 42;
    GenerateResult result;
    REQUIRE( generate( options, &result ).ok );

    // In-place edit of one manifest field (content untouched): the recorded
    // self_fingerprint no longer matches the edited payload.
    const fs::path manifest_path = dir.path / "manifest.json";
    std::string bytes = readBinary( manifest_path );
    const std::size_t pos = bytes.find( "\"seed\" : 42" ); // jsoncpp "k : v" style
    REQUIRE( pos != std::string::npos );
    bytes.replace( pos, 11, "\"seed\" : 43" );
    {
        std::ofstream out( manifest_path, std::ios::binary | std::ios::trunc );
        out << bytes;
    }
    VerifyReport report;
    REQUIRE( verifyDirectory( dir.str(), &report ).ok );
    REQUIRE( report.problems.size() == 1 );
    CHECK( report.problems[0] ==
           "self_fingerprint mismatch (manifest was edited after generation)" );
}

TEST_CASE( "verifyDirectory detects in-place corruption and unlisted data files",
           "[foundry][manifest]" )
{
    GDALAllRegister();
    TempDir dir;
    Options options;
    options.out_dir = dir.str();
    options.seed = 9;
    GenerateResult result;
    REQUIRE( generate( options, &result ).ok );

    // Size-preserving byte flip: exactly one sha256 problem, no size problem.
    {
        std::fstream f( dir.path / "dem_sample.tif",
                        std::ios::in | std::ios::out | std::ios::binary );
        REQUIRE( f );
        f.seekp( 200 );
        char c = 0;
        REQUIRE( static_cast<std::streamsize>( f.read( &c, 1 ).gcount() ) == 1 );
        f.seekp( 200 );
        f.put( static_cast<char>( c ^ 0xFF ) );
        REQUIRE( f );
    }
    VerifyReport flip;
    REQUIRE( verifyDirectory( dir.str(), &flip ).ok );
    REQUIRE( flip.problems.size() == 1 );
    CHECK( flip.problems[0] == "sha256 drift: dem_sample.tif" );

    // An untracked-looking data file that the manifest does not list.
    {
        std::ofstream stray( dir.path / "stray.tif", std::ios::binary );
        stray << 'x';
    }
    VerifyReport stray_report;
    REQUIRE( verifyDirectory( dir.str(), &stray_report ).ok );
    bool flagged_unlisted = false;
    for ( const std::string &problem : stray_report.problems )
        flagged_unlisted = flagged_unlisted || problem == "unlisted data file: stray.tif";
    CHECK( flagged_unlisted );
}

// ---------------------------------------------------------------------------
// 5. stress profile smoke
// ---------------------------------------------------------------------------

TEST_CASE( "stress profile emits the 2048x2048 grid with closed-form DEM",
           "[foundry][stress]" )
{
    GDALAllRegister();
    TempDir dir;
    Options options;
    options.out_dir = dir.str();
    options.profile = Profile::Stress;
    options.products = { Product::DemSample, Product::DemSlopeTruth };
    GenerateResult result;
    REQUIRE( generate( options, &result ).ok );
    REQUIRE( result.files.size() == 2 );

    GDALDataset *dem = openRaster( dir.path / "dem_sample.tif" );
    checkGrid( dem, 2048, 2048 );
    const float emitted = readFloatPixel( dem, 1024, 1024 );
    const float expected = static_cast<float>( demElevation( 1024.5 / 2048.0, 1024.5 / 2048.0 ) );
    CHECK( emitted == expected );
    GDALClose( dem );

    GDALDataset *slope = openRaster( dir.path / "dem_slope_truth.tif" );
    checkGrid( slope, 2048, 2048 );
    double gx = 0.0;
    double gy = 0.0;
    demGradient( 1024.5 / 2048.0, 1024.5 / 2048.0, &gx, &gy );
    double s = 0.0;
    double a = 0.0;
    slopeAspectDegrees( gx, gy, gridForProfile( Profile::Stress ), &s, &a );
    CHECK( readFloatPixel( slope, 1024, 1024 ) == static_cast<float>( s ) );
    GDALClose( slope );
}

// ---------------------------------------------------------------------------
// lab platform 12.0 — catalog usability, fail-closed stale pruning, atomic
// manifest publish and the cross-run determinism matrix (O2/O5 evidence).
// ---------------------------------------------------------------------------

TEST_CASE( "generate and verify tolerate spaces and Unicode in the output dir",
           "[foundry][cli][paths]" )
{
    GDALAllRegister();
    TempDir parent;
    const std::string spaced = ( parent.path / "class 材 料 2026" ).string();
    Options options;
    options.out_dir = spaced;
    options.products = { Product::DemSample };
    GenerateResult result;
    REQUIRE( generate( options, &result ).ok );
    CHECK( result.files.size() == 1 );
    REQUIRE( fs::exists( fs::path( spaced ) / "manifest.json" ) );

    // The CLI receives the same path as one argv element (no shell escaping
    // involved): generate + verify round-trip through a spaced/Unicode dir.
    const int gen_rc = runCli( { "--out=" + spaced, "--products=dem_sample" } );
    CHECK( gen_rc == 0 );
    const int verify_rc = runCli( { "--verify", "--out=" + spaced } );
    CHECK( verify_rc == 0 );
}

TEST_CASE( "no temp residue after the atomic manifest publish",
           "[foundry][e2e][manifest]" )
{
    GDALAllRegister();
    TempDir dir;
    Options options;
    options.out_dir = dir.str();
    GenerateResult result;
    REQUIRE( generate( options, &result ).ok );
    bool saw_tmp = false;
    for ( const auto &entry : fs::directory_iterator( dir.path ) )
        saw_tmp = saw_tmp || entry.path().extension() == ".tmp";
    CHECK_FALSE( saw_tmp );
    CHECK( fs::exists( dir.path / "manifest.json" ) );
}

TEST_CASE( "stale pruning fails closed when an owned name is not a regular file",
           "[foundry][e2e][stale]" )
{
    GDALAllRegister();
    TempDir dir;
    Options full;
    full.out_dir = dir.str();
    GenerateResult first;
    REQUIRE( generate( full, &first ).ok );

    // Impersonate a stale catalog file with a directory: it cannot be pruned
    // and no regeneration can clear it, so generate must refuse (typed io)
    // instead of silently leaving a future --verify landmine.
    std::error_code ec;
    fs::remove( dir.path / "dem_sample.tif", ec );
    REQUIRE( fs::create_directory( dir.path / "dem_sample.tif", ec ) );

    Options subset;
    subset.out_dir = dir.str();
    subset.products = { Product::LandsatSample };
    GenerateResult second;
    const Outcome outcome = generate( subset, &second );
    CHECK_FALSE( outcome.ok );
    CHECK( outcome.error.category == "io" );
    CHECK( outcome.error.message.find( "not regular" ) != std::string::npos );
    // Refusal is pre-emission: the directory is exactly as it was, nothing
    // pruned, nothing overwritten.
    CHECK( fs::exists( dir.path / "landsat_sample.tif" ) );
    CHECK( fs::exists( dir.path / "change_before.tif" ) );
}

TEST_CASE( "cli --products selects a subset with typed refusals",
           "[foundry][cli][products]" )
{
    TempDir dir;

    // Unknown product and empty ids are usage errors echoing the input.
    CHECK( runCli( { "--out=" + dir.str(), "--products=nope_sample" } ) == 2 );
    CHECK( runCli( { "--out=" + dir.str(), "--products=dem_sample,,change_before" } ) == 2 );
    CHECK( runCli( { "--out=" + dir.str(), "--products=dem_sample," } ) == 2 );
    CHECK( runCli( { "--out=" + dir.str(), "--products=" } ) == 2 );

    // Two selection spellings at once, or selection plus verify: usage.
    CHECK( runCli( { "--out=" + dir.str(), "--products=dem_sample",
                     "--spec=whatever.json" } ) == 2 );
    CHECK( runCli( { "--out=" + dir.str(), "--products=dem_sample", "--verify" } ) == 2 );

    // Happy path: a comma list generates exactly the named products.
    const int rc = runCli( { "--out=" + dir.str(),
                             "--products=dem_sample,change_before" } );
    CHECK( rc == 0 );
    VerifyReport report;
    REQUIRE( verifyDirectory( dir.str(), &report ).ok );
    CHECK( report.problems.empty() );
    CHECK( report.files_checked == 2 );
    CHECK( fs::exists( dir.path / "dem_sample.tif" ) );
    CHECK( fs::exists( dir.path / "change_before.tif" ) );
    CHECK_FALSE( fs::exists( dir.path / "landsat_sample.tif" ) );

    // Subset onto the same dir prunes the unselected product (full<->subset
    // leaves no stale owned files either direction).
    const int rc2 = runCli( { "--out=" + dir.str(), "--products=landsat_sample" } );
    CHECK( rc2 == 0 );
    VerifyReport report2;
    REQUIRE( verifyDirectory( dir.str(), &report2 ).ok );
    CHECK( report2.files_checked == 1 );
    CHECK_FALSE( fs::exists( dir.path / "dem_sample.tif" ) );
}

TEST_CASE( "cli --list-products prints the catalog and refuses combinations",
           "[foundry][cli][products]" )
{
    TempDir dir;
    // runCli hides stdout, so capture via the shell is overkill: assert the
    // contract that matters — exit 0 alone, exit 2 combined with anything.
    CHECK( runCli( { "--list-products" } ) == 0 );
    CHECK( runCli( { "--list-products", "--seed=7" } ) == 2 );
    CHECK( runCli( { "--list-products", "--out=" + dir.str() } ) == 2 );
    CHECK( runCli( { "--list-products", "--verify" } ) == 2 );
}

TEST_CASE( "determinism matrix: same seed twice is byte-identical across "
           "seeds, profiles and subsets",
           "[foundry][e2e][determinism]" )
{
    GDALAllRegister();
    struct Case
    {
        uint32_t seed;
        Profile profile;
        std::vector<Product> products;
        const char *label;
    };
    const std::vector<Case> cases = {
        { 42, Profile::Lab, {}, "full/lab/seed42" },
        { 7, Profile::Lab, { Product::DemSample, Product::ChangeBefore,
                             Product::ChangeAfter },
          "subset/lab/seed7" },
        { 42, Profile::Lab, { Product::LandsatSample, Product::LandsatTruth },
          "optical+truth/lab/seed42" },
        { 123456789u, Profile::Lab, { Product::TrainingSamples },
          "shapefile/lab/seed123456789" },
    };

    for ( const Case &item : cases )
    {
        CAPTURE( item.label );
        TempDir first_dir;
        TempDir second_dir;
        Options options;
        options.out_dir = first_dir.str();
        options.seed = item.seed;
        options.profile = item.profile;
        options.products = item.products;
        GenerateResult first;
        REQUIRE( generate( options, &first ).ok );

        Options again = options;
        again.out_dir = second_dir.str();
        GenerateResult second;
        REQUIRE( generate( again, &second ).ok );

        REQUIRE( first.files.size() == second.files.size() );
        REQUIRE( first.manifest_bytes == second.manifest_bytes );
        for ( std::size_t i = 0; i < first.files.size(); ++i )
        {
            REQUIRE( first.files[i].name == second.files[i].name );
            REQUIRE( first.files[i].sha256 == second.files[i].sha256 );
            CHECK( readBinary( first_dir.path / first.files[i].name ) ==
                   readBinary( second_dir.path / second.files[i].name ) );
        }

        // Both directories verify clean independently.
        for ( const TempDir *dir : { &first_dir, &second_dir } )
        {
            VerifyReport report;
            REQUIRE( verifyDirectory( dir->str(), &report ).ok );
            CHECK( report.problems.empty() );
            CHECK( report.files_checked == first.files.size() );
        }
    }
}

TEST_CASE( "cli two-pass matrix: same seed command twice is byte-identical",
           "[foundry][cli][determinism]" )
{
    TempDir first_dir;
    TempDir second_dir;
    const auto run = []( const std::string &out ) {
        return runCli( { "--out=" + out, "--seed=7",
                         "--products=dem_sample,change_before,change_after" } );
    };
    REQUIRE( run( first_dir.str() ) == 0 );
    REQUIRE( run( second_dir.str() ) == 0 );

    const std::string manifest_a = readBinary( first_dir.path / "manifest.json" );
    const std::string manifest_b = readBinary( second_dir.path / "manifest.json" );
    // manifest.json embeds no path or timestamp; two runs on one host must
    // agree byte for byte.
    CHECK( manifest_a == manifest_b );
    CHECK( readBinary( first_dir.path / "dem_sample.tif" ) ==
           readBinary( second_dir.path / "dem_sample.tif" ) );
    CHECK( readBinary( first_dir.path / "change_before.tif" ) ==
           readBinary( second_dir.path / "change_before.tif" ) );
    CHECK( readBinary( first_dir.path / "change_after.tif" ) ==
           readBinary( second_dir.path / "change_after.tif" ) );
}
