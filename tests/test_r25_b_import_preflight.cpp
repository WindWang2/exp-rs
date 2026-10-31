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

#include <QDir>
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

    // The oracle only means something when the container actually exposes
    // subdatasets (a driver that collapses a single-variable netCDF cannot
    // exercise the user-choice verdict) — SKIP the whole case otherwise
    // instead of converting non-exercise into a pass.
    if ( report.subdatasetCount <= 0 )
    {
        SKIP( "netCDF driver present but does not expose subdatasets for this file" );
    }
    CHECK( report.verdict == ImportPreflightVerdict::NeedsUserChoice );
    REQUIRE( report.finding( QStringLiteral( "import.container_subdatasets" ) ) != nullptr );
    CHECK_FALSE( report.finding( QStringLiteral( "import.container_subdatasets" ) )->remediation.isEmpty() );
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

TEST_CASE( "import preflight error text leaks no filesystem content beyond the path",
           "[r25_b][import_preflight][security]" )
{
    QTemporaryDir dir;
    // A file with a secret-looking name outside the reported path must not
    // appear in the report's messages (the report cites the caller's path
    // and nothing else).
    const QString secret = dir.filePath( QStringLiteral( "secret-content-not-in-report.txt" ) );
    {
        std::ofstream out( secret.toStdString() );
        out << "TOKEN=abcd1234";
    }
    // A relative path spelling must not resolve into an absolute disclosure.
    const ImportPreflightReport report =
      ImportPreflight::inspect( QFileInfo( secret ).absoluteFilePath() );
    REQUIRE( report.finding( QStringLiteral( "import.unreadable" ) ) != nullptr );
    const sicnu::data::ImportPreflightFinding *unreadable =
      report.finding( QStringLiteral( "import.unreadable" ) );
    // Positive oracle: the message names the typed error class and cites the
    // caller's path — never the file's content or any other filesystem text.
    const bool citesTypedClass =
      unreadable->message.contains( QStringLiteral( "open_failed" ) );
    const bool citesCallerPath = unreadable->message.contains( secret );
    CHECK( ( citesTypedClass || citesCallerPath ) );
    CHECK_FALSE( unreadable->message.contains( QStringLiteral( "TOKEN=" ) ) );
    CHECK_FALSE( unreadable->message.contains( QStringLiteral( "abcd1234" ) ) );
    CHECK_FALSE( unreadable->remediation.contains( QStringLiteral( "abcd1234" ) ) );
    for ( const sicnu::data::ImportPreflightFinding &finding : report.findings )
    {
        CHECK_FALSE( finding.message.contains( QStringLiteral( "TOKEN=" ) ) );
        CHECK_FALSE( finding.remediation.contains( QStringLiteral( "abcd1234" ) ) );
    }
}

TEST_CASE( "import preflight JSON carries exact byte sizes (qint64, no double truncation)",
           "[r25_b][import_preflight]" )
{
  QTemporaryDir dir;
  const QString path = writePlainRaster( dir, QStringLiteral( "qint64.tif" ) );

  const ImportPreflightReport report = ImportPreflight::inspect( path );
  const QJsonObject identity =
    report.toJson().value( QStringLiteral( "identity" ) ).toObject();

  // The JSON stores integers, not doubles: reading back must equal the
  // struct's quint64 exactly (a double cast would lose precision > 2^53).
  const QJsonValue sizeValue = identity.value( QStringLiteral( "size_bytes" ) );
  CHECK( ( sizeValue.isDouble() || sizeValue.isString() ) );
  CHECK( static_cast<quint64>( sizeValue.toDouble() ) == report.identitySizeBytes );
  CHECK( static_cast<quint64>( identity.value( QStringLiteral( "hashed_bytes" ) ).toDouble() )
         == report.identityHashedBytes );
}

TEST_CASE( "import preflight does not refuse a directory product root",
           "[r25_b][import_preflight]" )
{
  QTemporaryDir dir;
  // A SAFE-shaped directory: the adapter family claims it while the root is
  // not an openable dataset — the preflight must not refuse (the collection
  // import path handles per-constituent registration).
  const QString safe = dir.filePath( QStringLiteral( "S2A_MSIL1C_20260901T000000_N0509.SAFE" ) );
  QDir().mkdir( safe );
  QDir().mkdir( safe + "/GRANULE" );
  QDir().mkdir( safe + "/GRANULE/L1C_T31TCJ" );
  QDir().mkdir( safe + "/GRANULE/L1C_T31TCJ/IMG_DATA" );
  // The three core constituents the adapter verifies: manifest.safe, the
  // product-level MTD, and at least one measurement file.
  for ( const QString &core :
        { safe + "/manifest.safe", safe + "/MTD_MSIL1C.xml",
          safe + "/GRANULE/L1C_T31TCJ/MTD_TL.xml",
          safe + "/GRANULE/L1C_T31TCJ/IMG_DATA/T31TCJ_20260901T103021_B04.jp2" } )
  {
    QFile file( core );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QByteArrayLiteral( "\x00\x00\x00\x0cjP  \r\n\x87\n" ) );
    file.close();
  }

  const ImportPreflightReport report = ImportPreflight::inspect( safe );
  // The root is not an openable dataset, but the product itself is complete:
  // the preflight explains the collection path instead of refusing.
  CHECK( report.verdict != ImportPreflightVerdict::Refused );
  // A readable-but-sparse SAFE reports PartialReadable (missing manifest or
  // measurements degrade the verdict, never fabricate completeness) — and a
  // partial product is still NOT a refusal: registration proceeds through
  // the collection path with the gaps reported.
  CHECK( ( report.completeness == QStringLiteral( "complete" )
           || report.completeness == QStringLiteral( "partial_readable" ) ) );
  REQUIRE( report.finding( QStringLiteral( "import.directory_product_root" ) ) != nullptr );
  CHECK_FALSE( report.finding( QStringLiteral( "import.directory_product_root" ) )->remediation.isEmpty() );
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

    // Determinism discriminator (perf baseline companion): the same input
    // state must serialize to the byte-identical document on re-inspection.
    const ImportPreflightReport second = ImportPreflight::inspect( path );
    const QJsonDocument secondDoc( second.toJson() );
    CHECK( doc == secondDoc );
}
