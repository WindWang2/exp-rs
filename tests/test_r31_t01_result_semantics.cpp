#include <catch2/catch_test_macros.hpp>

#include "jobs/job_engine.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_schema.h"
#include "operators/io/io_operators.h"
#include "processing/framework/atomic_algorithm_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_plane.h"
#include "processing/framework/task_center.h"
#include "processing/framework/tool_call_dispatcher.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include "data/data_manager.h"

namespace
{

QCoreApplication &ensureApp()
{
  static int argc = 1;
  static char name[] = "test_r31_t01_result_semantics";
  static char *argv[] = { name, nullptr };
  static QCoreApplication *app = new QCoreApplication( argc, argv );
  return *app;
}

class MixedReportOperator final : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "r31:mixed_report"; }

    Json::Value schema() const override
    {
      return sicnu::operators::schema::makeRootSchema(
        "Mixed report", "Value fields only; no declared file artifact", Json::Value(), Json::Value() );
    }

    Json::Value run( const Json::Value &, sicnu::operators::RSOperatorContext & ) override
    {
      Json::Value result( Json::objectValue );
      result["driver"] = "GTiff";
      result["kind"] = "raster";
      result["path"] = "/tmp/source-that-must-not-be-published.tif";
      result["digest"] = "0123456789abcdef";
      result["output"] = "verification report, not an artifact";
      return result;
    }
};

class MultiOutputOperator final : public sicnu::operators::RSOperator
{
  public:
    std::string name() const override { return "r31:multi_output"; }

    Json::Value schema() const override
    {
      using namespace sicnu::operators::schema;
      Json::Value outputs( Json::objectValue );
      outputs["output"] = makeOutputParam( "output", "Primary raster", "tif" );
      outputs["confidence"] = makeOutputParam( "confidence", "Confidence raster", "tif" );
      return makeRootSchema( "Two artifacts", "Two declared file ports", Json::Value(), outputs );
    }

    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext & ) override
    {
      Json::Value result( Json::objectValue );
      result["output"] = params["output"];
      result["confidence"] = params["confidence"];
      return result;
    }
};

class SchemaFixtureOperator final : public sicnu::operators::RSOperator
{
  public:
    explicit SchemaFixtureOperator( const Json::Value &schema ) : mSchema( schema ) {}
    std::string name() const override { return "r31:legacy_producer"; }
    Json::Value schema() const override { return mSchema; }
    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &context ) override
    {
      sicnu::operators::io::IoTranslateOperator producer;
      return producer.run( params, context );
    }

  private:
    Json::Value mSchema;
};

void writeTestRaster( const QString &path )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH dataset = GDALCreate( driver, path.toUtf8().constData(), 2, 2, 1,
                                     GDT_Float32, nullptr );
  REQUIRE( dataset != nullptr );
  const float pixels[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
  REQUIRE( GDALRasterIO( GDALGetRasterBand( dataset, 1 ), GF_Write, 0, 0, 2, 2,
                         const_cast<float *>( pixels ), 2, 2, GDT_Float32, 0, 0 ) == CE_None );
  double transform[6] = { 10.0, 1.0, 0.0, 20.0, 0.0, -1.0 };
  REQUIRE( GDALSetGeoTransform( dataset, transform ) == CE_None );
  REQUIRE( GDALSetProjection( dataset, "EPSG:4326" ) == CE_None );
  GDALClose( dataset );
}

QByteArray fileDigest( const QString &path )
{
  QFile file( path );
  REQUIRE( file.open( QIODevice::ReadOnly ) );
  return QCryptographicHash::hash( file.readAll(), QCryptographicHash::Sha256 ).toHex();
}

/// A GTiff carrying a synthetic SUBDATASETS domain — the container shape
/// `io:subdatasets` inventories (entries are classified from the declared
/// names; no subdataset file is opened).
void writeRasterWithSubdatasets( const QString &path )
{
  GDALAllRegister();
  GDALDriverH driver = GDALGetDriverByName( "GTiff" );
  REQUIRE( driver != nullptr );
  GDALDatasetH dataset = GDALCreate( driver, path.toUtf8().constData(), 2, 2, 1,
                                     GDT_Float32, nullptr );
  REQUIRE( dataset != nullptr );
  const char *subdatasets[] = {
    "SUBDATASET_1_NAME=NETCDF:\"synthetic.nc\":temperature",
    "SUBDATASET_1_DESC=[2x2] temperature (32-bit floating-point)",
    "SUBDATASET_2_NAME=NETCDF:\"synthetic.nc\":salinity",
    "SUBDATASET_2_DESC=[2x2] salinity (32-bit floating-point)",
    nullptr };
  REQUIRE( GDALSetMetadata( dataset, subdatasets, "SUBDATASETS" ) == CE_None );
  GDALClose( dataset );
}

QStringList directoryFiles( const QString &path )
{
  return QDir( path ).entryList( QDir::Files | QDir::NoDotAndDotDot, QDir::Name );
}

struct ExecutionReset
{
    ExecutionReset()
    {
      sicnu::TaskCenter::instance().shutdownForTests();
      sicnu::jobs::JobEngine::instance().clearExecutors();
      sicnu::processing::AtomicAlgorithmRegistry::instance().reset();
      // TaskCenter::shutdownForTests() restarts task ids at 1; the execution
      // plane's commit-once cache is keyed by task id and would otherwise
      // serve a PREVIOUS case's committed payload to the reused id.
      sicnu::processing::ExecutionPlane::instance().resetForTests();
      // ADR 0062 bridge (same wiring as app/main.cpp): the unified registry
      // becomes executable as a JobEngine fallback, so an adapter-registered
      // algorithm runs through the REAL TaskCenter → JobEngine chain.
      sicnu::jobs::JobEngine::instance().setFallbackExecutor(
        []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) {
          const auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
            req.algorithmId );
          if ( !adapter )
            throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
          sicnu::processing::ProgressCallback progressBridge;
          progressBridge = [&ctx]( int percent, const std::string &message ) {
            ctx.reportProgress( percent / 100.0, message );
          };
          return adapter->execute( req.params, progressBridge,
                                   [&ctx]() { return ctx.isCancelled(); } );
        } );
    }
    ~ExecutionReset()
    {
      sicnu::TaskCenter::instance().shutdownForTests();
      sicnu::jobs::JobEngine::instance().clearExecutors();
      sicnu::processing::ExecutionPlane::instance().resetForTests();
    }
};

} // namespace

TEST_CASE( "R31-T01 WP02: mixed value reports never become output assets",
           "[r31][t01][result_semantics][reports]" )
{
  ensureApp();
  ExecutionReset reset;
  sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<sicnu::processing::RsOperatorAdapter>(
      std::make_unique<MixedReportOperator>() ) );

  auto &center = sicnu::TaskCenter::instance();
  QStringList autoLoadPaths;
  QObject::connect( &center, &sicnu::TaskCenter::layerAutoLoadRequested,
                    [&autoLoadPaths]( const QString &path, long ) {
                      autoLoadPaths.append( path );
                    } );

  const long taskId = center.enqueueTask( QStringLiteral( "r31:mixed_report" ), {},
                                          true, sicnu::TaskPriority::Normal, {}, true );
  REQUIRE( taskId > 0 );
  const auto info = center.waitForTask( taskId, std::chrono::seconds( 30 ) );
  INFO( "errorMessage: " << info.errorMessage.toStdString() );
  for ( const QString &line : info.logBuffer )
    INFO( "log: " << line.toStdString() );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  CHECK( info.resultPayload["driver"].asString() == "GTiff" );
  CHECK( info.resultPayload["kind"].asString() == "raster" );
  CHECK( info.resultPayload["path"].asString()
         == "/tmp/source-that-must-not-be-published.tif" );
  CHECK( info.resultPayload["digest"].asString() == "0123456789abcdef" );
  CHECK( info.resultPayload["output"].asString()
         == "verification report, not an artifact" );
  CHECK( info.outputLayerPath.isEmpty() );
  CHECK( autoLoadPaths.isEmpty() );

  int commitCalls = 0;
  const auto committer = [&commitCalls]( const sicnu::AlgorithmTaskInfo &, std::string &path,
                                         std::string &, std::string &assetId ) {
    ++commitCalls;
    path = "/tmp/false-asset.tif";
    assetId = "asset-false";
    return true;
  };
  const Json::Value payload =
    sicnu::processing::ToolCallDispatcher::buildTaskResultPayload( info, committer );
  CHECK( commitCalls == 0 );
  CHECK( payload["status"].asString() == "success" );
  CHECK( payload["output"].asString() == "verification report, not an artifact" );
  CHECK_FALSE( payload.isMember( "assetId" ) );
}


TEST_CASE( "R31-T01 WP02: multiple declared outputs keep every named identity",
           "[r31][t01][result_semantics][multi_output]" )
{
  ensureApp();
  ExecutionReset reset;
  sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter(
    std::make_shared<sicnu::processing::RsOperatorAdapter>(
      std::make_unique<MultiOutputOperator>() ) );

  auto &center = sicnu::TaskCenter::instance();
  QStringList autoLoadPaths;
  QObject::connect( &center, &sicnu::TaskCenter::layerAutoLoadRequested,
                    [&autoLoadPaths]( const QString &path, long ) {
                      autoLoadPaths.append( path );
                    } );
  const QVariantMap params{
    { QStringLiteral( "output" ), QStringLiteral( "/tmp/primary.tif" ) },
    { QStringLiteral( "confidence" ), QStringLiteral( "/tmp/confidence.tif" ) },
  };
  const long taskId = center.enqueueTask( QStringLiteral( "r31:multi_output" ), params,
                                          true, sicnu::TaskPriority::Normal, {}, true );
  REQUIRE( taskId > 0 );
  const auto info = center.waitForTask( taskId, std::chrono::seconds( 30 ) );
  REQUIRE( info.status == sicnu::TaskStatus::Completed );
  CHECK( info.outputLayerPath.isEmpty() );
  CHECK( autoLoadPaths.isEmpty() );
  REQUIRE( info.resultPayload["declaredOutputs"].isObject() );
  CHECK( info.resultPayload["declaredOutputs"]["output"].asString()
         == "/tmp/primary.tif" );
  CHECK( info.resultPayload["declaredOutputs"]["confidence"].asString()
         == "/tmp/confidence.tif" );

  int commitCalls = 0;
  const auto committer = [&commitCalls]( const sicnu::AlgorithmTaskInfo &, std::string &,
                                         std::string &, std::string & ) {
    ++commitCalls;
    return true;
  };
  const Json::Value payload =
    sicnu::processing::ToolCallDispatcher::buildTaskResultPayload( info, committer );
  CHECK( commitCalls == 0 );
  CHECK( payload["status"].asString() == "success" );
  CHECK( payload["declaredOutputs"]["output"].asString() == "/tmp/primary.tif" );
  CHECK( payload["declaredOutputs"]["confidence"].asString() == "/tmp/confidence.tif" );
}


TEST_CASE( "R31-T01 WP03: IO report tools are side-effect free through the real dispatcher",
           "[r31][t01][result_semantics][io_reports]" )
{
  ensureApp();
  ExecutionReset reset;
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString input = dir.filePath( QStringLiteral( "input.tif" ) );
  writeRasterWithSubdatasets( input );
  const QByteArray beforeDigest = fileDigest( input );
  const QStringList beforeFiles = directoryFiles( dir.path() );

  sicnu::data::DataManager manager;
  const quint64 beforeGeneration = manager.catalogGeneration();
  sicnu::processing::ToolCallDispatcher dispatcher;
  dispatcher.setDataManager( &manager );

  const auto run = [&dispatcher, &input]( const std::string &name, bool legacyTolerance ) {
    Json::Value envelope( Json::objectValue );
    envelope["name"] = name;
    envelope["arguments"]["input"] = input.toStdString();
    if ( legacyTolerance )
      envelope["arguments"]["allowMissingManifest"] = true;
    return dispatcher.submitBlocking( envelope, std::chrono::seconds( 30 ) );
  };

  for ( const std::string &name : { std::string( "io:inspect" ), std::string( "io:doctor" ),
                                    std::string( "io:subdatasets" ) } )
  {
    const Json::Value payload = run( name, false );
    INFO( name << ": " << payload.toStyledString() );
    CHECK( payload["status"].asString() == "success" );
    CHECK( payload["taskId"].asInt64() > 0 );
    CHECK( sicnu::TaskCenter::instance().getTaskInfo( payload["taskId"].asInt64() )
               .outputLayerPath.isEmpty() );
    CHECK_FALSE( payload.isMember( "assetId" ) );
    CHECK( manager.assets().isEmpty() );
    CHECK( manager.catalogGeneration() == beforeGeneration );
    CHECK( directoryFiles( dir.path() ) == beforeFiles );
    CHECK( fileDigest( input ) == beforeDigest );
  }

  const Json::Value subdatasets = run( "io:subdatasets", false );
  CHECK( subdatasets["entries"].isArray() );
  CHECK( subdatasets["count"].asUInt() == 2 );

  const Json::Value verified = run( "io:verify_dataset", true );
  INFO( verified.toStyledString() );
  CHECK( verified["status"].asString() == "success" );
  CHECK( verified["verified"].isBool() );
  CHECK_FALSE( verified["verified"].asBool() );
  CHECK_FALSE( verified["manifestPresent"].asBool() );
  CHECK( verified["issues"].isArray() );
  CHECK( manager.assets().isEmpty() );
  CHECK( manager.catalogGeneration() == beforeGeneration );
  CHECK( directoryFiles( dir.path() ) == beforeFiles );
  CHECK( fileDigest( input ) == beforeDigest );
}

TEST_CASE( "R31-T01 WP03: a real raster producer still commits and reopens correctly",
           "[r31][t01][result_semantics][io_producer]" )
{
  ensureApp();
  ExecutionReset reset;
  std::string algorithmId = "io:translate";
  SECTION( "explicit produced-file output" ) {}
  SECTION( "legacy raster helper in outputs declares the produced file" )
  {
    using namespace sicnu::operators::schema;
    sicnu::operators::io::IoTranslateOperator producer;
    Json::Value schema = producer.schema();
    schema["properties"]["output"] = makeStringParam( "output", "Output raster path" );
    schema["outputs"]["output"] = makeRasterParam( "output", "Output raster path" );
    sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter(
      std::make_shared<sicnu::processing::RsOperatorAdapter>( std::make_unique<SchemaFixtureOperator>( schema ) ) );
    algorithmId = "r31:legacy_producer";
  }
  SECTION( "legacy string output retains the marked destination parameter" )
  {
    sicnu::operators::io::IoTranslateOperator producer;
    Json::Value schema = producer.schema();
    schema["outputs"]["output"] = sicnu::operators::schema::makeStringParam( "output", "Output raster path" );
    sicnu::processing::AtomicAlgorithmRegistry::instance().registerAdapter(
      std::make_shared<sicnu::processing::RsOperatorAdapter>( std::make_unique<SchemaFixtureOperator>( schema ) ) );
    algorithmId = "r31:legacy_producer";
  }
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString input = dir.filePath( QStringLiteral( "input.tif" ) );
  const QString output = dir.filePath( QStringLiteral( "output.tif" ) );
  writeTestRaster( input );
  const QByteArray beforeDigest = fileDigest( input );

  sicnu::data::DataManager manager;
  const quint64 beforeGeneration = manager.catalogGeneration();
  sicnu::processing::ToolCallDispatcher dispatcher;
  dispatcher.setDataManager( &manager );
  Json::Value envelope( Json::objectValue );
  envelope["name"] = algorithmId;
  envelope["arguments"]["input"] = input.toStdString();
  envelope["arguments"]["output"] = output.toStdString();
  envelope["arguments"]["driver"] = "GTiff";
  const Json::Value payload = dispatcher.submitBlocking( envelope, std::chrono::seconds( 30 ) );
  INFO( payload.toStyledString() );
  REQUIRE( payload["status"].asString() == "success" );
  REQUIRE( payload["output"].isString() );
  const QString committed = QString::fromStdString( payload["output"].asString() );
  CHECK( committed.endsWith( QStringLiteral( "_committed.tif" ) ) );
  REQUIRE( QFileInfo::exists( committed ) );
  CHECK( payload["assetId"].isString() );
  REQUIRE( manager.assets().size() == 1 );
  CHECK( manager.catalogGeneration() > beforeGeneration );
  const auto snapshot = manager.assets().front();
  CHECK( snapshot.source().canonicalSource == committed );
  const auto provenance = manager.provenance( snapshot.id() );
  REQUIRE( provenance.has_value() );
  CHECK( provenance->algorithmId == QString::fromStdString( algorithmId ) );
  CHECK( fileDigest( input ) == beforeDigest );

  GDALAllRegister();
  GDALDatasetH dataset = GDALOpen( committed.toUtf8().constData(), GA_ReadOnly );
  REQUIRE( dataset != nullptr );
  float actual[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
  REQUIRE( GDALRasterIO( GDALGetRasterBand( dataset, 1 ), GF_Read, 0, 0, 2, 2,
                         actual, 2, 2, GDT_Float32, 0, 0 ) == CE_None );
  CHECK( actual[0] == 1.0f );
  CHECK( actual[1] == 2.0f );
  CHECK( actual[2] == 3.0f );
  CHECK( actual[3] == 4.0f );
  double transform[6] = {};
  REQUIRE( GDALGetGeoTransform( dataset, transform ) == CE_None );
  CHECK( transform[0] == 10.0 );
  CHECK( transform[1] == 1.0 );
  CHECK( transform[3] == 20.0 );
  CHECK( transform[5] == -1.0 );
  CHECK( QString::fromUtf8( GDALGetProjectionRef( dataset ) ).contains( "4326" ) );
  GDALClose( dataset );
}

TEST_CASE( "R31-T01 WP02: legacy file schemas remain distinct from declared value reports",
           "[r31][t01][result_semantics][legacy_schema]" )
{
  using namespace sicnu::operators::schema;
  Json::Value outputs( Json::objectValue );
  outputs["raster"] = makeRasterParam( "raster", "Legacy raster output" );
  outputs["vector"] = makeVectorParam( "vector", "Legacy vector output" );
  outputs["table"] = makeStringParam( "table", "Legacy table output" );
  outputs["table"]["format"] = "table";
  outputs["table"]["SicnuFileRole"] = "input";
  outputs["text"] = makeStringParam( "text", "A report string" );
  outputs["object"]["type"] = "object";
  outputs["format_only"] = makeStringParam( "format_only", "A semantic report without a file role" );
  outputs["format_only"]["format"] = "table";
  outputs["destination"] = makeStringParam( "destination", "A produced file declared by its destination parameter" );
  Json::Value properties( Json::objectValue );
  properties["destination"] = makeOutputParam( "destination", "Destination", "csv" );
  SchemaFixtureOperator fixture( makeRootSchema( "Legacy output types", "", properties, outputs ) );
  const auto descriptor = sicnu::processing::AlgorithmDescriptorBuilder::buildFromRsOperator( fixture );
  REQUIRE( descriptor.outputs.size() == 7 );
  for ( const auto &port : descriptor.outputs )
  {
    INFO( "port: " << port.name );
    const bool file = port.name == "raster" || port.name == "vector"
                      || port.name == "table" || port.name == "destination";
    CHECK( port.producesFile == file );
    CHECK( port.toJsonSchema().isMember( "SicnuFileRole" ) == file );
  }
}
