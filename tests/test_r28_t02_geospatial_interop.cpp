// test_r28_t02_geospatial_interop.cpp — R28-T02 WP07: geospatial
// interoperability facts that must survive a write→reopen round trip through
// the canonical reader/writer contracts, plus the #1579 literal-percent
// filename pin and typed-refusal checks for unknown grid/CRS facts.
//
// Fixtures are small REAL GeoTIFF files created through the production
// writer (no mocks): the round trip must preserve NoData, scale/offset and
// band roles — and must refuse (typed) what it cannot state, never
// defaulting an unknown into a spatial fact.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "geospatial/crs/crs_policy.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_writer.h"
#include "geospatial/util/resource_uri.h"

#include <QFileInfo>
#include <QTemporaryDir>

#include <string>

using namespace sicnu::geo;
using Catch::Approx;

namespace
{
QString writeBandRaster( const QTemporaryDir &dir, const QString &name, bool withNoData,
                         double nodata, const std::string &role )
{
    const QString path = dir.filePath( name );
    RasterBandSpec spec;
    spec.dtype = "Float32";
    spec.hasNoData = withNoData;
    spec.noDataValue = nodata;
    spec.role = role;
    RasterWriteOptions options;
    options.overwrite = true;
    RasterWriter writer = RasterWriter::create( path.toStdString(), 8, 8, { spec }, options );
    writer.finalize();
    return path;
}
} // namespace

TEST_CASE( "raster round trip preserves NoData, scale/offset and band role",
           "[io][r28t02][roundtrip]" )
{
    QTemporaryDir dir;
    RasterBandSpec spec;
    spec.dtype = "Float32";
    spec.hasNoData = true;
    spec.noDataValue = -9999.0;
    spec.hasScale = true;
    spec.scale = 0.0001;
    spec.hasOffset = true;
    spec.offset = -1000.0;
    spec.role = "reflectance";
    RasterWriteOptions options;
    options.overwrite = true;
    const QString path = dir.filePath( "roles.tif" );
    RasterWriter writer = RasterWriter::create( path.toStdString(), 8, 8, { spec }, options );
    writer.finalize();

    const RasterMetadata meta = inspectRaster( path.toStdString() );
    CHECK( meta.width == 8 );
    CHECK( meta.height == 8 );
    REQUIRE( meta.bands.size() == 1 );
    // NoData / scale / offset / role survive the reopen — "absence flags are
    // explicit, undeclared is not zero": a lost offset here silently biases
    // every ratio index (the #1740 family).
    CHECK( meta.bands[0].hasNoData );
    CHECK( meta.bands[0].noDataValue == Approx( -9999.0 ) );
    CHECK( meta.bands[0].hasScale );
    CHECK( meta.bands[0].scale == Approx( 0.0001 ) );
    CHECK( meta.bands[0].offset == Approx( -1000.0 ) );
    CHECK( meta.bands[0].role == "reflectance" );
}

TEST_CASE( "resolveAgainst accepts literal-percent filenames and still refuses %2e escapes",
           "[io][r28t02][uri][1579]" )
{
    const std::string base = "/data/products/safe1";
    // A literal '%' in a legal filename is NOT a traversal (#1579: the old
    // code refused these with a misleading "'..' traversal" reason).
    const ResourceUri literal = ResourceUri::resolveAgainst( base, "cloud_50%.tif" );
    CHECK( literal.kind != ResourceKind::Invalid );
    CHECK( literal.path == "/data/products/safe1/cloud_50%.tif" );
    const ResourceUri malformed = ResourceUri::resolveAgainst( base, "file%zz/scene.tif" );
    CHECK( malformed.kind != ResourceKind::Invalid );
    CHECK( malformed.path == "/data/products/safe1/file%zz/scene.tif" );
    // The REAL traversal protections are intact: encoded and literal dotdot
    // both refuse, and an embedded NUL refuses.
    CHECK( ResourceUri::resolveAgainst( base, "%2e%2e/outside.tif" ).kind
           == ResourceKind::Invalid );
    CHECK( ResourceUri::resolveAgainst( base, "../outside.tif" ).kind
           == ResourceKind::Invalid );
    CHECK( ResourceUri::resolveAgainst( base, "a/%00/x.tif" ).kind
           == ResourceKind::Invalid );
}

TEST_CASE( "UTF-8 and space paths round trip through the raster contract",
           "[io][r28t02][roundtrip]" )
{
    QTemporaryDir dir;
    // Chinese + spaces + a literal percent: legal on every supported
    // platform and common in CN EO product trees; the writer/reader must not
    // transliterate or percent-mangle the path the catalog records.
    const QString path = writeBandRaster( dir, QStringLiteral( "影像 200%_波段.tif" ), false,
                                          0.0, "" );
    CHECK( QFileInfo::exists( path ) );

    const RasterMetadata meta = inspectRaster( path.toStdString() );
    CHECK( meta.width == 8 );

    // The URI vocabulary keeps the path spellable and displayable.
    const ResourceUri uri = ResourceUri::parse( path.toStdString() );
    CHECK( uri.kind != ResourceKind::Invalid );
    CHECK( uri.display().find( "影像" ) != std::string::npos );
    CHECK( uri.display().find( "200%" ) != std::string::npos );
}

TEST_CASE( "unknown CRS/grid facts refuse typed, never default to legal",
           "[io][r28t02][crs]" )
{
    QTemporaryDir dir;
    // A raster with NO CRS: canonical metadata must SAY so (invalid CrsInfo,
    // empty authid) rather than inventing EPSG:4326.
    const QString path = writeBandRaster( dir, "nocrs.tif", false, 0.0, "" );
    const RasterMetadata meta = inspectRaster( path.toStdString() );
    CHECK( !meta.crs.valid );
    CHECK( meta.crs.authid.empty() );
    CHECK( !meta.hasGeotransform );

    // CRS policy: resolving an unknown authid through the dataset-CRS
    // resolver is a typed refusal, not a silent WGS84 fallback.
    CrsInfo bogus;
    bogus.valid = true; // the info itself claims validity…
    bogus.authid = "EPSG:99999999"; // …but the code does not exist
    bool refused = false;
    try
    {
        const CrsPolicy policy; // default policy: no assumed CRS
        ( void )resolveDatasetCrs( bogus, policy );
    }
    catch ( const GeoError &error )
    {
        refused = true;
        CHECK( ( error.code() == ErrorCode::InvalidCrs
                 || error.code() == ErrorCode::InvalidArgument ) );
        CHECK( std::string( error.what() ).find( "99999999" ) != std::string::npos );
    }
    CHECK( refused );
}
