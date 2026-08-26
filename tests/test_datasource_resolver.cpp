// tests/test_datasource_resolver.cpp
// QgsDataSourceResolver classification contract tests (GH #560 unified resolver)
#include <catch2/catch_test_macros.hpp>

#include "core/qgsdatasourceresolver.h"

#include <QString>

TEST_CASE( "QgsDataSourceResolver classifies local file paths as LocalFile", "[core][datasource_resolver]" )
{
    // Relative, absolute, and dot-relative local paths -> LocalFile
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "data/a.tif" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "./relative.tif" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/tmp/a.tif" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/home/user/data.tif" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "a.tif" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "../up.tif" ) ) == QgsDataSourceKind::LocalFile );
    // Fallback / unknown scheme stays LocalFile
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "some_random_string" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/not/vsi/prefix.tif" ) ) == QgsDataSourceKind::LocalFile );
}

TEST_CASE( "QgsDataSourceResolver classifies GDAL virtual paths", "[core][datasource_resolver]" )
{
    // Canonical VSI prefixes
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/vsimem/x.tif" ) ) == QgsDataSourceKind::GdalVirtualPath );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/vsizip/a.zip/b.tif" ) ) == QgsDataSourceKind::GdalVirtualPath );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/vsicurl/https://example.com/x.tif" ) ) == QgsDataSourceKind::GdalVirtualPath );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/vsis3/bucket/key.tif" ) ) == QgsDataSourceKind::GdalVirtualPath );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/vsigs/bucket/key.tif" ) ) == QgsDataSourceKind::GdalVirtualPath );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/vsiaz/container/blob.tif" ) ) == QgsDataSourceKind::GdalVirtualPath );
    // Generic /vsi prefix also counts
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/vsicurl_streaming/https://example.com/x" ) ) == QgsDataSourceKind::GdalVirtualPath );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/vsimem/test_vector_inspect.geojson" ) ) == QgsDataSourceKind::GdalVirtualPath );
}

TEST_CASE( "QgsDataSourceResolver classification is case-insensitive for VSI", "[core][datasource_resolver]" )
{
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/VSIMEM/X.TIF" ) ) == QgsDataSourceKind::GdalVirtualPath );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/VsImEm/foo.tif" ) ) == QgsDataSourceKind::GdalVirtualPath );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/VSIZIP/a.zip/b.tif" ) ) == QgsDataSourceKind::GdalVirtualPath );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/VSICURL/https://x" ) ) == QgsDataSourceKind::GdalVirtualPath );
}

TEST_CASE( "QgsDataSourceResolver classifies OGR connection strings", "[core][datasource_resolver]" )
{
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "PG:dbname=gis" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "PG:host=localhost dbname=test" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "GPKG:/tmp/a.gpkg" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "WFS:https://example.com/wfs" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "MySQL:host=localhost dbname=test" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "MSSQL:server=localhost;database=gis" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "OCI:dbname=gis" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "ODBC:DSN=mydsn" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "SQLite:/tmp/a.sqlite" ) ) == QgsDataSourceKind::OgrConnectionString );
}

TEST_CASE( "QgsDataSourceResolver classification is case-insensitive for OGR", "[core][datasource_resolver]" )
{
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "pg:dbname=gis" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "Pg:dbname=gis" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "gpkg:/tmp/a.gpkg" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "wfs:https://example.com/wfs" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "mysql:host=localhost" ) ) == QgsDataSourceKind::OgrConnectionString );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "sqlite:/tmp/a.db" ) ) == QgsDataSourceKind::OgrConnectionString );
}

TEST_CASE( "QgsDataSourceResolver classifies remote URIs", "[core][datasource_resolver]" )
{
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "https://example.com/x.tif" ) ) == QgsDataSourceKind::RemoteUri );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "http://example.com/x.tif" ) ) == QgsDataSourceKind::RemoteUri );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "ftp://example.com/x.tif" ) ) == QgsDataSourceKind::RemoteUri );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "s3://bucket/key.tif" ) ) == QgsDataSourceKind::RemoteUri );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "gs://bucket/key.tif" ) ) == QgsDataSourceKind::RemoteUri );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "az://container/blob.tif" ) ) == QgsDataSourceKind::RemoteUri );
}

TEST_CASE( "QgsDataSourceResolver classification is case-insensitive for remote URIs", "[core][datasource_resolver]" )
{
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "HTTPS://example.com/x.tif" ) ) == QgsDataSourceKind::RemoteUri );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "S3://bucket/key" ) ) == QgsDataSourceKind::RemoteUri );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "GS://bucket/key" ) ) == QgsDataSourceKind::RemoteUri );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "AZ://container/blob" ) ) == QgsDataSourceKind::RemoteUri );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "FTP://example.com/x.tif" ) ) == QgsDataSourceKind::RemoteUri );
}

TEST_CASE( "QgsDataSourceResolver classifies Windows drive paths as LocalFile", "[core][datasource_resolver]" )
{
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "C:/data/a.tif" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "C:\\data\\a.tif" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "D:/a.tif" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "c:/lower.tif" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "Z:\\path\\to\\file.tif" ) ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "C:" ) ) == QgsDataSourceKind::LocalFile );
    // Windows drive must precede OGR check: C:/... must not be mistaken for connection string
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "C:/PG:foo" ) ) == QgsDataSourceKind::LocalFile );
}

TEST_CASE( "QgsDataSourceResolver classifies empty string as LocalFile", "[core][datasource_resolver]" )
{
    CHECK( QgsDataSourceResolver::classify( QString() ) == QgsDataSourceKind::LocalFile );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "" ) ) == QgsDataSourceKind::LocalFile );
}

TEST_CASE( "QgsDataSourceResolver priority: VSI wins over all", "[core][datasource_resolver]" )
{
    // /vsi prefix has highest priority after empty
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/vsimem/PG:foo" ) ) == QgsDataSourceKind::GdalVirtualPath );
    CHECK( QgsDataSourceResolver::classify( QStringLiteral( "/vsi/something" ) ) == QgsDataSourceKind::GdalVirtualPath );
}

TEST_CASE( "QgsDataSourceResolver requiresLocalExistenceCheck matches classify", "[core][datasource_resolver]" )
{
    auto check = []( const QString &s ) {
        const bool needsCheck = QgsDataSourceResolver::requiresLocalExistenceCheck( s );
        const bool isLocal = QgsDataSourceResolver::classify( s ) == QgsDataSourceKind::LocalFile;
        CHECK( needsCheck == isLocal );
    };
    // LocalFile -> true
    check( QStringLiteral( "data/a.tif" ) );
    check( QStringLiteral( "/tmp/a.tif" ) );
    check( QStringLiteral( "C:/data/a.tif" ) );
    check( QStringLiteral( "C:\\data\\a.tif" ) );
    check( QStringLiteral( "" ) );
    check( QStringLiteral( "some_random" ) );
    // GdalVirtualPath -> false
    check( QStringLiteral( "/vsimem/x.tif" ) );
    check( QStringLiteral( "/vsizip/a.zip/b.tif" ) );
    check( QStringLiteral( "/vsicurl/https://x" ) );
    check( QStringLiteral( "/vsis3/bucket/key" ) );
    // OgrConnectionString -> false
    check( QStringLiteral( "PG:dbname=gis" ) );
    check( QStringLiteral( "GPKG:/tmp/a.gpkg" ) );
    check( QStringLiteral( "WFS:https://x" ) );
    check( QStringLiteral( "MySQL:host=localhost" ) );
    // RemoteUri -> false
    check( QStringLiteral( "https://example.com/x.tif" ) );
    check( QStringLiteral( "s3://bucket/key" ) );
    check( QStringLiteral( "gs://bucket/key" ) );
    check( QStringLiteral( "az://container/blob" ) );
}

TEST_CASE( "QgsDataSourceResolver kindToString round-trips", "[core][datasource_resolver]" )
{
    CHECK( QgsDataSourceResolver::kindToString( QgsDataSourceKind::LocalFile ) == QStringLiteral( "LocalFile" ) );
    CHECK( QgsDataSourceResolver::kindToString( QgsDataSourceKind::GdalVirtualPath ) == QStringLiteral( "GdalVirtualPath" ) );
    CHECK( QgsDataSourceResolver::kindToString( QgsDataSourceKind::OgrConnectionString ) == QStringLiteral( "OgrConnectionString" ) );
    CHECK( QgsDataSourceResolver::kindToString( QgsDataSourceKind::RemoteUri ) == QStringLiteral( "RemoteUri" ) );
}
