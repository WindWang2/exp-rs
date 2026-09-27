/***************************************************************************
 * test_verification_env_12.cpp — Platform 12.0 · Oracle O-10
 *
 * The verification platform must not be able to report a FALSE RED.
 *
 * WHY THIS EXISTS (evidence-anchored, see EVIDENCE.md E-0):
 *
 *   During the 12.0 baseline measurement, six of eight inherited 11.0 suites
 *   were reported RED. Five of them were not broken at all — the runner had
 *   no PROJ_DATA, so the vcpkg GDAL/PROJ build could not find proj.db, and
 *   every CRS-touching operator threw:
 *
 *     InvalidParameter: input raster carries no CRS;
 *                       declare srcCrsOverride to clip anyway
 *     InvalidParameter: warpRaster: option construction failed
 *
 *   Those exceptions are INDISTINGUISHABLE from real product defects. A
 *   verification platform that cannot tell "the product is wrong" from
 *   "the runner is unprovisioned" is not trustworthy — the same way a
 *   platform that silently passes is not trustworthy.
 *
 * Two aggravating traps found while diagnosing:
 *   1. A Unix-style PROJ_DATA (/c/deps/...) fails IDENTICALLY to no
 *      PROJ_DATA: the value is consumed by the native PROJ 9 library, which
 *      needs C:\deps\... . Equal symptoms for different causes.
 *   2. Deriving PROJ_DATA/GDAL_DATA from a bash $(pwd) made
 *      QTemporaryDir::isValid() return false in EVERY case, producing a
 *      third, unrelated whole-suite false-failure signature.
 *
 * DESIGN NOTES
 *  - This gate deliberately FAILS (it does not skip) when data is missing.
 *    A skip is a false green; a legible hard failure is the honest outcome.
 *  - It asserts the *provisioning*, not a particular absolute path, so it
 *    stays portable: it asks GDAL and PROJ whether they can construct a
 *    well-known CRS, which is the capability the lanes actually need.
 ***************************************************************************/
#include <catch2/catch_test_macros.hpp>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_srs_api.h>
#include <proj.h>

#include <QDir>
#include <QTemporaryDir>
#include <QString>

#include <cstdlib>
#include <string>

namespace {

/// The EPSG code every raster lane depends on: WGS 84.
constexpr int kWgs84 = 4326;

std::string envOrEmpty( const char *name )
{
    const char *value = std::getenv( name );
    return value ? std::string( value ) : std::string();
}

/// True when `path` looks like a native Windows absolute path. PROJ 9 on
/// Windows cannot resolve an MSYS path such as /c/deps/... — it reports the
/// same "cannot find proj.db" as an unset variable, which is the trap this
/// helper exists to make explicit.
bool looksNativeWindowsPath( const std::string &path )
{
    if ( path.size() >= 2 && path[1] == ':' )
        return true;
    return false;
}

} // namespace

TEST_CASE( "GDAL and PROJ resolve their data without environment tricks",
           "[env12][o10]" )
{
    // Report the provisioning state BEFORE asserting, so a failure names the
    // cause instead of being an opaque boolean.
    const std::string projData = envOrEmpty( "PROJ_DATA" );
    const std::string projLib = envOrEmpty( "PROJ_LIB" );
    const std::string gdalData = envOrEmpty( "GDAL_DATA" );
    UNSCOPED_INFO( "PROJ_DATA = " << ( projData.empty() ? "<unset>" : projData ) );
    UNSCOPED_INFO( "PROJ_LIB  = " << ( projLib.empty() ? "<unset>" : projLib ) );
    UNSCOPED_INFO( "GDAL_DATA = " << ( gdalData.empty() ? "<unset>" : gdalData ) );

    const std::string effectiveProj = !projData.empty() ? projData : projLib;
    if ( !effectiveProj.empty() )
    {
        // Catch trap #1 explicitly: a Unix-style path silently behaves like
        // an unset variable, which is worse than an obvious mistake.
        INFO( "PROJ data path: " << effectiveProj );
        CHECK( looksNativeWindowsPath( effectiveProj ) );
        CHECK( QDir( QString::fromStdString( effectiveProj ) ).exists() );
    }

    // The real oracle: can PROJ construct WGS 84 from its database?
    // This is exactly what io:clip / io:warp / the numeric-reference lane do.
    PJ_CONTEXT *ctx = proj_context_create();
    REQUIRE( ctx != nullptr );
    PJ *crs = proj_create( ctx, "EPSG:4326" );
    if ( crs == nullptr )
    {
        // Surface the library's own diagnostic verbatim — this is the line a
        // reader needs to see when the gate fires.
        UNSCOPED_INFO( "proj_create(EPSG:4326) returned null; "
                       "this is the unprovisioned-data signature. Set PROJ_DATA "
                       "to a WINDOWS-style path containing proj.db + proj.ini." );
    }
    CHECK( crs != nullptr );
    if ( crs )
        proj_destroy( crs );
    proj_context_destroy( ctx );
}

TEST_CASE( "GDAL can construct the WGS 84 spatial reference",
           "[env12][o10]" )
{
    // A second, independent probe through a different library path. PROJ can
    // resolve while GDAL's own SRS layer is misconfigured (e.g. GDAL_DATA
    // missing datum-shift grids), and several lanes call both.
    GDALAllRegister();

    OGRSpatialReference srs;
    const OGRErr err = srs.importFromEPSG( kWgs84 );
    INFO( "OGRSpatialReference::importFromEPSG(4326) returned " << err );
    CHECK( err == OGRERR_NONE );

    if ( err == OGRERR_NONE )
        CHECK( srs.IsProjected() == 0 ); // 4326 is geographic
}

TEST_CASE( "a truncated/zero-byte raster is not silently accepted as readable",
           "[env12][o10]" )
{
    // Companion half of "no false red": the platform must also refuse to
    // treat an unreadable artifact as success. This pins the degenerate case
    // the truth-of-success lane (O-1) relies on: GDAL must FAIL to open a
    // byte-truncated file rather than return a dataset with plausible-looking
    // dimensions.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const QString truncated = dir.filePath( QStringLiteral( "truncated.tif" ) );
    {
        // A valid-enough TIFF header followed by nothing: GDAL must not
        // present this as a usable raster.
        QFile f( truncated );
        REQUIRE( f.open( QIODevice::WriteOnly ) );
        const char header[8] = { 'I', 'I', 42, 0, 8, 0, 0, 0 };
        f.write( header, sizeof( header ) );
        f.close();
    }

    GDALDatasetH ds = GDALOpen( truncated.toUtf8().constData(), GA_ReadOnly );
    INFO( "GDALOpen on a truncated TIFF returned "
          << ( ds ? "a dataset (BAD: false-success risk)" : "null (correct)" ) );
    // Either GDAL refuses outright, or — if the driver is lenient — the
    // dataset must not claim usable dimensions. Both outcomes are honest;
    // "open succeeded with non-zero size" is not.
    if ( ds )
    {
        const int w = GDALGetRasterXSize( ds );
        const int h = GDALGetRasterYSize( ds );
        INFO( "truncated raster reported size " << w << "x" << h );
        CHECK( ( w == 0 || h == 0 ) );
        GDALClose( ds );
    }
}
