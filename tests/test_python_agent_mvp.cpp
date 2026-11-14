// Real agent -> Task Center -> Python -> transactional Data Asset delivery.
// The mask oracle is a fixed, hand-worked matrix, independent of the script.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/mcp_server.h"
#include "data/data_manager.h"
#include "jobs/job_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_id.h"
#include "processing/framework/task_center.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace
{

using sicnu::processing::ExecutionId;

void ensureApplication()
{
  if ( QCoreApplication::instance() )
    return;
  static int argc = 1;
  static char name[] = "test_python_agent_mvp";
  static char *argv[] = { name, nullptr };
  static auto *application = new QCoreApplication( argc, argv );
  ( void ) application;
}

class ScopedEnvironment
{
  public:
    ScopedEnvironment( const char *name, const QByteArray &value )
      : m_name( name ), m_previous( qgetenv( name ) ),
        m_wasSet( qEnvironmentVariableIsSet( name ) )
    {
      qputenv( m_name.constData(), value );
    }
    ~ScopedEnvironment()
    {
      if ( m_wasSet )
        qputenv( m_name.constData(), m_previous );
      else
        qunsetenv( m_name.constData() );
    }

  private:
    QByteArray m_name;
    QByteArray m_previous;
    bool m_wasSet;
};

QString configuredEnvironmentRoot()
{
  const QString root = qEnvironmentVariable( "SICNU_TEST_PYTHON_ENV_ROOT" );
  if ( root.isEmpty() )
    SKIP( "Real Python E2E requires SICNU_TEST_PYTHON_ENV_ROOT; no implicit dependency installation." );
  return QDir( root ).absolutePath();
}

bool waitUntil( const std::function<bool()> &predicate, int timeoutMs )
{
  QElapsedTimer timer;
  timer.start();
  do
  {
    QCoreApplication::processEvents();
    if ( predicate() )
      return true;
    QThread::msleep( 10 );
  } while ( timer.elapsed() < timeoutMs );
  return false;
}

std::string readable( const QVariantMap &value )
{
  return QJsonDocument( QJsonObject::fromVariantMap( value ) )
    .toJson( QJsonDocument::Compact ).toStdString();
}

// Captures protocol output only: all execution, scheduling and committing
// remain the production McpServer/TaskCenter/JobEngine implementations.
class RecordingMcpServer : public McpServer
{
  public:
    QVariantMap request( const QString &method, const QVariantMap &parameters )
    {
      m_response.clear();
      m_error.clear();
      m_responded = false;
      handleRequest( { { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
                       { QStringLiteral( "id" ), ++m_nextId },
                       { QStringLiteral( "method" ), method },
                       { QStringLiteral( "params" ), parameters } } );
      INFO( m_error.toStdString() );
      REQUIRE( m_error.isEmpty() );
      REQUIRE( m_responded );
      return m_response;
    }

    void initializedNotification()
    {
      m_error.clear();
      m_responded = false;
      handleRequest( { { QStringLiteral( "jsonrpc" ), QStringLiteral( "2.0" ) },
                       { QStringLiteral( "method" ), QStringLiteral( "notifications/initialized" ) } } );
      INFO( m_error.toStdString() );
      REQUIRE( m_error.isEmpty() );
      REQUIRE_FALSE( m_responded );
    }

  protected:
    void sendResponse( const QVariant &, const QVariantMap &result ) override
    {
      m_response = result;
      m_responded = true;
    }
    void sendError( const QVariant &, int, const QString &message ) override
    {
      m_error = message;
    }
    void sendError( const QVariant &, int, const QString &message, const QVariantMap & ) override
    {
      m_error = message;
    }
    void sendNotification( const QString &, const QVariantMap & ) override {}

  private:
    QVariantMap m_response;
    QString m_error;
    bool m_responded = false;
    int m_nextId = 0;
};

QString deliveredAssetId( const QVariantMap &status )
{
  const QString nested = status.value( QStringLiteral( "result" ) ).toMap()
                           .value( QStringLiteral( "assetId" ) ).toString();
  return nested.isEmpty() ? status.value( QStringLiteral( "asset_id" ) ).toString() : nested;
}

struct AgentFixture
{
  explicit AgentFixture( const QString &environmentRoot )
    : workspacePolicy( "SICNU_MCP_WORKSPACE", workspace.path().toUtf8() ),
      environmentPolicy( "SICNU_PYTHON_ENV_ROOT", environmentRoot.toUtf8() ),
      executionPolicy( "SICNU_PYTHON_EXECUTION", QByteArrayLiteral( "trusted_local" ) )
  {
    REQUIRE( workspace.isValid() );
    sicnu::processing::AtomicAlgorithmRegistry::instance().initialize();
    server.setDataManager( &data );
    const QVariantMap initialized = server.request( QStringLiteral( "initialize" ),
      { { QStringLiteral( "protocolVersion" ), QStringLiteral( "2024-11-05" ) },
        { QStringLiteral( "capabilities" ), QVariantMap() },
        { QStringLiteral( "clientInfo" ),
          QVariantMap{ { QStringLiteral( "name" ), QStringLiteral( "python-mvp-e2e" ) },
                       { QStringLiteral( "version" ), QStringLiteral( "1" ) } } } } );
    REQUIRE( initialized.value( QStringLiteral( "protocolVersion" ) ).toString()
             == QLatin1String( "2024-11-05" ) );
    server.initializedNotification();
  }

  ~AgentFixture()
  {
    // A failed assertion must not leave a live Python child or delete files
    // while the owning JobEngine body is still using the workspace.
    for ( const long taskId : tasks )
    {
      const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
      if ( !sicnu::isTerminalStatus( info.status ) )
        sicnu::TaskCenter::instance().cancelTask( taskId );
      if ( !info.jobId.empty() )
        sicnu::jobs::JobEngine::instance().waitForJob( info.jobId, 5000 );
    }
    QCoreApplication::processEvents();
  }

  QVariantMap call( const QString &tool, const QVariantMap &arguments )
  {
    const QVariantMap envelope = server.request(
      QStringLiteral( "tools/call" ),
      { { QStringLiteral( "name" ), tool }, { QStringLiteral( "arguments" ), arguments } } );
    INFO( readable( envelope ) );
    REQUIRE_FALSE( envelope.value( QStringLiteral( "isError" ) ).toBool() );
    const QVariantList content = envelope.value( QStringLiteral( "content" ) ).toList();
    REQUIRE( content.size() == 1 );
    const QByteArray text = content.first().toMap().value( QStringLiteral( "text" ) ).toString().toUtf8();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson( text, &parseError );
    REQUIRE( parseError.error == QJsonParseError::NoError );
    REQUIRE( document.isObject() );
    return document.object().toVariantMap();
  }

  QString submit( const QString &tool, const QVariantMap &arguments )
  {
    const QVariantMap result = call( tool, arguments );
    INFO( readable( result ) );
    const QString wire = result.value( QStringLiteral( "execution_id" ) ).toString();
    const auto id = ExecutionId::fromWire( wire );
    REQUIRE( id.has_value() );
    tasks.push_back( id->taskId() );
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( id->taskId() );
    CHECK( info.algorithmId == tool );
    CHECK( info.resourceProfile == sicnu::ProviderResourceProfile::ExternalCliSubprocess );
    return wire;
  }

  QVariantMap await( const QString &wire, bool requireAsset = false, int timeoutMs = 30000 )
  {
    QVariantMap latest;
    const bool done = waitUntil( [ & ] {
      latest = call( QStringLiteral( "get_execution_status" ),
                     { { QStringLiteral( "execution_id" ), wire } } );
      const QString state = latest.value( QStringLiteral( "status" ) ).toString();
      if ( state == QLatin1String( "failed" ) || state == QLatin1String( "canceled" ) )
        return true;
      if ( state != QLatin1String( "completed" ) )
        return false;
      // The async committer runs on the DataManager owner thread. A task's
      // worker reaching Completed can precede that queued delivery.
      return !requireAsset || !deliveredAssetId( latest ).isEmpty()
             || latest.value( QStringLiteral( "result" ) ).toMap()
                  .value( QStringLiteral( "status" ) ).toString() == QLatin1String( "error" );
    }, timeoutMs );
    INFO( readable( latest ) );
    REQUIRE( done );
    return latest;
  }

  QTemporaryDir workspace;
  ScopedEnvironment workspacePolicy;
  ScopedEnvironment environmentPolicy;
  ScopedEnvironment executionPolicy;
  sicnu::data::DataManager data;
  RecordingMcpServer server;
  std::vector<long> tasks;
};

const std::array<double, 6> kGrid = { 300000, 10, 0, 4000000, 0, -10 };

void writeNdvi( const QString &path, std::array<float, 12> values )
{
  ensureGdalInit();
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  const auto close = []( GDALDataset *dataset ) { GDALClose( dataset ); };
  std::unique_ptr<GDALDataset, decltype( close )> dataset(
    driver->Create( path.toUtf8().constData(), 4, 3, 1, GDT_Float32, nullptr ), close );
  REQUIRE( dataset != nullptr );
  auto transform = kGrid;
  REQUIRE( dataset->SetGeoTransform( transform.data() ) == CE_None );
  OGRSpatialReference crs;
  REQUIRE( crs.importFromEPSG( 32648 ) == OGRERR_NONE );
  char *wkt = nullptr;
  REQUIRE( crs.exportToWkt( &wkt ) == OGRERR_NONE );
  const CPLErr projectionResult = dataset->SetProjection( wkt );
  CPLFree( wkt );
  REQUIRE( projectionResult == CE_None );
  GDALRasterBand *band = dataset->GetRasterBand( 1 );
  REQUIRE( band->SetNoDataValue( -9999 ) == CE_None );
  REQUIRE( band->RasterIO( GF_Write, 0, 0, 4, 3, values.data(), 4, 3,
                           GDT_Float32, 0, 0 ) == CE_None );
}

sicnu::data::AssetId registerRaster( AgentFixture &fixture, const QString &path )
{
  sicnu::data::RegisterRequest request;
  request.source.providerKey = QStringLiteral( "gdal" );
  request.source.canonicalSource = path;
  const auto result = fixture.data.registerSource( request );
  REQUIRE_FALSE( result.assetId.isNull() );
  return result.assetId;
}

void checkDigest( const QString &digest )
{
  REQUIRE( digest.size() == 64 );
  CHECK( QByteArray::fromHex( digest.toLatin1() ).size() == 32 );
}

QString fileDigest( const QString &path )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  return QString::fromLatin1( QCryptographicHash::hash( file.readAll(), QCryptographicHash::Sha256 ).toHex() );
}

QString writeScript( AgentFixture &fixture, const QString &name, const QByteArray &source )
{
  const QString path = fixture.workspace.filePath( name );
  QFile file( path );
  REQUIRE( file.open( QIODevice::WriteOnly ) );
  REQUIRE( file.write( source ) == source.size() );
  file.close();
  return path;
}

void requirePreparedEnvironment( AgentFixture &fixture )
{
  const QString probe = fixture.submit( QStringLiteral( "python:environment" ),
    { { QStringLiteral( "action" ), QStringLiteral( "probe" ) },
      { QStringLiteral( "profile_id" ), QStringLiteral( "geospatial-v1" ) } } );
  const QVariantMap status = fixture.await( probe );
  INFO( readable( status ) );
  REQUIRE( status.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "completed" ) );
  INFO( "Run the environment preparation E2E first against SICNU_TEST_PYTHON_ENV_ROOT." );
  REQUIRE( status.value( QStringLiteral( "result" ) ).toMap().value( QStringLiteral( "ready" ) ).toBool() );
}

struct InputEvidence
{
  QString path;
  sicnu::data::AssetId id;
  sicnu::data::AssetRevision revision;
  QString digest;
};

InputEvidence registeredInput( AgentFixture &fixture )
{
  const QString path = fixture.workspace.filePath( QStringLiteral( "input.tif" ) );
  writeNdvi( path, { .1f, .2f, .3f, -9999, .4f, .5f, .6f, .7f, -9999, .1f, .2f, .3f } );
  const auto id = registerRaster( fixture, path );
  return { path, id, fixture.data.asset( id )->revision(), fileDigest( path ) };
}

void settleJob( const QString &wire )
{
  const auto task = sicnu::TaskCenter::instance().getTaskInfo( ExecutionId::fromWire( wire )->taskId() );
  REQUIRE_FALSE( task.jobId.empty() );
  REQUIRE( sicnu::jobs::JobEngine::instance().waitForJob( task.jobId, 5000 ) );
  QCoreApplication::processEvents();
}

void checkNoNewAsset( AgentFixture &fixture, const InputEvidence &input, const QString &output )
{
  CHECK_FALSE( fixture.data.findByPath( output ).has_value() );
  CHECK( fixture.data.assets().size() == 1 );
  REQUIRE( fixture.data.asset( input.id ).has_value() );
  CHECK( fixture.data.asset( input.id )->revision() == input.revision );
  CHECK( fileDigest( input.path ) == input.digest );
  // This fixture begins with files only. No task-owned temporary directory
  // may survive a refused, failed or timed-out script.
  CHECK( QDir( fixture.workspace.path() )
           .entryList( QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot ).isEmpty() );
}

} // namespace

TEST_CASE( "Python agent prepares an environment and delivers a verified NDVI change mask",
           "[python][mvp][e2e][real]" )
{
  ensureApplication();
  AgentFixture fixture( configuredEnvironmentRoot() );

  const QVariantList tools = fixture.server.request( QStringLiteral( "tools/list" ),
                               { { QStringLiteral( "includeSchemas" ), true } } )
                               .value( QStringLiteral( "tools" ) ).toList();
  for ( const QString &name : { QStringLiteral( "python:environment" ),
                               QStringLiteral( "python:run_script" ) } )
  {
    const auto found = std::find_if( tools.cbegin(), tools.cend(), [ & ]( const QVariant &tool ) {
      return tool.toMap().value( QStringLiteral( "name" ) ).toString() == name;
    } );
    REQUIRE( found != tools.cend() );
    CHECK( found->toMap().value( QStringLiteral( "inputSchema" ) ).toMap()
             .value( QStringLiteral( "type" ) ).toString() == QLatin1String( "object" ) );
  }

  const QVariantMap environmentArguments = {
    { QStringLiteral( "action" ), QStringLiteral( "prepare" ) },
    { QStringLiteral( "profile_id" ), QStringLiteral( "geospatial-v1" ) },
    { QStringLiteral( "timeout_seconds" ), 600 } };
  const QString preparation = fixture.submit( QStringLiteral( "python:environment" ), environmentArguments );
  const QVariantMap prepared = fixture.await( preparation, false, 650000 );
  INFO( readable( prepared ) );
  REQUIRE( prepared.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "completed" ) );
  REQUIRE( prepared.value( QStringLiteral( "result" ) ).toMap()
             .value( QStringLiteral( "ready" ) ).toBool() );
  CHECK( fixture.data.assets().isEmpty() );

  const QString probe = fixture.submit( QStringLiteral( "python:environment" ),
    { { QStringLiteral( "action" ), QStringLiteral( "probe" ) },
      { QStringLiteral( "profile_id" ), QStringLiteral( "geospatial-v1" ) } } );
  const QVariantMap probed = fixture.await( probe );
  REQUIRE( probed.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "completed" ) );
  const QVariantMap environment = probed.value( QStringLiteral( "result" ) ).toMap();
  REQUIRE( environment.value( QStringLiteral( "ready" ) ).toBool() );
  checkDigest( environment.value( QStringLiteral( "environment_sha256" ) ).toString() );
  checkDigest( environment.value( QStringLiteral( "lock_sha256" ) ).toString() );

  const QString before = fixture.workspace.filePath( QStringLiteral( "before.tif" ) );
  const QString after = fixture.workspace.filePath( QStringLiteral( "after.tif" ) );
  writeNdvi( before, { .1f, .2f, .3f, -9999, .4f, .5f, .6f, .7f, -9999, .1f, .2f, .3f } );
  writeNdvi( after, { .1f, .55f, .65f, .4f, .05f, .5f, .25f, .72f, .5f, -9999, .21f, .31f } );
  const auto beforeAsset = registerRaster( fixture, before );
  const auto afterAsset = registerRaster( fixture, after );

  const QString script = fixture.workspace.filePath( QStringLiteral( "ndvi_change_mask.py" ) );
  REQUIRE( QFile::copy( QStringLiteral( SICNU_SOURCE_DIR "/docs/python/examples/ndvi_change_mask.py" ), script ) );
  QFile scriptFile( script );
  REQUIRE( scriptFile.open( QIODevice::ReadOnly ) );
  const QByteArray scriptSource = scriptFile.readAll();
  const QString scriptDigest = QString::fromLatin1(
    QCryptographicHash::hash( scriptSource, QCryptographicHash::Sha256 ).toHex() );
  scriptFile.close();

  const QString output = fixture.workspace.filePath( QStringLiteral( "change_mask.tif" ) );
  const QString wire = fixture.submit( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "profile_id" ), QStringLiteral( "geospatial-v1" ) },
      { QStringLiteral( "script" ), QString::fromUtf8( scriptSource ) },
      { QStringLiteral( "inputs" ), QStringList{ before, after } },
      { QStringLiteral( "output" ), output },
      { QStringLiteral( "parameters" ), QVariantMap{ { QStringLiteral( "threshold" ), .2 } } },
      { QStringLiteral( "timeout_seconds" ), 30 } } );
  const QVariantMap terminal = fixture.await( wire, true );
  INFO( readable( terminal ) );
  REQUIRE( terminal.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "completed" ) );
  const QVariantMap result = terminal.value( QStringLiteral( "result" ) ).toMap();
  CHECK( result.value( QStringLiteral( "output" ) ).toString() == output );
  CHECK( result.value( QStringLiteral( "script_sha256" ) ).toString() == scriptDigest );
  CHECK( result.value( QStringLiteral( "environment_sha256" ) )
         == environment.value( QStringLiteral( "environment_sha256" ) ) );
  CHECK( result.value( QStringLiteral( "lock_sha256" ) )
         == environment.value( QStringLiteral( "lock_sha256" ) ) );
  REQUIRE_FALSE( result.value( QStringLiteral( "python_version" ) ).toString().isEmpty() );
  const QVariantMap statistics = result.value( QStringLiteral( "statistics" ) ).toMap();
  CHECK( statistics.value( QStringLiteral( "changed_pixels" ) ).toInt() == 4 );
  CHECK( statistics.value( QStringLiteral( "valid_pixels" ) ).toInt() == 9 );
  CHECK( statistics.value( QStringLiteral( "changed_fraction" ) ).toDouble() == Catch::Approx( 4.0 / 9.0 ) );

  const auto outputId = sicnu::data::AssetId::fromString( deliveredAssetId( terminal ) );
  REQUIRE( outputId.has_value() );
  const auto asset = fixture.data.asset( *outputId );
  REQUIRE( asset.has_value() );
  CHECK( asset->source().canonicalSource == QFileInfo( output ).canonicalFilePath() );
  CHECK( fixture.data.assets().size() == 3 );
  const auto provenance = fixture.data.provenance( *outputId );
  REQUIRE( provenance.has_value() );
  CHECK_FALSE( provenance->parameters.contains( QStringLiteral( "script" ) ) );
  CHECK( provenance->algorithmId == QLatin1String( "python:run_script" ) );
  CHECK( provenance->taskReference == QString::number( ExecutionId::fromWire( wire )->taskId() ) );
  REQUIRE( provenance->inputs.size() == 2 );
  for ( const auto &input : { beforeAsset, afterAsset } )
  {
    const auto edge = std::find_if( provenance->inputs.cbegin(), provenance->inputs.cend(),
      [ & ]( const auto &entry ) { return entry.assetId == input; } );
    REQUIRE( edge != provenance->inputs.cend() );
    CHECK( edge->revision == fixture.data.asset( input )->revision() );
  }
  const QJsonObject execution = provenance->parameters.value( QStringLiteral( "python_execution" ) ).toObject();
  for ( const QString &key : { QStringLiteral( "script_sha256" ), QStringLiteral( "environment_sha256" ),
                              QStringLiteral( "lock_sha256" ), QStringLiteral( "python_version" ),
                              QStringLiteral( "profile_id" ) } )
    CHECK( execution.value( key ).toVariant() == result.value( key ) );

  const auto close = []( GDALDataset *dataset ) { GDALClose( dataset ); };
  std::unique_ptr<GDALDataset, decltype( close )> mask(
    static_cast<GDALDataset *>( GDALOpen( output.toUtf8().constData(), GA_ReadOnly ) ), close );
  REQUIRE( mask != nullptr );
  CHECK( mask->GetRasterXSize() == 4 );
  CHECK( mask->GetRasterYSize() == 3 );
  REQUIRE( mask->GetRasterCount() == 1 );
  std::array<double, 6> transform;
  REQUIRE( mask->GetGeoTransform( transform.data() ) == CE_None );
  CHECK( transform == kGrid );
  OGRSpatialReference actual( mask->GetProjectionRef() );
  OGRSpatialReference expected;
  REQUIRE( expected.importFromEPSG( 32648 ) == OGRERR_NONE );
  CHECK( actual.IsSame( &expected ) );
  GDALRasterBand *band = mask->GetRasterBand( 1 );
  CHECK( band->GetRasterDataType() == GDT_Byte );
  int hasNoData = 0;
  CHECK( band->GetNoDataValue( &hasNoData ) == 255 );
  CHECK( hasNoData == 1 );
  std::array<unsigned char, 12> pixels;
  REQUIRE( band->RasterIO( GF_Read, 0, 0, 4, 3, pixels.data(), 4, 3,
                           GDT_Byte, 0, 0 ) == CE_None );
  const std::array<unsigned char, 12> expectedPixels = { 0, 1, 1, 255, 1, 0, 1, 0, 255, 255, 0, 0 };
  CHECK( pixels == expectedPixels );
}

TEST_CASE( "Python agent cancels an already running script without publishing its staged raster",
           "[python][mvp][e2e][cancel][real]" )
{
  ensureApplication();
  AgentFixture fixture( configuredEnvironmentRoot() );
  requirePreparedEnvironment( fixture );

  const QString input = fixture.workspace.filePath( QStringLiteral( "before.tif" ) );
  writeNdvi( input, { .1f, .2f, .3f, -9999, .4f, .5f, .6f, .7f, -9999, .1f, .2f, .3f } );
  const auto inputId = registerRaster( fixture, input );
  const auto inputRevision = fixture.data.asset( inputId )->revision();
  const QString inputDigest = fileDigest( input );
  const QString output = fixture.workspace.filePath( QStringLiteral( "cancelled.tif" ) );
  const QString ready = fixture.workspace.filePath( QStringLiteral( "script-ready.txt" ) );
  const QString script = writeScript( fixture, QStringLiteral( "wait_after_write.py" ), QByteArrayLiteral( R"PY(
import rasterio
import time

with rasterio.open(inputs[0]) as source:
    with rasterio.open(output, 'w', **source.profile) as target:
        target.write(source.read())
with open(parameters['ready_path'], 'w', encoding='utf-8') as marker:
    marker.write(output)
    marker.flush()
while True:
    time.sleep(0.05)
)PY" ) );

  const QString wire = fixture.submit( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "profile_id" ), QStringLiteral( "geospatial-v1" ) },
      { QStringLiteral( "script_path" ), script },
      { QStringLiteral( "inputs" ), QStringList{ input } },
      { QStringLiteral( "output" ), output },
      { QStringLiteral( "parameters" ), QVariantMap{ { QStringLiteral( "ready_path" ), ready } } },
      { QStringLiteral( "timeout_seconds" ), 30 } } );

  // The marker is written after a real raster is closed by Python. It
  // distinguishes an active script from a queued/admitted-but-not-started job.
  REQUIRE( waitUntil( [ & ] { return QFileInfo( ready ).size() > 0; }, 15000 ) );
  QFile marker( ready );
  REQUIRE( marker.open( QIODevice::ReadOnly ) );
  const QString stagedOutput = QString::fromUtf8( marker.readAll() );
  marker.close();
  REQUIRE( QFileInfo( stagedOutput ).isFile() );
  const QString stagingDirectory = QFileInfo( stagedOutput ).absolutePath();
  REQUIRE( QFileInfo( QFileInfo( stagingDirectory ).absolutePath() ).canonicalFilePath()
           == QFileInfo( fixture.workspace.path() ).canonicalFilePath() );
  CHECK_FALSE( QFileInfo::exists( output ) );
  CHECK( fixture.data.assets().size() == 1 );
  const QVariantMap running = fixture.call( QStringLiteral( "get_execution_status" ),
    { { QStringLiteral( "execution_id" ), wire } } );
  REQUIRE( running.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "running" ) );

  fixture.call( QStringLiteral( "cancel_execution" ),
    { { QStringLiteral( "execution_id" ), wire } } );
  const QVariantMap terminal = fixture.await( wire );
  INFO( readable( terminal ) );
  REQUIRE( terminal.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "canceled" ) );
  const auto task = sicnu::TaskCenter::instance().getTaskInfo( ExecutionId::fromWire( wire )->taskId() );
  REQUIRE_FALSE( task.jobId.empty() );
  REQUIRE( sicnu::jobs::JobEngine::instance().waitForJob( task.jobId, 5000 ) );
  CHECK_FALSE( QFileInfo::exists( stagingDirectory ) );
  CHECK_FALSE( QFileInfo::exists( output ) );
  CHECK_FALSE( fixture.data.findByPath( output ).has_value() );
  CHECK( fixture.data.assets().size() == 1 );
  REQUIRE( fixture.data.asset( inputId ).has_value() );
  CHECK( fixture.data.asset( inputId )->revision() == inputRevision );
  CHECK( fileDigest( input ) == inputDigest );
}

TEST_CASE( "Python agent refuses an external raster mask before asset publication",
           "[python][mvp][e2e][external-mask][real]" )
{
  ensureApplication();
  AgentFixture fixture( configuredEnvironmentRoot() );
  requirePreparedEnvironment( fixture );

  const QString input = fixture.workspace.filePath( QStringLiteral( "before.tif" ) );
  writeNdvi( input, { .1f, .2f, .3f, -9999, .4f, .5f, .6f, .7f, -9999, .1f, .2f, .3f } );
  const auto inputId = registerRaster( fixture, input );
  const auto inputRevision = fixture.data.asset( inputId )->revision();
  const QString inputDigest = fileDigest( input );
  const QString output = fixture.workspace.filePath( QStringLiteral( "masked.tif" ) );
  const QString ready = fixture.workspace.filePath( QStringLiteral( "mask-ready.json" ) );
  const QString release = fixture.workspace.filePath( QStringLiteral( "mask-release.txt" ) );
  const QByteArray source = QByteArrayLiteral( R"PY(
import json
import pathlib
import time
import numpy as np
import rasterio

with rasterio.Env(GDAL_TIFF_INTERNAL_MASK=False):
    with rasterio.open(inputs[0]) as image:
        with rasterio.open(output, 'w', **image.profile) as target:
            target.write(image.read())
            mask = np.full((image.height, image.width), 255, dtype=np.uint8)
            mask[0, 0] = 0
            target.write_mask(mask)
sidecar = pathlib.Path(output + '.msk')
if not sidecar.is_file():
    raise RuntimeError('Expected a real external GDAL mask')
with open(parameters['ready_path'], 'w', encoding='utf-8') as marker:
    json.dump({'output': output, 'mask': str(sidecar)}, marker)
    marker.flush()
while not pathlib.Path(parameters['release_path']).is_file():
    time.sleep(0.01)
result = {'external_mask_created': True}
)PY" );

  const QString wire = fixture.submit( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "profile_id" ), QStringLiteral( "geospatial-v1" ) },
      { QStringLiteral( "script" ), QString::fromUtf8( source ) },
      { QStringLiteral( "inputs" ), QStringList{ input } },
      { QStringLiteral( "output" ), output },
      { QStringLiteral( "parameters" ), QVariantMap{ { QStringLiteral( "ready_path" ), ready },
                                                     { QStringLiteral( "release_path" ), release } } },
      { QStringLiteral( "timeout_seconds" ), 30 } } );

  REQUIRE( waitUntil( [ & ] { return QFileInfo( ready ).size() > 0; }, 15000 ) );
  QFile marker( ready );
  REQUIRE( marker.open( QIODevice::ReadOnly ) );
  QJsonParseError parseError;
  const QJsonDocument created = QJsonDocument::fromJson( marker.readAll(), &parseError );
  marker.close();
  REQUIRE( parseError.error == QJsonParseError::NoError );
  REQUIRE( created.isObject() );
  const QString stagedOutput = created.object().value( QStringLiteral( "output" ) ).toString();
  const QString externalMask = created.object().value( QStringLiteral( "mask" ) ).toString();
  const QString stagingDirectory = QFileInfo( stagedOutput ).absolutePath();
  REQUIRE( QFileInfo( stagedOutput ).isFile() );
  REQUIRE( QFileInfo( externalMask ).isFile() );
  REQUIRE( QFileInfo( externalMask ).size() > 0 );
  REQUIRE( QFileInfo( externalMask ).absolutePath() == stagingDirectory );
  REQUIRE( QFileInfo( QFileInfo( stagingDirectory ).absolutePath() ).canonicalFilePath()
           == QFileInfo( fixture.workspace.path() ).canonicalFilePath() );
  {
    // Reopen through native GDAL before releasing the script: the sidecar
    // really supplies mask semantics, rather than merely having a .msk name.
    const auto close = []( GDALDataset *dataset ) { GDALClose( dataset ); };
    std::unique_ptr<GDALDataset, decltype( close )> raster(
      static_cast<GDALDataset *>( GDALOpen( stagedOutput.toUtf8().constData(), GA_ReadOnly ) ), close );
    REQUIRE( raster != nullptr );
    GDALRasterBand *mask = raster->GetRasterBand( 1 )->GetMaskBand();
    REQUIRE( mask != nullptr );
    unsigned char firstMaskValue = 255;
    REQUIRE( mask->RasterIO( GF_Read, 0, 0, 1, 1, &firstMaskValue, 1, 1,
                             GDT_Byte, 0, 0 ) == CE_None );
    CHECK( firstMaskValue == 0 );
  }
  CHECK_FALSE( QFileInfo::exists( output ) );
  QFile releaseFile( release );
  REQUIRE( releaseFile.open( QIODevice::WriteOnly ) );
  REQUIRE( releaseFile.write( "continue" ) == 8 );
  releaseFile.close();

  const QVariantMap terminal = fixture.await( wire );
  INFO( readable( terminal ) );
  const auto task = sicnu::TaskCenter::instance().getTaskInfo( ExecutionId::fromWire( wire )->taskId() );
  REQUIRE_FALSE( task.jobId.empty() );
  REQUIRE( sicnu::jobs::JobEngine::instance().waitForJob( task.jobId, 5000 ) );
  QCoreApplication::processEvents();
  CHECK( terminal.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "failed" ) );
  CHECK_FALSE( QFileInfo::exists( output ) );
  CHECK_FALSE( QFileInfo::exists( stagingDirectory ) );
  CHECK_FALSE( fixture.data.findByPath( output ).has_value() );
  CHECK( fixture.data.assets().size() == 1 );
  REQUIRE( fixture.data.asset( inputId ).has_value() );
  CHECK( fixture.data.asset( inputId )->revision() == inputRevision );
  CHECK( fileDigest( input ) == inputDigest );
}

TEST_CASE( "Python agent enforces the script wall-clock budget without delivering partial output",
           "[python][mvp][e2e][timeout][real]" )
{
  ensureApplication();
  AgentFixture fixture( configuredEnvironmentRoot() );
  requirePreparedEnvironment( fixture );
  const InputEvidence input = registeredInput( fixture );
  const QString output = fixture.workspace.filePath( QStringLiteral( "timed-out.tif" ) );
  const QString ready = fixture.workspace.filePath( QStringLiteral( "timeout-ready.txt" ) );
  const QByteArray source = QByteArrayLiteral( R"PY(
from pathlib import Path
import time

Path(output).write_bytes(b'partial raster')
Path(parameters['ready_path']).write_text(output, encoding='utf-8')
time.sleep(10)
)PY" );
  const QString wire = fixture.submit( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "profile_id" ), QStringLiteral( "geospatial-v1" ) },
      { QStringLiteral( "script" ), QString::fromUtf8( source ) },
      { QStringLiteral( "inputs" ), QStringList{ input.path } },
      { QStringLiteral( "output" ), output },
      { QStringLiteral( "parameters" ), QVariantMap{ { QStringLiteral( "ready_path" ), ready } } },
      { QStringLiteral( "timeout_seconds" ), 1 } } );
  REQUIRE( waitUntil( [ & ] { return QFileInfo( ready ).size() > 0; }, 15000 ) );
  const QVariantMap terminal = fixture.await( wire );
  INFO( readable( terminal ) );
  settleJob( wire );
  CHECK( terminal.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "failed" ) );
  CHECK( terminal.value( QStringLiteral( "errorMessage" ) ).toString()
           .contains( QStringLiteral( "wall-clock budget" ) ) );
  CHECK_FALSE( QFileInfo::exists( output ) );
  checkNoNewAsset( fixture, input, output );
}

TEST_CASE( "Python agent refuses failed scripts and invalid deliveries without changing existing data",
           "[python][mvp][e2e][failure][real]" )
{
  ensureApplication();
  AgentFixture fixture( configuredEnvironmentRoot() );
  requirePreparedEnvironment( fixture );
  const InputEvidence input = registeredInput( fixture );
  const QString output = fixture.workspace.filePath( QStringLiteral( "failed.tif" ) );
  const QString marker = fixture.workspace.filePath( QStringLiteral( "unexpected-script-start.txt" ) );
  QString source;
  QString expectedError;
  QString existingDigest;

  SECTION( "a script raises after writing partial staged data" )
  {
    source = QStringLiteral( "from pathlib import Path\n"
                             "Path(output).write_bytes(b'partial raster')\n"
                             "raise RuntimeError('deliberate-script-failure')\n" );
    expectedError = QStringLiteral( "deliberate-script-failure" );
  }
  SECTION( "exit zero without the declared raster is a failure" )
  {
    source = QStringLiteral( "result = {'note': 'no output was written'}\n" );
    expectedError = QStringLiteral( "GeoTIFF" );
  }
  SECTION( "exit zero with a fake GeoTIFF is a failure" )
  {
    source = QStringLiteral( "from pathlib import Path\n"
                             "Path(output).write_bytes(b'not a GeoTIFF')\n" );
    expectedError = QStringLiteral( "GeoTIFF" );
  }
  SECTION( "an existing destination is preserved and the script never starts" )
  {
    writeNdvi( output, { .9f, .8f, .7f, .6f, .5f, .4f, .3f, .2f, .1f, 0.f, -.1f, -.2f } );
    existingDigest = fileDigest( output );
    source = QStringLiteral( "from pathlib import Path\n"
                             "Path(parameters['marker_path']).write_text('started', encoding='utf-8')\n"
                             "raise RuntimeError('must refuse before executing')\n" );
    expectedError = QStringLiteral( "already exist" );
  }

  const QString wire = fixture.submit( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "profile_id" ), QStringLiteral( "geospatial-v1" ) },
      { QStringLiteral( "script" ), source },
      { QStringLiteral( "inputs" ), QStringList{ input.path } },
      { QStringLiteral( "output" ), output },
      { QStringLiteral( "parameters" ), QVariantMap{ { QStringLiteral( "marker_path" ), marker } } },
      { QStringLiteral( "timeout_seconds" ), 10 } } );
  const QVariantMap terminal = fixture.await( wire );
  INFO( readable( terminal ) );
  settleJob( wire );
  CHECK( terminal.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "failed" ) );
  CHECK( terminal.value( QStringLiteral( "errorMessage" ) ).toString().contains( expectedError ) );
  if ( existingDigest.isEmpty() )
    CHECK_FALSE( QFileInfo::exists( output ) );
  else
  {
    CHECK( fileDigest( output ) == existingDigest );
    CHECK_FALSE( QFileInfo::exists( marker ) );
  }
  checkNoNewAsset( fixture, input, output );
}

TEST_CASE( "Python agent requires trusted execution and a prepared environment before starting code",
           "[python][mvp][e2e][policy]" )
{
  ensureApplication();
  QTemporaryDir emptyEnvironment;
  REQUIRE( emptyEnvironment.isValid() );
  AgentFixture fixture( emptyEnvironment.path() );
  const InputEvidence input = registeredInput( fixture );
  const QString output = fixture.workspace.filePath( QStringLiteral( "refused.tif" ) );
  const QString marker = fixture.workspace.filePath( QStringLiteral( "unexpected-script-start.txt" ) );
  std::unique_ptr<ScopedEnvironment> executionPolicy;
  QString expectedError;

  SECTION( "host opt-in is absent" )
  {
    executionPolicy = std::make_unique<ScopedEnvironment>( "SICNU_PYTHON_EXECUTION", QByteArray() );
    expectedError = QStringLiteral( "trusted_local" );
  }
  SECTION( "a known profile has not been prepared" )
  {
    const QString probe = fixture.submit( QStringLiteral( "python:environment" ),
      { { QStringLiteral( "action" ), QStringLiteral( "probe" ) },
        { QStringLiteral( "profile_id" ), QStringLiteral( "geospatial-v1" ) } } );
    const QVariantMap status = fixture.await( probe );
    REQUIRE( status.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "completed" ) );
    REQUIRE_FALSE( status.value( QStringLiteral( "result" ) ).toMap()
                     .value( QStringLiteral( "ready" ) ).toBool() );
    // A source-only host may also lack its explicit bootstrap runtime.
    // Submission must preserve the readiness diagnostic and never start code.
    expectedError = status.value( QStringLiteral( "result" ) ).toMap()
                      .value( QStringLiteral( "error_message" ) ).toString();
    REQUIRE_FALSE( expectedError.isEmpty() );
  }

  const QString source = QStringLiteral( "from pathlib import Path\n"
    "Path(parameters['marker_path']).write_text('started', encoding='utf-8')\n" );
  const QString wire = fixture.submit( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "profile_id" ), QStringLiteral( "geospatial-v1" ) },
      { QStringLiteral( "script" ), source },
      { QStringLiteral( "inputs" ), QStringList{ input.path } },
      { QStringLiteral( "output" ), output },
      { QStringLiteral( "parameters" ), QVariantMap{ { QStringLiteral( "marker_path" ), marker } } },
      { QStringLiteral( "timeout_seconds" ), 10 } } );
  const QVariantMap terminal = fixture.await( wire );
  INFO( readable( terminal ) );
  settleJob( wire );
  CHECK( terminal.value( QStringLiteral( "status" ) ).toString() == QLatin1String( "failed" ) );
  CHECK( terminal.value( QStringLiteral( "errorMessage" ) ).toString()
           .contains( expectedError, Qt::CaseInsensitive ) );
  CHECK_FALSE( QFileInfo::exists( output ) );
  CHECK_FALSE( QFileInfo::exists( marker ) );
  checkNoNewAsset( fixture, input, output );
}

TEST_CASE( "Python agent never automatically replays a script after an uncertain worker completion",
           "[python][mvp][retry]" )
{
  ensureApplication();
  sicnu::processing::AtomicAlgorithmRegistry::instance().initialize();
  auto &center = sicnu::TaskCenter::instance();
  struct RetryPolicyScope
  {
    sicnu::TaskCenter &center;
    int previousLimit;
    long taskId = -1;
    ~RetryPolicyScope()
    {
      if ( taskId > 0 && !sicnu::isTerminalStatus( center.getTaskInfo( taskId ).status ) )
        center.cancelTask( taskId );
      center.setMaxAutoRetries( previousLimit );
    }
  } policy{ center, center.maxAutoRetries() };
  center.setMaxAutoRetries( 1 );
  QTemporaryDir workspace;
  REQUIRE( workspace.isValid() );
  policy.taskId = center.enqueueTask( QStringLiteral( "python:run_script" ),
    { { QStringLiteral( "script" ), QStringLiteral( "raise RuntimeError('tracking-only test must not execute')" ) },
      { QStringLiteral( "output" ), workspace.filePath( QStringLiteral( "never.tif" ) ) } },
    false, sicnu::TaskPriority::Normal, {}, /*autoDispatch=*/false );
  REQUIRE( policy.taskId > 0 );
  center.markTaskRunning( policy.taskId );
  REQUIRE( center.getTaskInfo( policy.taskId ).status == sicnu::TaskStatus::Running );
  center.markTaskFailed( policy.taskId, QStringLiteral( "worker crashed: completion reply lost" ) );
  const auto failed = center.getTaskInfo( policy.taskId );
  CHECK( failed.status == sicnu::TaskStatus::Failed );
  CHECK( failed.autoRetryAttempts == 0 );
  CHECK_FALSE( QFileInfo::exists( workspace.filePath( QStringLiteral( "never.tif" ) ) ) );
}
