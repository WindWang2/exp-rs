// test_semantic_drift_9.cpp — Scientific Algorithms 9.0 (M1): mechanical
// drift guards for the numeric/raster semantics contracts established with
// the M0 defect fixes (#848/#853/#854/#855/#856/#873). Each check binds a
// documented contract to an anchor in the owned sources so the failure mode
// that produced the defect cannot silently return. These are drift guards,
// not behavior tests — the behavior itself is pinned by
// test_scientific_defects_9.cpp and the domain suites.

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QString>

#include <string>

namespace
{

std::string readSource( const char *relPath )
{
    const QString path = QString( "%1/%2" ).arg( CMAKE_SOURCE_DIR, relPath );
    QFile f( path );
    const bool opened = f.open( QIODevice::ReadOnly );
    INFO( "cannot open tracked source: " << relPath );
    REQUIRE( opened );
    return f.readAll().toStdString();
}

} // namespace

TEST_CASE( "Gradient kernels normalize each axis by its own spacing (#855)",
           "[drift][sar][anisotropic]" )
{
    const std::string sar = readSource( "src/processing/algorithms/sar/sar_terrain.cpp" );
    // The anisotropic seam is the two-parameter form; the single averaged
    // scalar must not return.
    REQUIRE( sar.find( "cellSizeXMeters" ) != std::string::npos );
    REQUIRE( sar.find( "cellSizeYMeters" ) != std::string::npos );
    REQUIRE( sar.find( "0.5 * ( cellX + cellY )" ) == std::string::npos );
    REQUIRE( sar.find( "0.5 * (cellX + cellY)" ) == std::string::npos );
}

TEST_CASE( "D8 direction decoding validates range before float→int casts (#853)",
           "[drift][terrain][ub]" )
{
    const std::string flow = readSource( "src/processing/algorithms/terrain_flow.cpp" );
    // Every cast site is guarded through the shared magnitude gate.
    REQUIRE( flow.find( "isCastableFlowCode" ) != std::string::npos );
    REQUIRE( flow.find( "isCastableFlowCode( d )" ) != std::string::npos );
    REQUIRE( flow.find( "isCastableFlowCode( dir[nIdx] )" ) != std::string::npos );
}

TEST_CASE( "Priority-flood fill seeds the NoData-adjacent boundary (#848)",
           "[drift][terrain][nodata]" )
{
    const std::string flow = readSource( "src/processing/algorithms/terrain_flow.cpp" );
    REQUIRE( flow.find( "bordersNoData" ) != std::string::npos );
}

TEST_CASE( "Typed multi-band SAR outputs declare per-band NoData (#854)",
           "[drift][sar][nodata]" )
{
    // The two SAR terrain operators write Float32 + Byte-mask outputs; the
    // blanket dataset-wide setter cannot express the Byte sentinel and must
    // stay out of exactly these files (new typed multi-band writers should
    // use setBandNoDataValue and be added here).
    for ( const char *op :
          { "src/operators/rs/rs_sar_terrain_flatten_operator.cpp",
            "src/operators/rs/rs_sar_terrain_correction_operator.cpp" } )
    {
        const std::string src = readSource( op );
        INFO( "operator: " << op );
        REQUIRE( src.find( "setNoDataValue(" ) == std::string::npos );
        REQUIRE( src.find( "setBandNoDataValue(" ) != std::string::npos );
    }
}

TEST_CASE( "Spectral scale probing carries no hardcoded sentinels (#856)",
           "[drift][spectral][scale]" )
{
    const std::string op =
        readSource( "src/operators/rs/rs_spectral_index_operator.cpp" );
    // The removed #856 dead block invented -9999/65535 sentinel guesses;
    // sentinel semantics live in nodata_utils (declared metadata) only.
    REQUIRE( op.find( "65535" ) == std::string::npos );
    REQUIRE( op.find( "9999" ) == std::string::npos );
}

TEST_CASE( "Declared numeric scales are finite-validated (#873)",
           "[drift][contracts][scale]" )
{
    const std::string contracts =
        readSource( "src/processing/contracts/scientific_contracts.cpp" );
    // The acceptance predicate must mention finiteness; the behavior test
    // (test_scientific_defects_9) pins +Inf/NaN refusals.
    const size_t fnPos = contracts.find( "domainFromDeclaredScale" );
    REQUIRE( fnPos != std::string::npos );
    const size_t finitePos = contracts.find( "std::isfinite", fnPos );
    REQUIRE( finitePos != std::string::npos );
}
