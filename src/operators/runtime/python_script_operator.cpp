#include "python_script_operator.h"
#include "python_environment.h"
#include "exprs/external_process.h"
#include "exprs/path_policy.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <cpl_string.h>
#include <chrono>
#include <algorithm>
#include <initializer_list>
#include <memory>


#include "operators/framework/rs_operator_registry.h"

namespace sicnu::operators::python
{
namespace
{

std::string utf8( const QString &value )
{
  return value.toUtf8().toStdString();
}

QByteArray jsonBytes( const Json::Value &value )
{
  Json::StreamWriterBuilder writer;
  writer["indentation"] = "";
  return QByteArray::fromStdString( Json::writeString( writer, value ) );
}

void writeTaskFile( const QString &path, const QByteArray &bytes )
{
  QSaveFile file( path );
  if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size() || !file.commit() )
    throw RSOperatorError( ErrorCode::FileNotWritable, "Cannot write Python task file: " + utf8( path ) );
}

void validateKeys( const Json::Value &params, std::initializer_list<const char *> allowed )
{
  if ( !params.isObject() )
    throw RSOperatorError( ErrorCode::TypeMismatch, "Python task parameters must be an object" );
  for ( const auto &key : params.getMemberNames() )
  {
    const auto found = std::find_if( allowed.begin(), allowed.end(),
      [&key]( const char *candidate ) { return key == candidate; } );
    if ( found == allowed.end() )
      throw RSOperatorError( ErrorCode::InvalidParameter, "Unknown Python task parameter: " + key );
  }
}

int timeoutFor( const Json::Value &params, int fallback, int maximum )
{
  if ( !params.isMember( "timeout_seconds" ) )
    return fallback;
  if ( !params["timeout_seconds"].isInt() )
    throw RSOperatorError( ErrorCode::TypeMismatch, "timeout_seconds must be an integer" );
  const int value = params["timeout_seconds"].asInt();
  if ( value < 1 || value > maximum )
    throw RSOperatorError( ErrorCode::OutOfRange, "timeout_seconds is outside the supported range" );
  return value;
}

std::string profileFor( const Json::Value &params )
{
  if ( !params.isMember( "profile_id" ) )
    return "geospatial-v1";
  if ( !params["profile_id"].isString() || params["profile_id"].asString() != "geospatial-v1" )
    throw RSOperatorError( ErrorCode::InvalidEnumValue, "Unknown Python profile; expected geospatial-v1" );
  return params["profile_id"].asString();
}

QString taskPath( const Json::Value &value, const char *name, bool mustExist )
{
  if ( !value.isString() || value.asString().empty() )
    throw RSOperatorError( ErrorCode::MissingRequiredParameter, std::string( name ) + " must be a file path" );
  const QString path = QString::fromStdString( value.asString() );
  if ( path.contains( QChar::Null ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, std::string( name ) + " contains a null character" );
  if ( !QDir::isAbsolutePath( path ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, std::string( name ) + " must be absolute" );
  const QFileInfo info( path );
  if ( mustExist && ( !info.isFile() || !info.isReadable() ) )
    throw RSOperatorError( ErrorCode::FileNotFound, std::string( name ) + " is not a readable file" );
  if ( qEnvironmentVariableIsSet( "SICNU_MCP_WORKSPACE" )
       && !qEnvironmentVariable( "SICNU_MCP_WORKSPACE" ).isEmpty() )
  {
    const std::string workspace = exprs::PathPolicy::workspaceRoot();
    if ( workspace.empty() || !exprs::PathPolicy::resolvesInsideRoot( workspace, utf8( path ) ) )
      throw RSOperatorError( ErrorCode::PolicyRefused, std::string( name ) + " escapes the configured workspace" );
  }
  return mustExist ? info.canonicalFilePath() : QDir::cleanPath( info.absoluteFilePath() );
}

void environmentError( const Json::Value &status )
{
  ErrorCode code = ErrorCode::NotInitialized;
  if ( status["cancelled"].asBool() )
    code = ErrorCode::Cancelled;
  else if ( status["timed_out"].asBool() )
    code = ErrorCode::ExternalProcessTimeout;
  throw RSOperatorError( code, status["error_message"].asString(), status );
}

// This host-owned runner supplies a small script interface. Script statistics
// stay nested: a script cannot forge native output paths or asset identities.
constexpr auto kRunner = R"PY(import json, pathlib, runpy, sys
request = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
namespace = runpy.run_path(
    request["script"],
    init_globals={"inputs": tuple(request["inputs"]), "output": request["output"],
                  "parameters": request["parameters"], "result": {}},
    run_name="__main__")
statistics = namespace.get("result", {})
if not isinstance(statistics, dict):
    raise TypeError("result must be a JSON object")
encoded = json.dumps(statistics, ensure_ascii=True, allow_nan=False, separators=(",", ":"))
if len(encoded.encode("utf-8")) > 65536:
    raise ValueError("result exceeds the 64 KiB statistics limit")
pathlib.Path(request["statistics"]).write_text(encoded, encoding="utf-8")
)PY";

Json::Value readStatistics( const QString &path )
{
  QFile file( path );
  if ( QFileInfo( path ).isSymLink() || !QFileInfo( path ).isFile() || !file.open( QIODevice::ReadOnly ) )
    throw RSOperatorError( ErrorCode::InvalidInputData, "Python result must be a regular JSON file of at most 64 KiB" );
  const QByteArray bytes = file.read( 64 * 1024 + 1 );
  if ( bytes.size() > 64 * 1024 )
    throw RSOperatorError( ErrorCode::InvalidInputData, "Python result exceeds 64 KiB" );
  Json::CharReaderBuilder builder;
  builder["allowComments"] = false;
  builder["rejectDupKeys"] = true;
  builder["failIfExtra"] = true;
  const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  Json::Value result;
  std::string error;
  if ( !reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &result, &error ) || !result.isObject() )
    throw RSOperatorError( ErrorCode::InvalidInputData, "Python result is not a JSON object" );
  return result;
}

Json::Value validateOutput( const QString &path, RSOperatorContext &context )
{
  if ( QFileInfo( path ).isSymLink() || !QFileInfo( path ).isFile() )
    throw RSOperatorError( ErrorCode::InvalidInputData, "Python did not produce a regular GeoTIFF output" );
  ensureGdalInit();
  const auto close = []( GDALDataset *dataset ) { GDALClose( dataset ); };
  std::unique_ptr<GDALDataset, decltype( close )> dataset(
    static_cast<GDALDataset *>( GDALOpenEx( path.toUtf8().constData(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                                           nullptr, nullptr, nullptr ) ), close );
  if ( !dataset || std::string( dataset->GetDriver()->GetDescription() ) != "GTiff"
       || dataset->GetRasterXSize() <= 0 || dataset->GetRasterYSize() <= 0 || dataset->GetRasterCount() <= 0 )
    throw RSOperatorError( ErrorCode::InvalidInputData, "Python output is not an openable non-empty GeoTIFF" );
  // Discover lazy GDAL dependencies before ownership leaves this directory.
  // The existing committer publishes PAM/world-file metadata, but not external
  // pixel masks or overviews. Refuse those rather than silently deleting them.
  for ( int band = 1; band <= dataset->GetRasterCount(); ++band )
  {
    context.throwIfCancelled();
    dataset->GetRasterBand( band )->GetMaskFlags();
    dataset->GetRasterBand( band )->GetOverviewCount();
  }
  const QFileInfo primary( path );
  const QString stem = QDir( primary.absolutePath() ).filePath( primary.completeBaseName() );
  const QStringList supported = { QDir::cleanPath( primary.absoluteFilePath() ),
    QDir::cleanPath( path + QStringLiteral( ".aux.xml" ) ),
    stem + QStringLiteral( ".tfw" ), stem + QStringLiteral( ".aux" ) };
  const std::unique_ptr<char *, decltype( &CSLDestroy )> dependencies( dataset->GetFileList(), &CSLDestroy );
  for ( int index = 0; dependencies && dependencies.get()[index]; ++index )
  {
    const QString listed = QString::fromUtf8( dependencies.get()[index] );
    const QFileInfo dependency( QDir::isAbsolutePath( listed ) ? listed : QDir( primary.absolutePath() ).filePath( listed ) );
    if ( !dependency.isFile() || dependency.isSymLink() || dependency.isJunction()
         || !supported.contains( QDir::cleanPath( dependency.absoluteFilePath() ) ) )
      throw RSOperatorError( ErrorCode::InvalidInputData,
        "Python MVP requires a self-contained GeoTIFF; external masks, overviews and unsupported sidecars are not delivered" );
  }
  // Read physical data at both ends of every band, rather than trusting file
  // existence or just a directory header. Scientific checks remain task-specific.
  for ( int band = 1; band <= dataset->GetRasterCount(); ++band )
  {
    context.throwIfCancelled();
    double sample = 0;
    auto *raster = dataset->GetRasterBand( band );
    if ( raster->RasterIO( GF_Read, 0, 0, 1, 1, &sample, 1, 1, GDT_Float64, 0, 0 ) != CE_None
         || raster->RasterIO( GF_Read, dataset->GetRasterXSize() - 1, dataset->GetRasterYSize() - 1,
                              1, 1, &sample, 1, 1, GDT_Float64, 0, 0 ) != CE_None )
      throw RSOperatorError( ErrorCode::InvalidInputData, "Python GeoTIFF data is unreadable" );
  }
  Json::Value shape( Json::objectValue );
  shape["width"] = dataset->GetRasterXSize();
  shape["height"] = dataset->GetRasterYSize();
  shape["bands"] = dataset->GetRasterCount();
  return shape;
}


Json::Value pythonMetadata( const RSOperator &op )
{
  Json::Value meta = op.RSOperator::metadata();
  meta["deterministic"] = false;
  meta["task"] = "python-execution";
  meta["tags"].append( "python" );
  meta["tags"].append( "scripting" );
  return meta;
}

Json::Value pythonEstimate()
{
  Json::Value estimate( Json::objectValue );
  estimate["estimatedRamBytes"] = Json::UInt64( 1024ULL * 1024 * 1024 );
  return estimate;
}
} // namespace

std::string PythonEnvironmentOperator::description() const
{
  return "Probe or explicitly prepare geospatial-v1: NumPy, Rasterio, Pandas, SciPy, Matplotlib, PyProj, Shapely, and Windows GDAL (osgeo). "
         "Packaged applications prepare from bundled hash-locked offline wheels without system Python or a package index. "
         "Before the first script, prepare and poll get_execution_status until the result reports ready=true. "
         "Use task status/cancel for this asynchronous operation.";
}

Json::Value PythonEnvironmentOperator::schema() const
{
  using namespace schema;
  Json::Value props( Json::objectValue );
  props["profile_id"] = makeEnumParam( "profile_id", "Managed environment profile", { "geospatial-v1" }, "geospatial-v1" );
  props["action"] = makeEnumParam( "action", "Probe readiness or explicitly prepare dependencies", { "probe", "prepare" }, "probe" );
  props["timeout_seconds"] = makeIntegerParam( "timeout_seconds", "Preparation wall-clock budget", 600 );
  setRange( props["timeout_seconds"], 1, 1800 );
  Json::Value outputs( Json::objectValue );
  outputs["ready"] = makeBooleanParam( "ready", "Interpreter and locked native dependencies passed the readiness probe" );
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  return root;
}

Json::Value PythonEnvironmentOperator::metadata() const { return pythonMetadata( *this ); }
Json::Value PythonEnvironmentOperator::executionEstimate() const { return pythonEstimate(); }

Json::Value PythonEnvironmentOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  validateKeys( params, { "profile_id", "action", "timeout_seconds" } );
  context.throwIfCancelled();
  const std::string profile = profileFor( params );
  const std::string action = params.get( "action", "probe" ).asString();
  const auto cancelled = [&context] { return context.isCancelled(); };
  PythonEnvironmentManager manager;
  Json::Value status;
  if ( action == "probe" )
    status = manager.probe( profile, cancelled );
  else if ( action == "prepare" )
    status = manager.prepare( profile, cancelled, timeoutFor( params, 600, 1800 ) );
  else
    throw RSOperatorError( ErrorCode::InvalidEnumValue, "Python environment action must be probe or prepare" );
  if ( status["cancelled"].asBool() || status["timed_out"].asBool()
       || ( action == "prepare" && !status["ready"].asBool() ) )
    environmentError( status );
  status.removeMember( "success" );
  return status;
}

std::string PythonScriptOperator::description() const
{
  return "Execute inline Python source or a snapshotted .py file in a prepared geospatial-v1 environment. "
         "If unprepared, first call python:environment with action=prepare and await ready=true. "
         "Scripts import the bundled scientific packages normally; Windows GDAL is imported from osgeo. Matplotlib defaults to headless Agg. "
         "Provide exactly one of script or script_path. "
         "Requires host opt-in SICNU_PYTHON_EXECUTION=trusted_local: code runs with the host user's file/network permissions. "
         "The script receives inputs, output, parameters and result globals; only a validated GeoTIFF is delivered as an asset. "
         "One process per task, 1 GiB native memory budget, bounded logs, timeout and process-tree cancellation. "
         "External masks and overviews are rejected; use a self-contained GeoTIFF. "
         "Arbitrary scripts are not declared safe for large rasters.";
}

Json::Value PythonScriptOperator::schema() const
{
  using namespace schema;
  Json::Value props( Json::objectValue );
  props["profile_id"] = makeEnumParam( "profile_id", "Prepared environment profile", { "geospatial-v1" }, "geospatial-v1" );
  props["script_path"] = makeStringParam( "script_path", "Existing UTF-8 .py script; snapshotted before execution (maximum 128 KiB)" );
  props["script"] = makeStringParam( "script", "Inline UTF-8 Python source (maximum 128 KiB); provide exactly one of script or script_path" );
  props["script"]["maxLength"] = 128 * 1024;
  props["inputs"] = makeArrayParam( "inputs", "Existing input file paths; registered assets retain their lineage", "string", 0, 16 );
  props["output"] = makeOutputParam( "output", "New GeoTIFF destination; published only after successful validation", "tif" );
  props["parameters"]["type"] = "object";
  props["parameters"]["description"] = "JSON parameters exposed to the script (maximum 64 KiB)";
  props["timeout_seconds"] = makeIntegerParam( "timeout_seconds", "Script process wall-clock budget", 300 );
  setRange( props["timeout_seconds"], 1, 3600 );
  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeRasterParam( "output", "Committed output GeoTIFF" );
  outputs["statistics"]["type"] = "object";
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  Json::Value inlineSource( Json::objectValue );
  inlineSource["required"] = makeRequired( { "script" } );
  inlineSource["not"]["required"] = makeRequired( { "script_path" } );
  Json::Value fileSource( Json::objectValue );
  fileSource["required"] = makeRequired( { "script_path" } );
  fileSource["not"]["required"] = makeRequired( { "script" } );
  root["oneOf"].append( inlineSource );
  root["oneOf"].append( fileSource );
  return root;
}

Json::Value PythonScriptOperator::metadata() const { return pythonMetadata( *this ); }
Json::Value PythonScriptOperator::executionEstimate() const { return pythonEstimate(); }

Json::Value PythonScriptOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  validateKeys( params, { "profile_id", "script", "script_path", "inputs", "output", "parameters", "timeout_seconds" } );
  context.throwIfCancelled();
  if ( qEnvironmentVariable( "SICNU_PYTHON_EXECUTION" ) != QStringLiteral( "trusted_local" ) )
    throw RSOperatorError( ErrorCode::PolicyRefused,
      "Python scripts require host opt-in SICNU_PYTHON_EXECUTION=trusted_local; code runs with host file/network permissions" );
  const std::string profile = profileFor( params );
  const int timeout = timeoutFor( params, 300, 3600 );
  if ( params.isMember( "script" ) == params.isMember( "script_path" ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, "Provide exactly one of script or script_path" );
  const QString outputPath = taskPath( params["output"], "output", false );
  const QString suffix = QFileInfo( outputPath ).suffix().toLower();
  if ( suffix != QStringLiteral( "tif" ) && suffix != QStringLiteral( "tiff" ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, "Python MVP delivers GeoTIFF outputs (.tif or .tiff)" );
  if ( QFileInfo::exists( outputPath ) || QFileInfo( outputPath ).isSymLink() )
    throw RSOperatorError( ErrorCode::FileNotWritable, "Python output destination must not already exist" );

  QByteArray script;
  if ( params.isMember( "script" ) )
  {
    if ( !params["script"].isString() )
      throw RSOperatorError( ErrorCode::TypeMismatch, "script must be UTF-8 source text" );
    script = QByteArray::fromStdString( params["script"].asString() );
  }
  else
  {
    const QString scriptPath = taskPath( params["script_path"], "script_path", true );
    if ( QFileInfo( scriptPath ).suffix().toLower() != QStringLiteral( "py" ) )
      throw RSOperatorError( ErrorCode::InvalidParameter, "script_path must name a UTF-8 .py file" );
    QFile scriptFile( scriptPath );
    if ( !scriptFile.open( QIODevice::ReadOnly ) )
      throw RSOperatorError( ErrorCode::FileNotReadable, "Python script is not readable" );
    script = scriptFile.read( 128 * 1024 + 1 );
  }
  if ( script.isEmpty() || script.size() > 128 * 1024 )
    throw RSOperatorError( ErrorCode::InvalidInputData, "Python script must contain 1 to 128 KiB of UTF-8 source" );
  QStringDecoder utf8Decoder( QStringDecoder::Utf8 );
  const QString decoded = utf8Decoder.decode( script );
  ( void ) decoded;
  if ( utf8Decoder.hasError() )
    throw RSOperatorError( ErrorCode::InvalidInputData, "Python script is not valid UTF-8" );

  Json::Value inputs( Json::arrayValue );
  if ( params.isMember( "inputs" ) )
  {
    if ( !params["inputs"].isArray() || params["inputs"].size() > 16 )
      throw RSOperatorError( ErrorCode::InvalidParameter, "inputs must be an array of at most 16 paths" );
    for ( const auto &input : params["inputs"] )
      inputs.append( utf8( taskPath( input, "inputs", true ) ) );
  }
  const Json::Value parameters = params.get( "parameters", Json::Value( Json::objectValue ) );
  if ( !parameters.isObject() || jsonBytes( parameters ).size() > 64 * 1024 )
    throw RSOperatorError( ErrorCode::InvalidParameter, "parameters must be a JSON object of at most 64 KiB" );

  PythonEnvironmentManager manager;
  const Json::Value environment = manager.probe( profile, [&context] { return context.isCancelled(); } );
  if ( !environment["ready"].asBool() )
    environmentError( environment );
  context.throwIfCancelled();

  const QString parent = QFileInfo( outputPath ).absolutePath();
  if ( !QDir().mkpath( parent ) )
    throw RSOperatorError( ErrorCode::DirectoryNotFound, "Cannot create the Python output directory" );
  QTemporaryDir stage( QDir( parent ).filePath( QStringLiteral( ".exprs-python-XXXXXX" ) ) );
  if ( !stage.isValid() )
    throw RSOperatorError( ErrorCode::FileNotWritable, "Cannot create a private Python task directory" );
  const QString snapshot = stage.filePath( QStringLiteral( "script.py" ) );
  const QString runner = stage.filePath( QStringLiteral( "runner.py" ) );
  const QString requestPath = stage.filePath( QStringLiteral( "request.json" ) );
  const QString stagedOutput = stage.filePath( QStringLiteral( "output.tif" ) );
  const QString statisticsPath = stage.filePath( QStringLiteral( "statistics.json" ) );
  writeTaskFile( snapshot, script );
  writeTaskFile( runner, QByteArray( kRunner ) );
  Json::Value requestJson( Json::objectValue );
  requestJson["script"] = utf8( snapshot );
  requestJson["inputs"] = inputs;
  requestJson["output"] = utf8( stagedOutput );
  requestJson["parameters"] = parameters;
  requestJson["statistics"] = utf8( statisticsPath );
  writeTaskFile( requestPath, jsonBytes( requestJson ) );

  exprs::ExternalProcessRequest request;
  request.argv = { environment["interpreter"].asString(), "-I", "-B", "-u", "-X", "utf8",
                   utf8( runner ), utf8( requestPath ) };
  request.workingDirectory = utf8( stage.path() );
  request.additionalAllowedRoots = { environment["environment_directory"].asString() };
  request.timeoutSeconds = timeout;
  request.memoryLimitBytes = 1024ULL * 1024 * 1024;
  request.stdoutLimitBytes = 256 * 1024;
  request.stderrLimitBytes = 64 * 1024;
  request.isCancelled = [&context] { return context.isCancelled(); };
  for ( const char *key : { "TEMP", "TMP", "TMPDIR" } )
    request.environment[key] = utf8( stage.path() );
  for ( const char *key : { "OPENBLAS_NUM_THREADS", "OMP_NUM_THREADS", "MKL_NUM_THREADS" } )
    request.environment[key] = "1";
  request.environment["PYTHONDONTWRITEBYTECODE"] = "1";
  request.environment["MPLBACKEND"] = "Agg";
  request.environment["MPLCONFIGDIR"] = utf8( QDir( stage.path() ).filePath( QStringLiteral( ".matplotlib" ) ) );
  auto lastProgress = std::chrono::steady_clock::now();
  request.onOutput = [&context, &lastProgress]( long stdoutBytes, long stderrBytes ) {
    const auto now = std::chrono::steady_clock::now();
    if ( now - lastProgress >= std::chrono::seconds( 1 ) )
    {
      context.reportProgress( 0.5, "Python running; stdout=" + std::to_string( stdoutBytes )
                                      + ", stderr=" + std::to_string( stderrBytes ) + " bytes" );
      lastProgress = now;
    }
  };
  context.reportProgress( 0.1, "Executing Python script in " + profile );
  const exprs::ExternalProcessResult process = exprs::ExternalProcess::run( request );
  if ( !process.stdOut.empty() )
    context.logInfo( process.stdOut );
  if ( !process.stdErr.empty() )
    context.logWarning( process.stdErr );
  context.throwIfCancelled();
  if ( process.cancelled )
    throw RSOperatorError( ErrorCode::Cancelled, "Python script was cancelled" );
  if ( process.timedOut )
    throw RSOperatorError( ErrorCode::ExternalProcessTimeout, "Python script exceeded its wall-clock budget" );
  if ( process.refusedByPolicy )
    throw RSOperatorError( ErrorCode::PolicyRefused, process.error );
  if ( !process.exitedCleanly() )
  {
    Json::Value details( Json::objectValue );
    details["exit_code"] = process.exitCode;
    details["stderr"] = process.stdErr;
    details["memory_limit_bytes"] = Json::UInt64( request.memoryLimitBytes );
    std::string message = process.error;
    if ( message.empty() )
    {
      message = "Python script failed (exit " + std::to_string( process.exitCode ) + ")";
      const QString summary = QString::fromStdString( process.stdErr ).trimmed()
                                .section( QLatin1Char( '\n' ), -1 ).left( 1024 );
      if ( !summary.isEmpty() )
        message += ": " + utf8( summary );
    }
    throw RSOperatorError( ErrorCode::ExternalProcessFailed,
                           message, details );
  }
  Json::Value result( Json::objectValue );
  result["statistics"] = readStatistics( statisticsPath );
  result["raster"] = validateOutput( stagedOutput, context );
  result["output"] = utf8( outputPath );
  result["staged_output"] = utf8( stagedOutput );
  result["staging_directory"] = utf8( stage.path() );
  result["script_sha256"] = QCryptographicHash::hash( script, QCryptographicHash::Sha256 ).toHex().toStdString();
  for ( const char *key : { "profile_id", "environment_sha256", "lock_sha256", "python_version" } )
    result[key] = environment[key];
  result["stdout"] = process.stdOut;
  result["stderr"] = process.stdErr;
  result["stdout_truncated"] = process.truncatedStdout;
  result["stderr_truncated"] = process.truncatedStderr;
  context.throwIfCancelled();
  // The existing owner-thread OutputCommitter consumes staged_output. Failed
  // or cancelled scripts remove staging through RAII and never publish output.
  context.reportProgress( 1.0, "Python output validated; awaiting asset publication" );
  context.throwIfCancelled();
  stage.setAutoRemove( false );
  return result;
}

REGISTER_RS_OPERATOR( PythonEnvironmentOperator, "python:environment" )
REGISTER_RS_OPERATOR( PythonScriptOperator, "python:run_script" )
} // namespace sicnu::operators::python
