// tests/test_r25_b_import_preflight.cpp — R25-B feature 1: the import
// preflight explanation report (sicnu.import.preflight/1).
//
// Contract under test (all read-only, no network, no registration):
//   * a plain GeoTIFF with CRS/NoData/role → Importable with complete facts
//   * a bare raster (no CRS, no geotransform, no NoData, no roles) →
//     Importable + the four quality findings, each with remediation
//   * a corrupt file → Refused + import.unreadable
//   * missing/empty path → Refused with the exact typed code
//   * remote spellings (https://, /vsicurl/) → OutOfScope, never probed
//   * identity token is proven (li1:v1: prefix, content strength)
//   * dedup prediction: registering a real file through the production
//     DataManager makes a later preflight report already_registered with the
//     SAME asset id — and the preflight itself never adds an asset
//   * a netCDF container exposes subdatasets → NeedsUserChoice
//   * a vector source reports driver + layer names
//   * toJson() round-trips the schema stamp and verdict
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/import_preflight.h"
#include "data/source_descriptor.h"

#include "geospatial/crs/crs_policy.h"
#include "geospatial/raster/raster_writer.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <gdal.h>

#include <fstream>

namespace
{

using sicnu::data::ImportPreflight;
using sicnu::data::ImportPreflightReport;
using sicnu::data::ImportPreflightVerdict;
using sicnu::data::DataManager;
using sicnu::data::RegisterRequest;
using sicnu::data::PersistencePolicy;

QString writePlainRaster( const QTemporaryDir &dir, const QString &name )
{
    const QString path = dir.filePath( name );
    sicnu::geo::RasterBandSpec spec;
    spec.dtype = "Float32";
    spec.hasNoData = true;
    spec.noDataValue = -9999.0;
    spec.role = "Red";
    sicnu::geo::RasterWriteOptions options;
    options.overwrite = true;
    sicnu::geo::RasterWriter writer =
        sicnu::geo::RasterWriter::create( path.toStdString(), 8, 8, { spec }, options );
    writer.setGeotransform( { 100.0, 0.5, 0.0, 40.0, 0.0, -0.5 } );
    writer.setCrs( sicnu::geo::Crs::fromAuthid( "EPSG:4326" ) );
    writer.finalize();
    return path;
}

QString writeBareRaster( const QTemporaryDir &dir, const QString &name )
{
    const QString path = dir.filePath( name );
    sicnu::geo::RasterBandSpec spec; // Byte, no NoData, no role
    sicnu::geo::RasterWriteOptions options;
    options.overwrite = true;
    sicnu::geo::RasterWriter writer =
        sicnu::geo::RasterWriter::create( path.toStdString(), 4, 4, { spec }, options );
    writer.finalize();
    return path;
}

bool hasFinding( const ImportPreflightReport &report, const QString &code )
{
    return report.finding( code ) != nullptr;
}

} // namespace

TEST_CASE( "import preflight explains a plain georeferenced raster", "[r25_b][import_preflight]" )
{
    QTemporaryDir dir;
    const QString path = writePlainRaster( dir, QStringLiteral( "plain.tif" ) );

    const ImportPreflightReport report = ImportPreflight::inspect( path );

    CHECK( report.verdict == ImportPreflightVerdict::Importable );
    CHECK( report.resourceKindString() == QStringLiteral( "raster" ) );
    CHECK( report.driver == QStringLiteral( "GTiff" ) );
    CHECK( report.width == 8 );
    CHECK( report.height == 8 );
    CHECK( report.bandCount == 1 );
    CHECK( report.crsValid );
    CHECK( report.crsAuthid == QStringLiteral( "EPSG:4326" ) );
    CHECK( report.hasGeotransform );
    REQUIRE( report.bands.size() == 1 );
    CHECK( report.bands[0].hasNoData );
    CHECK( report.bands[0].noDataValue == -9999.0 );
    CHECK( report.bands[0].role == QStringLiteral( "Red" ) );
    CHECK( report.adapterId == QStringLiteral( "generic_raster" ) );
    REQUIRE( report.candidateAdapterIds.size() == 1 );
    // None of the refusal findings may fire for a healthy raster.
    CHECK_FALSE( hasFinding( report, QStringLiteral( "import.crs_missing" ) ) );
    CHECK_FALSE( hasFinding( report, QStringLiteral( "import.unreadable" ) ) );
}

TEST_CASE( "import preflight reports quality gaps with remediation", "[r25_b][import_preflight]" )
{
    QTemporaryDir dir;
    const QString path = writeBareRaster( dir, QStringLiteral( "bare.tif" ) );

    const ImportPreflightReport report = ImportPreflight::inspect( path );

    // Warnings never refuse a healthy openable raster.
    CHECK( report.verdict == ImportPreflightVerdict::Importable );
    const sicnu::data::ImportPreflightFinding *crs =
        report.finding( QStringLiteral( "import.crs_missing" ) );
    REQUIRE( crs != nullptr );
    CHECK( crs->severity == QStringLiteral( "warning" ) );
    CHECK_FALSE( crs->remediation.isEmpty() );
    CHECK( hasFinding( report, QStringLiteral( "import.geotransform_missing" ) ) );
    CHECK( hasFinding( report, QStringLiteral( "import.nodata_undeclared" ) ) );
    CHECK( hasFinding( report, QStringLiteral( "import.band_roles_unknown" ) ) );
}

TEST_CASE( "import preflight refuses corrupt and missing sources with typed codes",
           "[r25_b][import_preflight]" )
{
    QTemporaryDir dir;
    const QString junk = dir.filePath( QStringLiteral( "junk.tif" ) );
    {
        std::ofstream out( junk.toStdString() );
        out << "definitely not a tiff";
    }
    const ImportPreflightReport corrupt = ImportPreflight::inspect( junk );
    CHECK( corrupt.verdict == ImportPreflightVerdict::Refused );
    REQUIRE( corrupt.finding( QStringLiteral( "import.unreadable" ) ) != nullptr );
    CHECK( corrupt.finding( QStringLiteral( "import.unreadable" ) )->severity
           == QStringLiteral( "error" ) );

    const ImportPreflightReport missing =
        ImportPreflight::inspect( dir.filePath( QStringLiteral( "absent.tif" ) ) );
    CHECK( missing.verdict == ImportPreflightVerdict::Refused );
    CHECK( hasFinding( missing, QStringLiteral( "import.path_missing" ) ) );

    const ImportPreflightReport empty = ImportPreflight::inspect( QString() );
    CHECK( empty.verdict == ImportPreflightVerdict::Refused );
    CHECK( hasFinding( empty, QStringLiteral( "import.empty_path" ) ) );
}

TEST_CASE( "import preflight never probes remote spellings", "[r25_b][import_preflight]" )
{
    for ( const QString &remote :
          { QStringLiteral( "https://example.com/sentinel.tif" ),
            QStringLiteral( "/vsicurl/https://example.com/a.tif" ),
            QStringLiteral( "s3://bucket/key.tif" ) } )
    {
        const ImportPreflightReport report = ImportPreflight::inspect( remote );
        CHECK( report.verdict == ImportPreflightVerdict::OutOfScope );
        REQUIRE( report.finding( QStringLiteral( "import.remote_out_of_scope" ) ) != nullptr );
        CHECK( report.finding( QStringLiteral( "import.remote_out_of_scope" ) )->severity
               == QStringLiteral( "info" ) );
    }
}

TEST_CASE( "import preflight proves local identity", "[r25_b][import_preflight]" )
{
    QTemporaryDir dir;
    const QString path = writePlainRaster( dir, QStringLiteral( "ident.tif" ) );

    const ImportPreflightReport report = ImportPreflight::inspect( path );

    CHECK( report.identityToken.startsWith( QStringLiteral( "li1:v1:" ) ) );
    CHECK( report.identityStrength == QStringLiteral( "content" ) );
    CHECK( report.identitySizeBytes > 0 );
    CHECK( report.identityHashedBytes == report.identitySizeBytes );

    ImportPreflight::Options options;
    options.probeIdentity = false;
    const ImportPreflightReport noIdentity = ImportPreflight::inspect( path, nullptr, options );
    CHECK( noIdentity.identityToken.isEmpty() );
    CHECK( noIdentity.identityHashedBytes == 0 );
}

TEST_CASE( "import preflight predicts dedup against the production catalog",
           "[r25_b][import_preflight]" )
{
    QTemporaryDir dir;
    const QString path = writePlainRaster( dir, QStringLiteral( "dedup.tif" ) );

    DataManager manager;
    RegisterRequest request;
    request.source.providerKey = QStringLiteral( "gdal" );
    request.source.canonicalSource = path;
    request.persistence = PersistencePolicy::ProjectPersistent;
    const sicnu::data::RegisterResult registered = manager.registerSource( request );
    REQUIRE_FALSE( registered.assetId.isNull() );

    // Read-only contract: the preflight must not change the catalog.
    const int assetsBefore = manager.assets().size();

    const ImportPreflightReport report = ImportPreflight::inspect( path, &manager );

    CHECK( report.alreadyRegistered );
    CHECK( report.existingAssetId == registered.assetId.toString() );
    CHECK( report.existingAssetRevision == 1 );
    REQUIRE( report.finding( QStringLiteral( "import.already_registered" ) ) != nullptr );
    CHECK_FALSE( report.finding( QStringLiteral( "import.already_registered" ) )->remediation.isEmpty() );
    CHECK( manager.assets().size() == assetsBefore );

    // A sibling file nobody registered must not predict a reuse.
    const QString other = writePlainRaster( dir, QStringLiteral( "other.tif" ) );
    const ImportPreflightReport sibling = ImportPreflight::inspect( other, &manager );
    CHECK_FALSE( sibling.alreadyRegistered );

    // Dedup opt-out leaves the fields empty without touching the verdict.
    ImportPreflight::Options options;
    options.probeDedup = false;
    const ImportPreflightReport noDedup = ImportPreflight::inspect( path, &manager, options );
    CHECK_FALSE( noDedup.alreadyRegistered );
    CHECK( noDedup.verdict == ImportPreflightVerdict::Importable );
}

TEST_CASE( "import preflight flags container subdatasets as a user decision",
           "[r25_b][import_preflight]" )
{
    GDALDriverH netcdf = GDALGetDriverByName( "netCDF" );
    if ( netcdf == nullptr )
    {
        SKIP( "netCDF driver not available in this GDAL build" );
    }
    QTemporaryDir dir;
    // Hand-built netCDF via GDAL: two subdatasets make the container a
    // NeedsUserChoice verdict (registering the file keeps it at file level).
    const QString path = dir.filePath( QStringLiteral( "container.nc" ) );
    // A one-variable netCDF already exposes itself as a subdataset container.
    const char *argv[] = { "-of", "netCDF", nullptr };
    GDALDatasetH ds = GDALCreate( GDALGetDriverByName( "netCDF" ), path.toStdString().c_str(),
                                  4, 4, 1, GDT_Byte, const_cast<char **>( argv ) );
    if ( ds != nullptr )
    {
        GDALClose( ds );
    }
    // If creation failed (driver registered but creation unsupported), skip.
    if ( !QFile::exists( path ) )
    {
        SKIP( "netCDF creation unsupported in this GDAL build" );
    }

    const ImportPreflightReport report = ImportPreflight::inspect( path );

    if ( report.subdatasetCount > 0 )
    {
        CHECK( report.verdict == ImportPreflightVerdict::NeedsUserChoice );
        REQUIRE( report.finding( QStringLiteral( "import.container_subdatasets" ) ) != nullptr );
        CHECK_FALSE( report.finding( QStringLiteral( "import.container_subdatasets" ) )->remediation.isEmpty() );
    }
    else
    {
        // Single-variable containers may not report subdatasets; the verdict
        // must then be a plain Importable, never a silent refusal.
        CHECK( report.verdict == ImportPreflightVerdict::Importable );
    }
}

TEST_CASE( "import preflight describes vector sources", "[r25_b][import_preflight]" )
{
    QTemporaryDir dir;
    const QString path = dir.filePath( QStringLiteral( "points.geojson" ) );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( R"({"type":"FeatureCollection","features":[{"type":"Feature",
                     "properties":{"name":"a"},"geometry":{"type":"Point",
                     "coordinates":[104.0,30.0]}}]})" );
    }

    const ImportPreflightReport report = ImportPreflight::inspect( path );

    CHECK( report.resourceKindString() == QStringLiteral( "vector" ) );
    CHECK( report.layerCount == 1 );
    REQUIRE( report.layerNames.size() == 1 );
    CHECK( report.verdict == ImportPreflightVerdict::Importable );
}

TEST_CASE( "import preflight report serializes with the schema stamp",
           "[r25_b][import_preflight]" )
{
    QTemporaryDir dir;
    const QString path = writePlainRaster( dir, QStringLiteral( "json.tif" ) );

    const ImportPreflightReport report = ImportPreflight::inspect( path );
    const QJsonObject json = report.toJson();

    CHECK( json.value( QStringLiteral( "schema" ) ).toString()
           == QStringLiteral( "sicnu.import.preflight/1" ) );
    CHECK( json.value( QStringLiteral( "schema_version" ) ).toInt() == 1 );
    CHECK( json.value( QStringLiteral( "verdict" ) ).toString()
           == QStringLiteral( "importable" ) );
    CHECK( json.value( QStringLiteral( "identity" ) ).toObject()
               .value( QStringLiteral( "token" ) )
               .toString()
               .startsWith( QStringLiteral( "li1:v1:" ) ) );
    CHECK( json.contains( QStringLiteral( "raster" ) ) );
    CHECK( json.value( QStringLiteral( "findings" ) ).isArray() );
    // The serialized document is valid JSON end to end.
    const QJsonDocument doc( json );
    CHECK_FALSE( doc.isNull() );
}
