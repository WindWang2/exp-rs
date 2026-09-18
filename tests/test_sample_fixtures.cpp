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
//     (2 usage / 3 spec refusal / 4 verify drift), spec-directory intake,
//     typed spec-refusal prefix on stderr
//   * re-run contract: generate twice into one directory (byte-identical,
//     shapefile sidecars replaced), full<->subset switching (stale owned
//     artifacts pruned, user files untouched, --verify 0 problem),
//     path-with-spaces output, host GDAL env perturbation (PAM / thread
//     count / SHAPE_ENCODING) leaving the bytes and manifest identical
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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
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

/// Unique temp path prefix/suffix shared by TempDir and the stderr-capture
/// helper (ASCII, shell-safe: digits and dashes only).
fs::path uniqueTempPath( const char *prefix )
{
    static std::atomic<int> counter{ 0 };
    const int n = counter.fetch_add( 1 );
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>( getpid() );
#endif
    return fs::temp_directory_path() /
           ( std::string( prefix ) + std::to_string( pid ) + "-" + std::to_string( n ) );
}

struct TempDir
{
    fs::path path;
    TempDir()
    {
        path = uniqueTempPath( "sicnu-foundry-test-" );
        fs::remove_all( path );
        fs::create_directories( path );
    }
    ~TempDir() { std::error_code ec; fs::remove_all( path, ec ); }
    std::string str() const { return path.string(); }
};

/// Set an environment variable for the current process (the CLI children
/// spawned via std::system inherit it) and restore the previous state on
/// destruction, so the perturbation cannot leak into other test cases.
class ScopedEnv
{
  public:
    ScopedEnv( const char *key, const char *value ) : key_( key )
    {
        const char *previous = std::getenv( key );
        if ( previous )
            previous_.emplace( previous );
#ifdef _WIN32
        _putenv( ( key_ + "=" + value ).c_str() );
#else
        ::setenv( key_.c_str(), value, 1 );
#endif
    }
    ScopedEnv( const ScopedEnv & ) = delete;
    ScopedEnv &operator=( const ScopedEnv & ) = delete;
    ~ScopedEnv()
    {
#ifdef _WIN32
        // An empty value removes the variable from the environment.
        _putenv( ( key_ + "=" + previous_.value_or( "" ) ).c_str() );
#else
        if ( previous_ )
            ::setenv( key_.c_str(), previous_->c_str(), 1 );
        else
            ::unsetenv( key_.c_str() );
#endif
    }

  private:
    std::string key_;
    std::optional<std::string> previous_;
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

/// runCli plus stderr capture (shell redirection into a temp file), so the
/// typed refusal prefixes on stderr are assertable, not just the exit code.
int runCliCaptureStderr( const std::vector<std::string> &args, std::string *stderr_out )
{
    const fs::path err_path = uniqueTempPath( "sicnu-foundry-stderr-" );
    std::string cmd = shellQuote( SICNU_GENERATE_SAMPLES_BIN );
    for ( const std::string &arg : args )
        cmd += " " + shellQuote( arg );
#ifdef _WIN32
    cmd += " 2> \"" + err_path.string() + "\"";
#else
    cmd += " 2> '" + err_path.string() + "'";
#endif
    const int status = std::system( cmd.c_str() );
    if ( stderr_out )
        *stderr_out = readBinary( err_path );
    std::error_code ec;
    fs::remove( err_path, ec );
#ifdef _WIN32
    return status;
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

TEST_CASE( "CLI: regenerating twice into the same directory succeeds byte-identical",
           "[foundry][cli][determinism][regenerate]" )
{
    // F-1032-P1-shp regression lock: the second pass must replace the whole
    // foundry-owned set (shapefile sidecars included) without relying on the
    // Shapefile driver's version-dependent behavior on pre-existing files.
    TempDir dir;
    REQUIRE( runCli( { "--out=" + dir.str(), "--seed=42" } ) == 0 );

    std::map<std::string, std::string> first_pass;
    for ( const auto &entry : fs::directory_iterator( dir.path ) )
        first_pass[entry.path().filename().string()] = readBinary( entry.path() );
    REQUIRE( first_pass.size() == 13 ); // 12 emitted files + manifest.json

    REQUIRE( runCli( { "--out=" + dir.str(), "--seed=42" } ) == 0 );

    std::map<std::string, std::string> second_pass;
    for ( const auto &entry : fs::directory_iterator( dir.path ) )
        second_pass[entry.path().filename().string()] = readBinary( entry.path() );
    CHECK( second_pass == first_pass );

    CHECK( runCli( { "--verify", "--out=" + dir.str() } ) == 0 );
}

TEST_CASE( "CLI: full<->subset switching keeps the tree verify-clean and never deletes user files",
           "[foundry][cli][regenerate]" )
{
    // F-1032-P1-stale regression lock: after a selection switch, generate must
    // prune exactly the catalog-owned basenames the new selection does not
    // emit, so the wrapper's forced --verify passes — while any non-foundry
    // file survives untouched. Non-data extensions (.txt/.gpkg) are also not
    // flagged by --verify, so the directory stays 0-problem throughout.
    TempDir dir;
    {
        std::ofstream notes( dir.path / "field_notes.txt" );
        notes << "made by a human";
        std::ofstream boundary( dir.path / "boundary.gpkg" );
        boundary << "also made by a human";
    }

    REQUIRE( runCli( { "--out=" + dir.str(), "--seed=42" } ) == 0 );
    const std::string full_manifest = readBinary( dir.path / "manifest.json" );

    TempDir spec_dir;
    const std::string spec = ( spec_dir.path / "subset.json" ).string();
    {
        std::ofstream out( spec );
        out << "{\"experiment\":\"switch\",\"products\":[\"dem_sample\",\"training_samples\"]}";
    }
    REQUIRE( runCli( { "--out=" + dir.str(), "--seed=42", "--spec=" + spec } ) == 0 );
    CHECK( runCli( { "--verify", "--out=" + dir.str() } ) == 0 );

    // Products outside the selection: every owned basename gone. The subset
    // keeps training_samples, so its sidecars must all still exist.
    for ( const char *name :
          { "landsat_sample.tif", "change_before.tif", "change_after.tif",
            "landsat_truth.tif", "change_truth.tif", "dem_slope_truth.tif",
            "dem_aspect_truth.tif" } )
    {
        INFO( "stale artifact still present: " << name );
        CHECK_FALSE( fs::exists( dir.path / name ) );
    }
    // Selected products: present, with the full owned sidecar set.
    CHECK( fs::exists( dir.path / "dem_sample.tif" ) );
    for ( const char *name : { "training_samples.shp", "training_samples.shx",
                               "training_samples.dbf", "training_samples.prj" } )
    {
        INFO( "selected sidecar missing: " << name );
        CHECK( fs::exists( dir.path / name ) );
    }
    CHECK_FALSE( fs::exists( dir.path / "training_samples.cpg" ) );
    const Json::Value subset_manifest =
      parseJsonBytes( readBinary( dir.path / "manifest.json" ) );
    REQUIRE( subset_manifest["products"].size() == 2 );
    CHECK( subset_manifest["products"][0].asString() == "dem_sample" );
    CHECK( subset_manifest["products"][1].asString() == "training_samples" );

    // Unknown user files survived both the pruning and the overwrite.
    CHECK( readBinary( dir.path / "field_notes.txt" ) == "made by a human" );
    CHECK( readBinary( dir.path / "boundary.gpkg" ) == "also made by a human" );

    // subset -> full: the full set comes back byte-identical to the first run.
    REQUIRE( runCli( { "--out=" + dir.str(), "--seed=42" } ) == 0 );
    CHECK( readBinary( dir.path / "manifest.json" ) == full_manifest );
    CHECK( runCli( { "--verify", "--out=" + dir.str() } ) == 0 );
    CHECK( readBinary( dir.path / "field_notes.txt" ) == "made by a human" );
    CHECK( readBinary( dir.path / "boundary.gpkg" ) == "also made by a human" );
}

TEST_CASE( "CLI: dropping the vector product prunes the whole shapefile sidecar set",
           "[foundry][cli][regenerate]" )
{
    // The full↔subset case above keeps training_samples selected; this one
    // exercises the prune branch for every existing sidecar of a dropped
    // product, including the reserved .cpg name.
    TempDir dir;
    REQUIRE( runCli( { "--out=" + dir.str(), "--seed=42" } ) == 0 );

    TempDir spec_dir;
    const std::string spec = ( spec_dir.path / "no_vector.json" ).string();
    {
        std::ofstream out( spec );
        out << "{\"experiment\":\"no-vector\",\"products\":[\"dem_sample\"]}";
    }
    REQUIRE( runCli( { "--out=" + dir.str(), "--seed=42", "--spec=" + spec } ) == 0 );

    for ( const char *name : { "training_samples.shp", "training_samples.shx",
                               "training_samples.dbf", "training_samples.prj",
                               "training_samples.cpg" } )
    {
        INFO( "dropped-product sidecar still present: " << name );
        CHECK_FALSE( fs::exists( dir.path / name ) );
    }
    CHECK( runCli( { "--verify", "--out=" + dir.str() } ) == 0 );
}

TEST_CASE( "CLI: host GDAL environment cannot perturb the emitted bytes",
           "[foundry][cli][determinism][regenerate]" )
{
    // F-1032-P1-gdalenv regression lock: the emit path pins PAM, thread count
    // and shapefile encoding, so a host-exported GDAL environment cannot add
    // sidecars or change any byte — manifest included (an extra .cpg would
    // otherwise surface as an extra manifest entry).
    TempDir clean_dir;
    REQUIRE( runCli( { "--out=" + clean_dir.str(), "--seed=42" } ) == 0 );

    // Capture the pre-test state: the host may legitimately export these
    // variables, and restoration must be judged against that state.
    const char *prev_threads = std::getenv( "GDAL_NUM_THREADS" );
    const char *prev_pam = std::getenv( "GDAL_PAM_ENABLED" );
    const char *prev_shape = std::getenv( "SHAPE_ENCODING" );

    TempDir noisy_dir;
    int noisy_exit = -1;
    {
        const ScopedEnv threads( "GDAL_NUM_THREADS", "8" );
        const ScopedEnv pam( "GDAL_PAM_ENABLED", "YES" );
        const ScopedEnv shape( "SHAPE_ENCODING", "CP936" );
        noisy_exit = runCli( { "--out=" + noisy_dir.str(), "--seed=42" } );
    }
    REQUIRE( noisy_exit == 0 );

    CHECK( readBinary( noisy_dir.path / "manifest.json" ) ==
           readBinary( clean_dir.path / "manifest.json" ) );
    const Json::Value manifest =
      parseJsonBytes( readBinary( noisy_dir.path / "manifest.json" ) );
    for ( const Json::Value &entry : manifest["files"] )
    {
        const std::string name = entry["name"].asString();
        INFO( "byte drift under perturbed env: " << name );
        CHECK( readBinary( noisy_dir.path / name ) == readBinary( clean_dir.path / name ) );
    }
    // The pinned knobs must not materialize sidecars either.
    CHECK_FALSE( fs::exists( noisy_dir.path / "training_samples.cpg" ) );
    CHECK_FALSE( fs::exists( noisy_dir.path / "landsat_sample.tif.aux.xml" ) );

    // The environment really was restored to its pre-test state (nullptr-safe
    // comparison: a host that exports these variables gets its values back).
    const auto restored = []( const char *current, const char *previous ) {
        if ( ( current == nullptr ) != ( previous == nullptr ) )
            return false;
        return current == nullptr || std::string( current ) == previous;
    };
    CHECK( restored( std::getenv( "GDAL_NUM_THREADS" ), prev_threads ) );
    CHECK( restored( std::getenv( "GDAL_PAM_ENABLED" ), prev_pam ) );
    CHECK( restored( std::getenv( "SHAPE_ENCODING" ), prev_shape ) );
}

TEST_CASE( "CLI: --out= with spaces in the path generates and verifies",
           "[foundry][cli]" )
{
    TempDir base;
    const fs::path spaced = base.path / "out put with spaces" / "deep er";
    REQUIRE( runCli( { "--out=" + spaced.string(), "--seed=42" } ) == 0 );
    CHECK( fs::exists( spaced / "manifest.json" ) );
    CHECK( runCli( { "--verify", "--out=" + spaced.string() } ) == 0 );
}

TEST_CASE( "CLI: spec refusals carry the typed prefix on stderr and cover the empty list",
           "[foundry][cli][spec]" )
{
    // Review test-hole closure: exit 3 alone does not pin the contract; the
    // documented "spec-refusal: [spec]" stderr prefix and the empty-products
    // refusal get dedicated cases here.
    TempDir dir;
    TempDir spec_dir;
    const std::string spec = ( spec_dir.path / "refusal.json" ).string();

    {
        std::ofstream out( spec );
        out << "{\"experiment\":\"x\",\"products\":[]}";
    }
    std::string err;
    CHECK( runCliCaptureStderr( { "--out=" + dir.str(), "--spec=" + spec }, &err ) == 3 );
    CHECK( err.rfind( "spec-refusal: [spec]", 0 ) == 0 );
    CHECK( err.find( "non-empty array" ) != std::string::npos );

    {
        std::ofstream out( spec );
        out << "{\"experiment\":\"x\",\"products\":[\"not_a_product\"]}";
    }
    CHECK( runCliCaptureStderr( { "--out=" + dir.str(), "--spec=" + spec }, &err ) == 3 );
    CHECK( err.rfind( "spec-refusal: [spec]", 0 ) == 0 );
    CHECK( err.find( "not_a_product" ) != std::string::npos );
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
