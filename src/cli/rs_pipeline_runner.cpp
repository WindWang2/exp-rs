/***************************************************************************
 * rs_pipeline_runner.cpp  —  Headless pipeline executor via TaskCenter
 ***************************************************************************/
#include "rs_pipeline_runner.h"

#include "processing/framework/task_center.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "workflow/workflow_definition.h"
#include "workflow/workflow_types.h"
#include "workflow/placeholder_grammar.h"

#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
#include "core/plugin_host.h"
#include "app/project_context.h"
#include "app/python/sicnu_app_interface.h"
#endif

#include "data/data_manager.h"
#include "data/data_asset.h"
#include "data/derivation_record.h"
#include "data/source_descriptor.h"

#include <algorithm>
#include <gdal.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTextStream>

#include <chrono>
#include <cstdlib>
#include <csignal>
#include <regex>
#include <sstream>
#include <unordered_set>

#include "jobs/job_engine.h"

namespace sicnu::cli {

// Defined here (not in main_cli.cpp) so test_pipeline_runner — which links
// this TU without the CLI entry point — resolves them (#455).
volatile sig_atomic_t g_cliInterrupted = 0;

bool cliIsInterrupted()
{
    return g_cliInterrupted != 0;
}

namespace {

// --- Input lineage resolution (#698) ---------------------------------------
// Input-side counterpart of TaskCenter's findOutputPathInParams: collects the
// step's "input"-like parameter values that are existing file paths, then
// resolves them against the DataManager catalog into (AssetId, revision)
// lineage records. Paths that do not resolve are reported in unresolvedPaths
// so provenance records them instead of dropping them silently. (Kept local to
// this TU, mirroring how providerKeyFor-style helpers are shared by copy
// across the commit pipeline.)

QStringList findPipelineInputPaths( const QVariantMap &params )
{
  QStringList paths;
  for ( auto it = params.begin(); it != params.end(); ++it )
  {
    if ( !it.key().contains( QStringLiteral( "input" ), Qt::CaseInsensitive ) )
      continue;

    const QVariant value = it.value();
    QStringList candidates;
    if ( value.userType() == qMetaTypeId<QStringList>() )
      candidates = value.toStringList();
    else
      candidates.append( value.toString() );

    for ( const QString &candidate : candidates )
    {
      const QString trimmed = candidate.trimmed();
      if ( trimmed.isEmpty() || trimmed.startsWith( QLatin1Char( '$' ) ) )
        continue;
      if ( !paths.contains( trimmed ) && QFileInfo::exists( trimmed ) )
        paths.append( trimmed );
    }
  }
  return paths;
}

struct PipelineInputLineage
{
  QVector<sicnu::data::DerivationInput> inputs;
  QStringList unresolvedPaths;
};

PipelineInputLineage resolvePipelineInputLineage( sicnu::data::DataManager *dataManager,
                                                  const QVariantMap &params )
{
  PipelineInputLineage lineage;
  const QStringList paths = findPipelineInputPaths( params );
  if ( !dataManager )
  {
    // No catalog wired: nothing can resolve. Keep every path visible.
    lineage.unresolvedPaths = paths;
    return lineage;
  }
  for ( const QString &path : paths )
  {
    const auto snapshot = dataManager->findByPath( path );
    if ( !snapshot )
    {
      lineage.unresolvedPaths.append( path );
      continue;
    }
    sicnu::data::DerivationInput input;
    input.assetId = snapshot->id();
    input.revision = snapshot->revision();
    lineage.inputs.append( input );
  }
  return lineage;
}

std::string expandEnvironmentPlaceholders( const std::string &input,
                                           const std::unordered_set<std::string> &knownStepIds )
{
  std::regex envRegex( R"(\$\{([A-Za-z0-9_]+)\})" );
  std::smatch match;
  std::string::const_iterator searchStart( input.cbegin() );
  std::string out;
  while ( std::regex_search( searchStart, input.cend(), match, envRegex ) )
  {
    out.append( searchStart, match[0].first );
    std::string varName = match[1].str();
    if ( knownStepIds.find( varName ) == knownStepIds.end() )
    {
      const char *envVal = std::getenv( varName.c_str() );
      if ( envVal )
        out.append( envVal );
      else
        out.append( match[0].str() );
    }
    else
    {
      out.append( match[0].str() );
    }
    searchStart = match[0].second;
  }
  out.append( searchStart, input.cend() );
  return out;
}

constexpr int kMaxPipelineSteps = 100;
constexpr auto kPipelinePollInterval = std::chrono::milliseconds( 10 );
constexpr auto kPipelineTimeout = std::chrono::minutes( 30 );

bool absolutePathOutsideWorkspace( const std::string &pathValue,
                                   const QString &workspaceRoot,
                                   std::string *detail )
{
  if ( pathValue.empty() )
    return false;

  QString path = QString::fromStdString( pathValue );
  if ( path.startsWith( QLatin1Char( '~' ) ) )
    path = QDir::homePath() + path.mid( 1 );

  QString workspaceCanon = QDir( workspaceRoot ).canonicalPath();
  if ( workspaceCanon.isEmpty() )
    workspaceCanon = QFileInfo( workspaceRoot ).absoluteFilePath();
  if ( workspaceCanon.isEmpty() )
    return false;

  const QFileInfo fi( path );
  QString resolved;
  if ( fi.isAbsolute() )
  {
    if ( fi.exists() )
    {
      resolved = fi.canonicalFilePath();
    }
    else
    {
      QDir parent = fi.dir();
      QString parentCanon = parent.canonicalPath();
      if ( parentCanon.isEmpty() )
        parentCanon = parent.absolutePath();
      resolved = QDir( parentCanon ).filePath( fi.fileName() );
    }
  }
  else
  {
    const QString joined = QDir( workspaceCanon ).filePath( path );
    const QFileInfo fiJoined( joined );
    if ( fiJoined.exists() )
    {
      resolved = fiJoined.canonicalFilePath();
    }
    else
    {
      QDir parent = fiJoined.dir();
      QString parentCanon = parent.canonicalPath();
      if ( parentCanon.isEmpty() )
        parentCanon = parent.absolutePath();
      resolved = QDir( parentCanon ).filePath( fiJoined.fileName() );
    }
  }

  const QString normResolved = QDir::cleanPath( resolved );
  const QString normWorkspace = QDir::cleanPath( workspaceCanon );

  if ( normResolved == normWorkspace )
    return false;
  if ( normResolved.startsWith( normWorkspace + QLatin1Char( '/' ) ) )
    return false;

  if ( detail )
    *detail = "Path outside SICNU_PIPELINE_WORKSPACE: " + pathValue;
  return true;
}

bool jsonValueOutsideWorkspace( const Json::Value &value,
                                const QString &workspaceRoot,
                                std::string *detail )
{
  if ( value.isString() )
    return absolutePathOutsideWorkspace( value.asString(), workspaceRoot, detail );

  if ( value.isArray() )
  {
    for ( Json::ArrayIndex i = 0; i < value.size(); ++i )
    {
      if ( jsonValueOutsideWorkspace( value[i], workspaceRoot, detail ) )
        return true;
    }
    return false;
  }

  if ( value.isObject() )
  {
    for ( const auto &name : value.getMemberNames() )
    {
      if ( jsonValueOutsideWorkspace( value[name], workspaceRoot, detail ) )
        return true;
    }
    return false;
  }

  return false;
}

/// Convert CLI pipeline JSON ({ steps: [ { operator, params } ] }) into a WorkflowDefinition
/// accepted by TaskCenter::submitPipeline. Sequential steps gain parent edges so the DAG
/// runner gates them; optional "id" / "$prev.output" style params still work.
bool cliJsonToWorkflowDefinition( const Json::Value &pipelineJson,
                                  sicnu::workflow::WorkflowDefinition &def,
                                  std::string *errorMessage )
{
  def = sicnu::workflow::WorkflowDefinition{};
  def.id = "cli_pipeline";
  def.title = pipelineJson.isMember( "name" ) && pipelineJson["name"].isString()
                ? pipelineJson["name"].asString()
                : "unnamed";

  const Json::Value steps = pipelineJson["steps"];
  std::unordered_set<std::string> allStepIds;
  for ( Json::ArrayIndex i = 0; i < steps.size(); ++i )
  {
    const Json::Value step = steps[i];
    std::string sid;
    if ( step.isMember( "id" ) && step["id"].isString() && !step["id"].asString().empty() )
      sid = step["id"].asString();
    else
      sid = "step_" + std::to_string( i );
    // Reject duplicate explicit ids (#634/F16): TaskCenter keys steps by id,
    // so duplicates silently collided one task per id.
    if ( !allStepIds.insert( sid ).second )
    {
      if ( errorMessage )
        *errorMessage = "Duplicate step id: " + sid;
      return false;
    }
  }

  std::string prevStepId;
  for ( Json::ArrayIndex i = 0; i < steps.size(); ++i )
  {
    const Json::Value step = steps[i];
    sicnu::workflow::StepDef stepDef;
    if ( step.isMember( "id" ) && step["id"].isString() && !step["id"].asString().empty() )
      stepDef.id = step["id"].asString();
    else
      stepDef.id = "step_" + std::to_string( i );

    stepDef.kind = sicnu::workflow::StepKind::Operator;
    stepDef.operatorId = step["operator"].asString();
    stepDef.title = stepDef.operatorId;
    if ( step.isMember( "params" ) && step["params"].isObject() )
      stepDef.params = step["params"];
    else
      stepDef.params = Json::Value( Json::objectValue );

    // Sequential dependency: step i waits for step i-1 (TaskCenter parent gating).
    if ( !prevStepId.empty() )
    {
      sicnu::workflow::StepConnection conn;
      conn.fromStepId = prevStepId;
      conn.fromPort = "output";
      conn.toPort = "input";
      stepDef.inputs.push_back( conn );
    }

    // Explicit $fromStep.output placeholders also create parent edges if missing.
    if ( stepDef.params.isObject() )
    {
      for ( const auto &key : stepDef.params.getMemberNames() )
      {
        if ( !stepDef.params[key].isString() )
          continue;
        std::string strVal = stepDef.params[key].asString();
        strVal = expandEnvironmentPlaceholders( strVal, allStepIds );
        stepDef.params[key] = strVal;

        auto inferredConns = sicnu::workflow::inferStepConnections( key, strVal );
        for ( const auto &conn : inferredConns )
        {
          if ( allStepIds.find( conn.fromStepId ) == allStepIds.end() )
            continue; // Not a declared step ID in this pipeline, do not add phantom DAG edge

          bool hasEdge = false;
          for ( const auto &c : stepDef.inputs )
          {
            if ( c.fromStepId == conn.fromStepId )
            {
              hasEdge = true;
              break;
            }
          }
          if ( !hasEdge )
          {
            stepDef.inputs.push_back( conn );
          }
        }
      }
    }

    const QString workspace = QProcessEnvironment::systemEnvironment().value(
      QStringLiteral( "SICNU_PIPELINE_WORKSPACE" ) );
    if ( !workspace.isEmpty() && stepDef.params.isObject() )
    {
      std::string detail;
      if ( jsonValueOutsideWorkspace( stepDef.params, workspace, &detail ) )
      {
        if ( errorMessage )
          *errorMessage = "Step " + stepDef.id + ": " + detail;
        return false;
      }
    }

    def.steps.push_back( stepDef );
    prevStepId = stepDef.id;
  }

  if ( def.steps.empty() )
  {
    if ( errorMessage )
      *errorMessage = "Pipeline has no operator steps";
    return false;
  }
  return true;
}

} // namespace

RsPipelineRunner::RsPipelineRunner( ProgressCallback progressCallback,
                                    LogCallback logCallback )
  : m_progressCallback( std::move( progressCallback ) )
  , m_logCallback( std::move( logCallback ) )
{
}

RsPipelineRunner::~RsPipelineRunner() = default;

bool RsPipelineRunner::addPythonPluginDirectory( const std::string &dirPath, std::string *errorOut )
{
  if ( dirPath.empty() )
  {
    if ( errorOut )
      *errorOut = "Directory path is empty";
    return false;
  }
  if ( !QDir( QString::fromStdString( dirPath ) ).exists() )
  {
    if ( errorOut )
      *errorOut = "Plugin directory does not exist: " + dirPath;
    return false;
  }
  // Deduplicate (#634): repeated --python-plugin flags reloaded the same
  // plugin once per occurrence.
  if ( std::find( m_pythonPluginDirs.begin(), m_pythonPluginDirs.end(), dirPath ) == m_pythonPluginDirs.end() )
    m_pythonPluginDirs.push_back( dirPath );
  return true;
}

bool RsPipelineRunner::ensurePythonPluginsLoaded()
{
  if ( m_pythonPluginDirs.empty() )
    return true;

  if ( m_pluginHost )
    return true;

#if defined( SICNU_EMBED_PYTHON ) && SICNU_EMBED_PYTHON
  if ( !m_dataManager )
  {
    m_ownedDataManager = std::make_unique<sicnu::data::DataManager>();
    m_dataManager = m_ownedDataManager.get();
  }

  // Headless plugin stack (ADR 0023, TICKET-14): a view-less ProjectContext, a
  // widget-free SicnuAppInterface, and the sanctioned PluginHost lifecycle owner.
  auto createdContext = sicnu::app::ProjectContext::createHeadless();
  if ( !createdContext )
  {
    reportLog( "error", "Failed to create headless project context for Python plugins" );
    return false;
  }
  m_projectContext = std::shared_ptr<sicnu::app::ProjectContext>( createdContext.take() );

  m_appInterface = std::make_shared<SicnuAppInterface>( nullptr, nullptr, m_projectContext.get() );
  m_pluginHost = std::make_shared<PluginHost>( 2 );
  m_pluginHost->setAppInterface( m_appInterface.get() );

  for ( const auto &dir : m_pythonPluginDirs )
  {
    if ( !m_pluginHost->loadPythonPlugin( QString::fromStdString( dir ) ) )
    {
      reportLog( "error", "Failed to load Python plugin '" + dir + "'" );
      return false;
    }
    reportLog( "info", "Loaded Python plugin: " + dir );
  }
  return true;
#else
  reportLog( "error", "Python plugin support is disabled (SICNU_EMBED_PYTHON=OFF)" );
  return false;
#endif
}

RsPipelineRunner::PipelineResult RsPipelineRunner::runFromJson( const Json::Value &pipelineJson )
{
  PipelineResult result;
  result.success = false;

  if ( !ensurePythonPluginsLoaded() )
  {
    result.errorMessage = "Failed to initialize Python plugins";
    return result;
  }

  std::string validationError;
  if ( !validatePipelineJson( pipelineJson, &validationError ) )
  {
    result.errorMessage = "Invalid pipeline JSON: " + validationError;
    return result;
  }

  sicnu::workflow::WorkflowDefinition def;
  std::string convertError;
  if ( !cliJsonToWorkflowDefinition( pipelineJson, def, &convertError ) )
  {
    result.errorMessage = convertError.empty() ? "Failed to convert pipeline to WorkflowDefinition"
                                               : convertError;
    return result;
  }

  const int totalSteps = static_cast<int>( def.steps.size() );
  reportLog( "info", "Starting pipeline via TaskCenter: " + def.title +
                       " (" + std::to_string( totalSteps ) + " steps)" );

  const long pipelineId = sicnu::TaskCenter::instance().submitPipeline( def, /*autoLoad=*/false );
  if ( pipelineId < 0 )
  {
    result.errorMessage = "TaskCenter rejected the pipeline DAG";
    reportLog( "error", result.errorMessage );
    return result;
  }

  const auto deadline = std::chrono::steady_clock::now() + kPipelineTimeout;
  for ( ;; )
  {
    if ( g_cliInterrupted || cliIsInterrupted() )
    {
        sicnu::TaskCenter::instance().cancelPipeline( pipelineId );
        sicnu::TaskCenter::instance().shutdown();
        sicnu::jobs::JobEngine::instance().shutdown();
        result.errorMessage = "Pipeline interrupted by signal";
        reportLog( "error", result.errorMessage );
        result.success = false;
        return result;
    }
    // Wait for completion, waking every poll interval to emit progress.
    const auto pipeInfo = sicnu::TaskCenter::instance().waitForPipeline( pipelineId, kPipelinePollInterval );

    // Deliver marshaled py: executions (ADR 0023): the py: prefix executor
    // blocks a JobEngine worker on a BlockingQueuedConnection to this thread.
    QCoreApplication::processEvents();

    // Emit coarse progress from task states.
    int completedCount = 0;
    int runningIndex = -1;
    double runningProgress = 0.0;
    for ( int i = 0; i < totalSteps; ++i )
    {
      const std::string &stepId = def.steps[static_cast<size_t>( i )].id;
      if ( !pipeInfo.stepToTaskId.contains( stepId ) )
        continue;
      const auto info = sicnu::TaskCenter::instance().getTaskInfo( pipeInfo.stepToTaskId[stepId] );
      if ( info.status == sicnu::TaskStatus::Completed )
        ++completedCount;
      else if ( info.status == sicnu::TaskStatus::Running )
      {
        runningIndex = i;
        runningProgress = info.progressPercentage;
      }
    }
    if ( runningIndex >= 0 )
    {
      reportProgress( runningIndex, totalSteps, runningProgress,
                      "Running " + def.steps[static_cast<size_t>( runningIndex )].operatorId );
    }
    else if ( completedCount > 0 && completedCount < totalSteps )
    {
      reportProgress( completedCount, totalSteps, 0.0, "Waiting for next step" );
    }

    if ( pipeInfo.isCompleted || pipeInfo.isFailed )
    {
      result.steps.clear();
      result.steps.reserve( static_cast<size_t>( totalSteps ) );
      bool anyFailed = pipeInfo.isFailed;

      for ( int i = 0; i < totalSteps; ++i )
      {
        const auto &stepDef = def.steps[static_cast<size_t>( i )];
        StepResult stepResult;
        stepResult.operatorName = stepDef.operatorId;
        stepResult.params = stepDef.params;

        if ( !pipeInfo.stepToTaskId.contains( stepDef.id ) )
        {
          stepResult.success = false;
          stepResult.errorMessage = "Step was not scheduled by TaskCenter";
          anyFailed = true;
          result.steps.push_back( stepResult );
          continue;
        }

        const long taskId = pipeInfo.stepToTaskId[stepDef.id];
        const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
        stepResult.params = stepDef.params; // may have been substituted on the task
        // Prefer live substituted params from the task when available.
        if ( !info.parameterMap.isEmpty() )
        {
          // Type-aware conversion (#647): stringifying every QVariant turned
          // numeric params into "512"-style strings and list params into
          // empty strings, misreporting what actually ran.
          Json::Value liveParams( Json::objectValue );
          for ( auto it = info.parameterMap.begin(); it != info.parameterMap.end(); ++it )
          {
            const QVariant &value = it.value();
            switch ( value.typeId() )
            {
              case QMetaType::Bool:
                liveParams[it.key().toStdString()] = value.toBool();
                break;
              case QMetaType::Int:
              case QMetaType::LongLong:
              case QMetaType::UInt:
              case QMetaType::ULongLong:
                liveParams[it.key().toStdString()] = Json::Int64( value.toLongLong() );
                break;
              case QMetaType::Double:
              case QMetaType::Float:
                liveParams[it.key().toStdString()] = value.toDouble();
                break;
              case QMetaType::QStringList:
              case QMetaType::QVariantList:
              {
                Json::Value arr( Json::arrayValue );
                for ( const QVariant &item : value.toList() )
                  arr.append( item.toString().toStdString() );
                liveParams[it.key().toStdString()] = arr;
                break;
              }
              default:
                liveParams[it.key().toStdString()] = value.toString().toStdString();
                break;
            }
          }
          stepResult.params = liveParams;
        }

        if ( info.status == sicnu::TaskStatus::Completed )
        {
          stepResult.success = true;
          stepResult.result = info.resultPayload.isNull() ? Json::Value( Json::objectValue )
                                                          : info.resultPayload;
          reportProgress( i, totalSteps, 1.0, "Finished " + stepDef.operatorId );
        }
        else if ( sicnu::isTerminalStatus( info.status ) )
        {
          stepResult.success = false;
          stepResult.errorMessage = info.errorMessage.toStdString();
          if ( stepResult.errorMessage.empty() )
            stepResult.errorMessage = "Step failed";
          // Normalize JobEngine wording for CLI tests / users.
          if ( stepResult.errorMessage.find( "Unknown algorithm" ) != std::string::npos )
            stepResult.errorMessage = "Operator not registered: " + stepDef.operatorId;
          anyFailed = true;
          result.errorMessage = "Step " + std::to_string( i + 1 ) + " (" + stepDef.operatorId +
                                ") failed: " + stepResult.errorMessage;
          reportLog( "error", result.errorMessage );
        }
        else
        {
          // Still in flight (Queued/Running/Cancelling/...) when a sibling
          // failed and the wait loop exited early: the cancelPipeline below
          // kills these. Report them as canceled by the sibling failure -
          // not as failed - and leave result.errorMessage to the real
          // failure reported above.
          stepResult.success = false;
          stepResult.errorMessage = "Canceled: sibling step failed while this step was still in flight";
          reportLog( "warn", "Step " + std::to_string( i + 1 ) + " (" + stepDef.operatorId +
                             ") " + stepResult.errorMessage );
        }
        result.steps.push_back( stepResult );
      }

      if ( anyFailed )
      {
        sicnu::TaskCenter::instance().cancelPipeline( pipelineId );
        if ( result.errorMessage.empty() )
          result.errorMessage = pipeInfo.errorMessage.isEmpty()
                                  ? "Pipeline failed"
                                  : pipeInfo.errorMessage.toStdString();
        result.success = false;
        return result;
      }

      result.success = true;
      if ( m_dataManager && result.success )
      {
        registerStepOutputs( pipelineId );
      }
      reportLog( "info", "Pipeline completed successfully: " + def.title );
      return result;
    }

    if ( std::chrono::steady_clock::now() > deadline )
    {
      sicnu::TaskCenter::instance().cancelPipeline( pipelineId );
      result.errorMessage = "Pipeline timed out waiting for TaskCenter";
      reportLog( "error", result.errorMessage );
      return result;
    }
  }
}

void RsPipelineRunner::setAssetRegistry( sicnu::data::DataManager *dataManager )
{
  m_dataManager = dataManager;
}

void RsPipelineRunner::registerStepOutputs( long pipelineId )
{
  if ( !m_dataManager )
    return;

  auto &taskCenter = sicnu::TaskCenter::instance();
  const auto pipeInfo = taskCenter.getPipelineInfo( pipelineId );

  for ( const auto &stepId : pipeInfo.orderedStepIds )
  {
    if ( !pipeInfo.stepToTaskId.contains( stepId ) )
      continue;
    const long taskId = pipeInfo.stepToTaskId[stepId];
    const auto task = taskCenter.getTaskInfo( taskId );
    if ( task.status != sicnu::TaskStatus::Completed || task.outputLayerPath.isEmpty() )
      continue;

    const QString path = task.outputLayerPath;
    ensureGdalInit();
    GDALDatasetH ds = GDALOpenEx( path.toUtf8().constData(),
                                 GDAL_OF_READONLY | GDAL_OF_RASTER | GDAL_OF_VECTOR,
                                 nullptr, nullptr, nullptr );
    if ( !ds )
    {
      reportLog( "warning", "Skipping asset registration; output not openable: " +
                            path.toStdString() );
      continue;
    }
    const bool isRaster = GDALGetRasterCount( ds ) > 0;
    GDALClose( ds );

    // ADR 0023: user-declared final output paths are registered in place as
    // TaskTemporary assets — no temp->stable move, no DeletableSource ownership.
    sicnu::data::SourceDescriptor source;
    source.providerKey = isRaster ? QStringLiteral( "gdal" ) : QStringLiteral( "ogr" );
    source.canonicalSource = path;

    sicnu::data::RegisterRequest request;
    request.source = source;
    request.persistence = sicnu::data::PersistencePolicy::TaskTemporary;

    const auto registered = m_dataManager->registerSource( request );
    if ( registered.assetId.isNull() )
    {
      reportLog( "warning", "Asset registration failed for: " + path.toStdString() );
      continue;
    }

    // Provenance is attached after successful registration (ADR 0023). The
    // step's "input"-like parameter paths are resolved against the catalog so
    // the record carries real derivedFrom edges (#698); paths that do not
    // resolve are recorded in unresolvedInputPaths, never dropped silently.
    const PipelineInputLineage lineage =
      resolvePipelineInputLineage( m_dataManager, task.parameterMap );
    const sicnu::data::DerivationRecord derivation =
      sicnu::data::makeTaskDerivation( task.algorithmId,
                                       QJsonObject::fromVariantMap( task.parameterMap ),
                                       QString::number( taskId ),
                                       lineage.inputs,
                                       lineage.unresolvedPaths );
    m_dataManager->attachDerivationRecord( registered.assetId, derivation );
  }
}

RsPipelineRunner::PipelineResult RsPipelineRunner::runFromFile( const std::string &filePath )
{
  PipelineResult result;
  result.success = false;

  QFile file( QString::fromStdString( filePath ) );
  if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
  {
    result.errorMessage = "Cannot open pipeline file: " + filePath;
    return result;
  }

  QTextStream in( &file );
  const QString jsonText = in.readAll();

  Json::Value root;
  Json::CharReaderBuilder readerBuilder;
  std::string parseError;
  std::istringstream jsonStream( jsonText.toStdString() );

  if ( !Json::parseFromStream( readerBuilder, jsonStream, &root, &parseError ) )
  {
    result.errorMessage = "JSON parse error in " + filePath + ": " + parseError;
    return result;
  }

  return runFromJson( root );
}

bool RsPipelineRunner::validatePipelineJson( const Json::Value &pipelineJson,
                                             std::string *errorMessage )
{
  if ( !pipelineJson.isObject() )
  {
    if ( errorMessage )
      *errorMessage = "Root must be a JSON object";
    return false;
  }

  if ( !pipelineJson.isMember( "steps" ) || !pipelineJson["steps"].isArray() )
  {
    if ( errorMessage )
      *errorMessage = "Missing or invalid 'steps' array";
    return false;
  }

  const Json::Value steps = pipelineJson["steps"];
  if ( static_cast<int>( steps.size() ) > kMaxPipelineSteps )
  {
    if ( errorMessage )
    {
      *errorMessage = "Pipeline exceeds maximum of " + std::to_string( kMaxPipelineSteps )
                      + " steps (got " + std::to_string( steps.size() ) + ")";
    }
    return false;
  }

  std::unordered_set<std::string> seenStepIds;
  for ( Json::ArrayIndex i = 0; i < steps.size(); ++i )
  {
    const Json::Value step = steps[i];
    if ( !step.isObject() )
    {
      if ( errorMessage )
        *errorMessage = "Step " + std::to_string( i ) + " is not an object";
      return false;
    }
    if ( !step.isMember( "operator" ) || !step["operator"].isString() )
    {
      if ( errorMessage )
        *errorMessage = "Step " + std::to_string( i ) + " missing 'operator' string";
      return false;
    }
    if ( step.isMember( "params" ) && !step["params"].isObject() )
    {
      if ( errorMessage )
        *errorMessage = "Step " + std::to_string( i ) + " 'params' is not an object";
      return false;
    }
    // Workspace path containment is enforced post-expansion in
    // cliJsonToWorkflowDefinition (after ${VAR} expansion and CWD
    // resolution) — pre-expansion checks here would be bypassable
    // (issue #313) and are intentionally omitted.

    // Duplicate step ids must fail validation, not just conversion: the
    // conversion-time check alone lets an invalid pipeline pass validation
    // and surface only at submit (#634).
    std::string sid;
    if ( step.isMember( "id" ) && step["id"].isString() && !step["id"].asString().empty() )
      sid = step["id"].asString();
    else
      sid = "step_" + std::to_string( i );
    if ( !seenStepIds.insert( sid ).second )
    {
      if ( errorMessage )
        *errorMessage = "Duplicate step id: " + sid;
      return false;
    }
  }
  return true;
}

void RsPipelineRunner::reportProgress( int stepIndex, int totalSteps, double stepProgress,
                                       const std::string &message ) const
{
  if ( m_progressCallback )
    m_progressCallback( stepIndex, totalSteps, stepProgress, message );
}

void RsPipelineRunner::reportLog( const std::string &level, const std::string &message ) const
{
  if ( m_logCallback )
    m_logCallback( level, message );
}

} // namespace sicnu::cli
