// tests/test_sar_platform10.cpp — Advanced SAR / PolSAR / InSAR 10.0
// operator E2E: full chain through the live operator registry on synthetic
// fixtures with exact expected products.
//
//   PolSAR    rs:sar_polsar_decompose (pauli exact; h_alpha on canonical
//             regions; dual-pol refusal)
//   InSAR     rs:sar_interferogram → rs:sar_phase_filter → rs:sar_unwrap →
//             rs:sar_displacement closed-form closure on a synthetic SLC
//             pair, plus grid/complex/provider refusals
//   Temporal  rs:sar_temporal_events date semantics (S-1 close) on scenes
//             with declared acquisition times
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <json/json.h>

#include <gdal.h>

#include <cmath>
#include <complex>
#include <limits>
#include <string>
#include <vector>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/algorithms/sar/sar_complex.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_temporal_events.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

using namespace sicnu::operators;
using Catch::Approx;

namespace
{

int &appArgc()
{
    static int argc = 1;
    return argc;
}
char appArgv0[] = "test_sar_platform10";
char *appArgv[] = { appArgv0, nullptr };

struct AppInit
{
    AppInit()
    {
        if ( !QCoreApplication::instance() )
            new QCoreApplication( appArgc(), appArgv );
    }
};

constexpr int kWidth = 24;
constexpr int kHeight = 24;

using cfloat = std::complex<float>;

/// Writes a CFloat32 GTiff. @a fill(band, x, y) produces each sample.
template <typename F>
bool writeComplex( const QString &path, int width, int height, int bands, F fill )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, bands,
                                  GDT_CFloat32, nullptr );
    if ( !ds )
        return false;
    bool ok = true;
    for ( int b = 1; b <= bands && ok; ++b )
    {
        std::vector<cfloat> values( static_cast<size_t>( width ) * height );
        for ( int y = 0; y < height && ok; ++y )
            for ( int x = 0; x < width && ok; ++x )
                values[static_cast<size_t>( y ) * width + x] = fill( b, x, y );
        ok = GDALRasterIO( GDALGetRasterBand( ds, b ), GF_Write, 0, 0, width, height,
                           values.data(), width, height, GDT_CFloat32, 0, 0 ) == CE_None;
    }
    GDALClose( ds );
    return ok;
}

bool writeFloatScene( const QString &path, int width, int height,
                      const std::vector<float> &values, const char *acquisitionUtc = nullptr,
                      bool declareDb = false )
{
    ensureGdalInit();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return false;
    if ( acquisitionUtc )
        GDALSetMetadataItem( ds, sicnu::sar::kAcquisitionUtcKey, acquisitionUtc, nullptr );
    if ( declareDb )
        GDALSetMetadataItem( ds, sicnu::sar::kDomainKey, "db", nullptr );
    const bool ok = GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, width, height,
                                  const_cast<float *>( values.data() ), width, height,
                                  GDT_Float32, 0, 0 ) == CE_None;
    GDALClose( ds );
    return ok;
}

std::vector<float> readFloatBand( const QString &path, int band )
{
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
        return {};
    std::vector<float> values( static_cast<size_t>( ds.width() ) * ds.height() );
    if ( !ds.readBandData( band, values.data(), ds.width(), ds.height() ) )
        return {};
    return values;
}

/// Declares SICNU_SAR_COMPLEX_CHANNELS on an existing GTiff (update mode).
bool declareChannels( const QString &path, const QString &decl )
{
    ensureGdalInit();
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
    if ( !ds )
        return false;
    GDALSetMetadataItem( ds, sicnu::sar::kComplexChannelsKey,
                         decl.toUtf8().constData(), nullptr );
    GDALClose( ds );
    return true;
}

Json::Value runOperator( const std::string &id, const Json::Value &params )
{
    auto op = RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    RSOperatorContext ctx;
    return op->run( params, ctx );
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// PolSAR
// ---------------------------------------------------------------------------

TEST_CASE( "rs:sar_polsar_decompose — exact Pauli powers on canonical regions",
           "[sar][polsar][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Left half = sphere (SHH = SVV = 1), right half = dihedral (SVV = −1).
    const QString input = tmp.filePath( "polsar.tif" );
    REQUIRE( writeComplex( input, kWidth, kHeight, 3,
                           []( int band, int x, int ) {
                               const cfloat svv = ( x < kWidth / 2 ) ? cfloat( 1, 0 )
                                                                     : cfloat( -1, 0 );
                               switch ( band )
                               {
                                   case 1: return cfloat( 1, 0 ); // HH
                                   case 2: return cfloat( 0, 0 ); // HV
                                   default: return svv;           // VV
                               }
                           } ) );
    REQUIRE( declareChannels( input, QStringLiteral( "HH;HV;VV" ) ) );

    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = tmp.filePath( "pauli.tif" ).toStdString();
    params["decomposition"] = "pauli";
    params["windowSize"] = 1;
    Json::Value result;
    REQUIRE_NOTHROW( result = runOperator( "rs:sar_polsar_decompose", params ) );
    REQUIRE( result["bands"].asString() == "odd_bounce;double_bounce;volume;span" );
    REQUIRE( result["reciprocityAssumed"].asBool() == false );

    const auto odd = readFloatBand( tmp.filePath( "pauli.tif" ), 1 );
    const auto dbl = readFloatBand( tmp.filePath( "pauli.tif" ), 2 );
    const auto vol = readFloatBand( tmp.filePath( "pauli.tif" ), 3 );
    const auto span = readFloatBand( tmp.filePath( "pauli.tif" ), 4 );
    REQUIRE( odd.size() == static_cast<size_t>( kWidth ) * kHeight );

    for ( int y = 0; y < kHeight; ++y )
        for ( int x = 0; x < kWidth; ++x )
        {
            const size_t i = static_cast<size_t>( y ) * kWidth + x;
            const bool sphere = x < kWidth / 2;
            REQUIRE( odd[i] == Approx( sphere ? 2.0f : 0.0f ).margin( 1e-5 ) );
            REQUIRE( dbl[i] == Approx( sphere ? 0.0f : 2.0f ).margin( 1e-5 ) );
            REQUIRE( vol[i] == 0.0f );
            REQUIRE( span[i] == Approx( 2.0f ).margin( 1e-5 ) );
        }
}

TEST_CASE( "rs:sar_polsar_decompose — h_alpha on a sphere field", "[sar][polsar][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Pure sphere field (SHH = SVV = 1, SHV = 0): single scattering
    // mechanism → H ≈ 0, α ≈ 0 even with a window ensemble (rank-1 stays
    // rank-1 under averaging).
    const QString input = tmp.filePath( "sphere.tif" );
    REQUIRE( writeComplex( input, kWidth, kHeight, 3,
                           []( int band, int, int ) {
                               return band == 2 ? cfloat( 0, 0 ) : cfloat( 1, 0 );
                           } ) );

    Json::Value params( Json::objectValue );
    params["input"] = input.toStdString();
    params["output"] = tmp.filePath( "ha.tif" ).toStdString();
    params["decomposition"] = "h_alpha";
    params["windowSize"] = 3;
    params["hhBand"] = 1;
    params["hvBand"] = 2;
    params["vvBand"] = 3;
    Json::Value result;
    REQUIRE_NOTHROW( result = runOperator( "rs:sar_polsar_decompose", params ) );
    REQUIRE( result["channelSource"].asString() == "parameters" );

    const auto entropy = readFloatBand( tmp.filePath( "ha.tif" ), 1 );
    const auto alpha = readFloatBand( tmp.filePath( "ha.tif" ), 3 );
    const auto lambda1 = readFloatBand( tmp.filePath( "ha.tif" ), 5 );
    for ( int i = 0; i < 16; ++i )
    {
        REQUIRE( entropy[i] == Approx( 0.0f ).margin( 1e-4 ) );
        REQUIRE( alpha[i] == Approx( 0.0f ).margin( 1e-3 ) );
        // λ1 = mean T11 over the window = |SHH+SVV|²/2 = 2.
        REQUIRE( lambda1[i] == Approx( 2.0f ).margin( 1e-4 ) );
    }
}

TEST_CASE( "rs:sar_polsar_decompose refuses detected and undeclared inputs",
           "[sar][polsar][refusal]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString detected = tmp.filePath( "detected.tif" );
    REQUIRE( writeFloatScene( detected, 8, 8, std::vector<float>( 64, 1.0f ) ) );

    Json::Value params( Json::objectValue );
    params["input"] = detected.toStdString();
    params["output"] = tmp.filePath( "out.tif" ).toStdString();
    params["decomposition"] = "pauli";
    params["hhBand"] = 1;
    params["hvBand"] = 1;
    params["vvBand"] = 1;

    bool threw = false;
    try
    {
        runOperator( "rs:sar_polsar_decompose", params );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        REQUIRE( std::string( e.what() ).find( "COMPLEX_BANDS_REQUIRED" )
                 != std::string::npos );
    }
    REQUIRE( threw );

    // Complex input without any channel declaration → POLARIZATION_MISMATCH.
    const QString undeclared = tmp.filePath( "undeclared.tif" );
    REQUIRE( writeComplex( undeclared, 8, 8, 2, []( int, int, int ) { return cfloat( 1, 0 ); } ) );
    params["input"] = undeclared.toStdString();
    params["hvBand"] = 2;
    threw = false;
    try
    {
        runOperator( "rs:sar_polsar_decompose", params );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        // hh=1, hv=2, vv=1 duplicates band 1 → the duplicate check fires.
        REQUIRE( ( std::string( e.what() ).find( "COMPLEX_BANDS_REQUIRED" )
                       != std::string::npos
                   || std::string( e.what() ).find( "POLARIZATION_MISMATCH" )
                          != std::string::npos ) );
    }
    REQUIRE( threw );
}

// ---------------------------------------------------------------------------
// InSAR chain
// ---------------------------------------------------------------------------

namespace insar_fixture
{

constexpr double kPhaseSlope = 0.15; // rad/px (< π between neighbors)
constexpr double kSlaveOffset = 1.0; // constant slave phase offset (rad)

/// Master: e^{i·slope·x}; slave: e^{i·(slope·x + offset)}.
bool writePair( const QString &masterPath, const QString &slavePath, int width, int height )
{
    auto masterField = []( int, int x, int ) {
        return cfloat( std::cos( kPhaseSlope * x ), std::sin( kPhaseSlope * x ) );
    };
    auto slaveField = []( int, int x, int ) {
        const double phase = kPhaseSlope * x + kSlaveOffset;
        return cfloat( std::cos( phase ), std::sin( phase ) );
    };
    return writeComplex( masterPath, width, height, 1, masterField )
           && writeComplex( slavePath, width, height, 1, slaveField );
}

} // namespace insar_fixture

TEST_CASE( "InSAR chain closes on a synthetic pair", "[sar][insar][e2e]" )
{
    using namespace insar_fixture;
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString master = tmp.filePath( "master.tif" );
    const QString slave = tmp.filePath( "slave.tif" );
    REQUIRE( writePair( master, slave, kWidth, kHeight ) );

    // 1. Interferogram + coherence.
    Json::Value ifgParams( Json::objectValue );
    ifgParams["master"] = master.toStdString();
    ifgParams["slave"] = slave.toStdString();
    ifgParams["output"] = tmp.filePath( "ifg.tif" ).toStdString();
    ifgParams["coherenceOutput"] = tmp.filePath( "coh.tif" ).toStdString();
    ifgParams["coherenceWindow"] = 5;
    Json::Value ifgResult;
    REQUIRE_NOTHROW( ifgResult = runOperator( "rs:sar_interferogram", ifgParams ) );
    REQUIRE( ifgResult["ramp"].asString() == "none" );

    // Interferometric phase must be exactly the constant slave offset
    // (wrapped): master·conj(slave) carries phase −offset everywhere.
    {
        GdalDatasetWrapper ds;
        REQUIRE( ds.open( tmp.filePath( "ifg.tif" ) ) );
        std::vector<cfloat> plane( static_cast<size_t>( kWidth ) * kHeight );
        REQUIRE( ds.readBandDataNative( 1, plane.data(), kWidth, kHeight ) );
        for ( int i = 0; i < 16; ++i )
        {
            REQUIRE( std::arg( plane[i] ) == Approx( -kSlaveOffset ).margin( 1e-5 ) );
            REQUIRE( std::abs( plane[i] ) == Approx( 1.0 ).margin( 1e-5 ) );
        }
    }
    // Identical-magnitude scenes → coherence exactly 1 everywhere.
    {
        const auto coh = readFloatBand( tmp.filePath( "coh.tif" ), 1 );
        REQUIRE( coh.size() == static_cast<size_t>( kWidth ) * kHeight );
        for ( int i = 0; i < 16; ++i )
            REQUIRE( coh[i] == Approx( 1.0f ).margin( 1e-5 ) );
    }

    // 2. Phase filter (constant phase is invariant).
    Json::Value filterParams( Json::objectValue );
    filterParams["input"] = tmp.filePath( "ifg.tif" ).toStdString();
    filterParams["output"] = tmp.filePath( "filtered.tif" ).toStdString();
    filterParams["window"] = 5;
    REQUIRE_NOTHROW( runOperator( "rs:sar_phase_filter", filterParams ) );
    {
        GdalDatasetWrapper ds;
        REQUIRE( ds.open( tmp.filePath( "filtered.tif" ) ) );
        std::vector<cfloat> plane( static_cast<size_t>( kWidth ) * kHeight );
        REQUIRE( ds.readBandDataNative( 1, plane.data(), kWidth, kHeight ) );
        for ( int i = 0; i < 16; ++i )
        {
            REQUIRE( std::abs( plane[i] ) == Approx( 1.0 ).margin( 1e-5 ) );
            REQUIRE( std::arg( plane[i] ) == Approx( -kSlaveOffset ).margin( 1e-4 ) );
        }
    }

    // 3. Unwrap.
    Json::Value unwrapParams( Json::objectValue );
    unwrapParams["input"] = tmp.filePath( "filtered.tif" ).toStdString();
    unwrapParams["output"] = tmp.filePath( "unwrapped.tif" ).toStdString();
    Json::Value unwrapResult;
    REQUIRE_NOTHROW( unwrapResult = runOperator( "rs:sar_unwrap", unwrapParams ) );
    REQUIRE( unwrapResult["unwrappedPixels"].asInt64() == kWidth * kHeight );
    {
        const auto unwrapped = readFloatBand( tmp.filePath( "unwrapped.tif" ), 1 );
        for ( int i = 0; i < 16; ++i )
            REQUIRE( unwrapped[i] == Approx( -static_cast<float>( kSlaveOffset ) ).margin( 1e-4 ) );
    }

    // 4. Displacement: d = −λ·φ/(4π) with λ = 5.6 cm and φ = −1 rad.
    Json::Value dispParams( Json::objectValue );
    dispParams["input"] = tmp.filePath( "unwrapped.tif" ).toStdString();
    dispParams["output"] = tmp.filePath( "disp.tif" ).toStdString();
    dispParams["wavelengthUm"] = 56000.0; // 5.6 cm
    Json::Value dispResult;
    REQUIRE_NOTHROW( dispResult = runOperator( "rs:sar_displacement", dispParams ) );
    REQUIRE( dispResult["wrappedSuspicionWarning"].asBool() == false );
    const double expected = 0.056 / ( 4.0 * M_PI ); // −λ·(−1)/(4π) > 0
    {
        const auto disp = readFloatBand( tmp.filePath( "disp.tif" ), 1 );
        for ( int i = 0; i < 16; ++i )
            REQUIRE( disp[i] == Approx( expected ).margin( 1e-7 ) );
    }
}

TEST_CASE( "InSAR refusals — grid, complex domain, provider, wavelength",
           "[sar][insar][refusal]" )
{
    using namespace insar_fixture;
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString master = tmp.filePath( "master.tif" );
    const QString slave = tmp.filePath( "slave.tif" );
    REQUIRE( writePair( master, slave, kWidth, kHeight ) );

    auto expectError = []( const std::string &id, const Json::Value &params,
                           const std::string &needle ) {
        bool threw = false;
        try
        {
            runOperator( id, params );
        }
        catch ( const RSOperatorError &e )
        {
            threw = true;
            INFO( e.what() );
            REQUIRE( std::string( e.what() ).find( needle ) != std::string::npos );
        }
        REQUIRE( threw );
    };

    SECTION( "dimension mismatch" )
    {
        const QString small = tmp.filePath( "small.tif" );
        REQUIRE( writeComplex( small, 8, 8, 1, []( int, int, int ) { return cfloat( 1, 0 ); } ) );
        Json::Value p;
        p["master"] = master.toStdString();
        p["slave"] = small.toStdString();
        p["output"] = tmp.filePath( "x.tif" ).toStdString();
        expectError( "rs:sar_interferogram", p, "MISMATCH" );
    }

    SECTION( "detected (float) slave refused" )
    {
        const QString detected = tmp.filePath( "detected.tif" );
        REQUIRE( writeFloatScene( detected, kWidth, kHeight,
                                  std::vector<float>( kWidth * kHeight, 1.0f ) ) );
        Json::Value p;
        p["master"] = master.toStdString();
        p["slave"] = detected.toStdString();
        p["output"] = tmp.filePath( "x.tif" ).toStdString();
        expectError( "rs:sar_interferogram", p, "COMPLEX_BANDS_REQUIRED" );
    }

    SECTION( "unknown unwrap provider refused (never substituted)" )
    {
        Json::Value p;
        p["input"] = tmp.filePath( "filtered.tif" ).toStdString();
        p["output"] = tmp.filePath( "u.tif" ).toStdString();
        p["provider"] = "snaphu";
        expectError( "rs:sar_unwrap", p, "UNWRAP_PROVIDER_UNAVAILABLE" );
    }

    SECTION( "missing wavelength refused" )
    {
        Json::Value p;
        p["input"] = master.toStdString(); // complex, but any raster works as "phase"
        p["output"] = tmp.filePath( "d.tif" ).toStdString();
        expectError( "rs:sar_displacement", p, "wavelength" );
    }
}

// ---------------------------------------------------------------------------
// Multi-temporal events
// ---------------------------------------------------------------------------

TEST_CASE( "rs:sar_temporal_events dates the event in calendar time (S-1 close)",
           "[sar][temporal-events][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Three scenes: [1, 1, 10] linear power; acquisitions 12 days apart.
    const QString a = tmp.filePath( "e1.tif" );
    const QString b = tmp.filePath( "e2.tif" );
    const QString c = tmp.filePath( "e3.tif" );
    REQUIRE( writeFloatScene( a, 4, 4, std::vector<float>( 16, 1.0f ),
                              /*acquisitionUtc=*/"2026-01-01" ) );
    REQUIRE( writeFloatScene( b, 4, 4, std::vector<float>( 16, 1.0f ),
                              /*acquisitionUtc=*/"2026-01-13" ) );
    REQUIRE( writeFloatScene( c, 4, 4, std::vector<float>( 16, 10.0f ),
                              /*acquisitionUtc=*/"2026-01-25" ) );

    Json::Value params( Json::objectValue );
    Json::Value inputs( Json::arrayValue );
    inputs.append( a.toStdString() );
    inputs.append( b.toStdString() );
    inputs.append( c.toStdString() );
    params["inputs"] = inputs;
    params["output"] = tmp.filePath( "events.tif" ).toStdString();
    params["changeThresholdDb"] = 6.0;
    Json::Value result;
    REQUIRE_NOTHROW( result = runOperator( "rs:sar_temporal_events", params ) );

    // Date semantics live in the result: scene 2 = 2026-01-25 = day 24.
    REQUIRE( result["dates"].size() == 3 );
    REQUIRE( result["dates"][0].asString() == "2026-01-01" );
    REQUIRE( result["dates"][2].asString() == "2026-01-25" );

    const auto eventFlag = readFloatBand( tmp.filePath( "events.tif" ), 1 );
    const auto firstIndex = readFloatBand( tmp.filePath( "events.tif" ), 3 );
    const auto firstDays = readFloatBand( tmp.filePath( "events.tif" ), 5 );
    const auto maxDev = readFloatBand( tmp.filePath( "events.tif" ), 7 );
    const auto validCount = readFloatBand( tmp.filePath( "events.tif" ), 9 );
    for ( int i = 0; i < 16; ++i )
    {
        REQUIRE( eventFlag[i] == 1.0f );
        REQUIRE( firstIndex[i] == 2.0f );
        REQUIRE( firstDays[i] == Approx( 24.0f ).margin( 1e-4 ) );
        REQUIRE( maxDev[i] == Approx( 10.0f ).margin( 1e-4 ) );
        REQUIRE( validCount[i] == 3.0f );
    }
}

TEST_CASE( "rs:sar_temporal_events refuses missing acquisition dates (no index-only "
           "products)",
           "[sar][temporal-events][refusal]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString a = tmp.filePath( "n1.tif" );
    const QString b = tmp.filePath( "n2.tif" );
    REQUIRE( writeFloatScene( a, 4, 4, std::vector<float>( 16, 1.0f ) ) );
    REQUIRE( writeFloatScene( b, 4, 4, std::vector<float>( 16, 2.0f ) ) );

    Json::Value params( Json::objectValue );
    Json::Value inputs( Json::arrayValue );
    inputs.append( a.toStdString() );
    inputs.append( b.toStdString() );
    params["inputs"] = inputs;
    params["output"] = tmp.filePath( "no-events.tif" ).toStdString();

    bool threw = false;
    try
    {
        runOperator( "rs:sar_temporal_events", params );
    }
    catch ( const RSOperatorError &e )
    {
        threw = true;
        REQUIRE( std::string( e.what() ).find( "ACQUISITION_DATES_MISSING" )
                 != std::string::npos );
    }
    REQUIRE( threw );
}

TEST_CASE( "rs:sar_temporal_stats echoes dates when declared (additive, bands unchanged)",
           "[sar][temporal-events]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const QString a = tmp.filePath( "s1.tif" );
    const QString b = tmp.filePath( "s2.tif" );
    REQUIRE( writeFloatScene( a, 4, 4, std::vector<float>( 16, 1.0f ), "2026-01-01" ) );
    REQUIRE( writeFloatScene( b, 4, 4, std::vector<float>( 16, 2.0f ), "2026-01-13" ) );

    Json::Value params( Json::objectValue );
    Json::Value inputs( Json::arrayValue );
    inputs.append( a.toStdString() );
    inputs.append( b.toStdString() );
    params["inputs"] = inputs;
    params["output"] = tmp.filePath( "stats.tif" ).toStdString();
    Json::Value result;
    REQUIRE_NOTHROW( result = runOperator( "rs:sar_temporal_stats", params ) );
    REQUIRE( result["dates"].size() == 2 );
    REQUIRE( result["dates"][1].asString() == "2026-01-13" );
    REQUIRE( result["timeSemantics"].asString().find( "0-based" ) != std::string::npos );
    // Fixed band contract unchanged: 11 bands.
    REQUIRE( result["bands"].asInt() == 11 );
}

// ---------------------------------------------------------------------------
// Coregistration
// ---------------------------------------------------------------------------

TEST_CASE( "rs:sar_coregister estimates and applies a synthetic global shift",
           "[sar][insar][coregister][e2e]" )
{
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    const int w = 32;
    const int h = 32;
    const double slope = 0.2;
    auto field = [&]( int band, int x, int y ) -> cfloat {
        (void)band;
        const unsigned lcg = ( 1103515245u * static_cast<unsigned>( y * w + x + 1 ) + 12345u );
        const double amp = 0.75 + 0.5 * ( ( lcg >> 16 ) % 1024 ) / 1024.0;
        const double phase = slope * x + 0.1 * y;
        return cfloat( static_cast<float>( amp * std::cos( phase ) ),
                       static_cast<float>( amp * std::sin( phase ) ) );
    };

    const QString master = tmp.filePath( "m.tif" );
    const QString slave = tmp.filePath( "s.tif" );
    REQUIRE( writeComplex( master, w, h, 1, field ) );
    // Slave = master shifted by (dx, dy) = (2, −1): slave(x, y) = master(x−2, y+1).
    REQUIRE( writeComplex( slave, w, h, 1,
                           [&]( int band, int x, int y ) -> cfloat {
                               const int sx = x - 2;
                               const int sy = y + 1;
                               if ( sx < 0 || sx >= w || sy < 0 || sy >= h )
                                   return cfloat( 0, 0 );
                               return field( band, sx, sy );
                           } ) );

    Json::Value params( Json::objectValue );
    params["master"] = master.toStdString();
    params["slave"] = slave.toStdString();
    params["output"] = tmp.filePath( "coreg.tif" ).toStdString();
    params["searchRadius"] = 6;
    Json::Value result;
    REQUIRE_NOTHROW( result = runOperator( "rs:sar_coregister", params ) );
    REQUIRE( result["dx"].asDouble() == Approx( 2.0 ).margin( 0.3 ) );
    REQUIRE( result["dy"].asDouble() == Approx( -1.0 ).margin( 0.3 ) );
    REQUIRE( result["confidentPatches"].asInt64() >= 3 );

    // The resampled slave must now agree with the master in the interior.
    GdalDatasetWrapper mds;
    REQUIRE( mds.open( master ) );
    std::vector<cfloat> mplane( static_cast<size_t>( w ) * h );
    REQUIRE( mds.readBandDataNative( 1, mplane.data(), w, h ) );
    GdalDatasetWrapper sds;
    REQUIRE( sds.open( tmp.filePath( "coreg.tif" ) ) );
    std::vector<cfloat> splane( static_cast<size_t>( w ) * h );
    REQUIRE( sds.readBandDataNative( 1, splane.data(), w, h ) );
    for ( int y = 4; y < h - 4; y += 3 )
        for ( int x = 4; x < w - 4; x += 3 )
        {
            const size_t i = static_cast<size_t>( y ) * w + x;
            REQUIRE( std::arg( splane[i] ) == Approx( std::arg( mplane[i] ) ).margin( 1e-3 ) );
        }
}

TEST_CASE( "rs:sar_interferogram flattenRamp keeps amplitude and removes a known ramp",
           "[sar][insar][e2e][ramp]" )
{
    using namespace insar_fixture;
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Master |s1| = 2 with phase 0.1x; slave |s2| = 3 with the same phase
    // slope PLUS a known linear ramp r(x, y) = 0.01x + 0.002y (|r| < π, so
    // the wrapped field still satisfies the robust linear fit exactly).
    const QString master = tmp.filePath( "rm.tif" );
    const QString slave = tmp.filePath( "rs.tif" );
    auto mField = []( int, int x, int y ) {
        const double phase = 0.1 * x + 0.02 * y;
        return cfloat( static_cast<float>( 2.0 * std::cos( phase ) ),
                       static_cast<float>( 2.0 * std::sin( phase ) ) );
    };
    auto sField = []( int, int x, int y ) {
        const double ramp = 0.01 * x + 0.002 * y;
        const double phase = 0.1 * x + 0.02 * y + ramp;
        return cfloat( static_cast<float>( 3.0 * std::cos( phase ) ),
                       static_cast<float>( 3.0 * std::sin( phase ) ) );
    };
    REQUIRE( writeComplex( master, kWidth, kHeight, 1, mField ) );
    REQUIRE( writeComplex( slave, kWidth, kHeight, 1, sField ) );

    Json::Value params( Json::objectValue );
    params["master"] = master.toStdString();
    params["slave"] = slave.toStdString();
    params["output"] = tmp.filePath( "ramp_ifg.tif" ).toStdString();
    params["coherenceOutput"] = tmp.filePath( "ramp_coh.tif" ).toStdString();
    params["flattenRamp"] = "linear";
    Json::Value result;
    REQUIRE_NOTHROW( result = runOperator( "rs:sar_interferogram", params ) );
    REQUIRE( result["ramp"].asString() == "linear" );
    REQUIRE( result["rampCoefficients"].size() == 3 );

    GdalDatasetWrapper ds;
    REQUIRE( ds.open( tmp.filePath( "ramp_ifg.tif" ) ) );
    std::vector<cfloat> plane( static_cast<size_t>( kWidth ) * kHeight );
    REQUIRE( ds.readBandDataNative( 1, plane.data(), kWidth, kHeight ) );
    for ( int y = 2; y < kHeight - 2; y += 5 )
        for ( int x = 2; x < kWidth - 2; x += 5 )
        {
            const cfloat v = plane[static_cast<size_t>( y ) * kWidth + x];
            // Amplitude contract: exactly |s1|·|s2| = 6 (no double-counted
            // slave term).
            REQUIRE( std::abs( v ) == Approx( 6.0 ).margin( 1e-3 ) );
            // Phase contract: the known ramp was removed (fit is exact on a
            // linear ramp) — residual = phase noise of the closed form, 0.
            REQUIRE( std::arg( v ) == Approx( 0.0 ).margin( 1e-6 ) );
        }
}

// ---------------------------------------------------------------------------
// Scale / bounded execution (synthetic, no heavyweight fixture)
// ---------------------------------------------------------------------------

TEST_CASE( "rs:sar_interferogram streams a 256×256 SLC pair with full valid coverage",
           "[sar][insar][scale]" )
{
    using namespace insar_fixture;
    const AppInit app;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    constexpr int kSide = 256;
    const QString master = tmp.filePath( "big_master.tif" );
    const QString slave = tmp.filePath( "big_slave.tif" );
    REQUIRE( writePair( master, slave, kSide, kSide ) );

    Json::Value params( Json::objectValue );
    params["master"] = master.toStdString();
    params["slave"] = slave.toStdString();
    params["output"] = tmp.filePath( "big_ifg.tif" ).toStdString();
    params["coherenceOutput"] = tmp.filePath( "big_coh.tif" ).toStdString();
    params["coherenceWindow"] = 5;
    Json::Value result;
    REQUIRE_NOTHROW( result = runOperator( "rs:sar_interferogram", params ) );
    REQUIRE( result["validPixels"].asInt64()
             == static_cast<Json::Int64>( kSide ) * kSide );
    REQUIRE( result["validCoherencePixels"].asInt64()
             == static_cast<Json::Int64>( kSide ) * kSide );

    // Spot-check the product phase at interior pixels.
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( tmp.filePath( "big_ifg.tif" ) ) );
    std::vector<cfloat> row( static_cast<size_t>( kSide ) );
    for ( int y : { 1, 128, 254 } )
    {
        REQUIRE( ds.readBandWindowNative( 1, 0, y, kSide, 1, row.data() ) );
        for ( int x : { 1, 128, 254 } )
            REQUIRE( std::arg( row[static_cast<size_t>( x )] )
                     == Approx( -kSlaveOffset ).margin( 1e-5 ) );
    }
}
