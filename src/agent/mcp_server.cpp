#include "mcp_server.h"
#include "core/sicnu_logging.h"
#include "env_flag.h"

#include <optional>
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/asset_types.h"
#include "data/derivation_record.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/algorithm_meta_store.h"
#include "processing/framework/json_params_converter.h"
#include "processing/framework/algorithm_preflight.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "shell/processing_job_adapter.h"
#include "interaction_tool_registry.h"
#include "agent/tool_catalog/agent_tool_catalog.h"
#include "agent/tool_catalog/agent_tool.h"
#include "agent/spatial_tools/spatial_tool.h"

#include <iostream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <qgis.h>
#include <json/json.h>

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsvectordataprovider.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingalgorithm.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsrectangle.h>
#include <qgsfields.h>

namespace {

/// Structured MCP tool error that preserves machine-readable codes in the
/// JSON-RPC error.data field without losing the human-facing message.
/// Derives from std::runtime_error so existing `catch (std::runtime_error&)`
/// handlers in tests and callers keep compiling.
struct McpToolError : public std::runtime_error
{
    QString errorCode;
    QString errorCategory;
    int rpcCode = -32000;
    bool retryable = false;

    McpToolError( const QString &msg, const QString &code = QString(), const QString &category = QString(), int rpc = -32000, bool retryable_ = false )
      : std::runtime_error( msg.toUtf8().constData() )
      , errorCode( code )
      , errorCategory( category )
      , rpcCode( rpc )
      , retryable( retryable_ )
    {
    }
};

QString mcpDataTypeToString( Qgis::DataType type ) {
    switch ( type ) {
        case Qgis::DataType::UnknownDataType: return QStringLiteral("Unknown");
        case Qgis::DataType::Byte: return QStringLiteral("Byte");
        case Qgis::DataType::Int8: return QStringLiteral("Int8");
        case Qgis::DataType::UInt16: return QStringLiteral("UInt16");
        case Qgis::DataType::Int16: return QStringLiteral("Int16");
        case Qgis::DataType::UInt32: return QStringLiteral("UInt32");
        case Qgis::DataType::Int32: return QStringLiteral("Int32");
        case Qgis::DataType::Float32: return QStringLiteral("Float32");
        case Qgis::DataType::Float64: return QStringLiteral("Float64");
        case Qgis::DataType::CInt16: return QStringLiteral("CInt16");
        case Qgis::DataType::CInt32: return QStringLiteral("CInt32");
        case Qgis::DataType::CFloat32: return QStringLiteral("CFloat32");
        case Qgis::DataType::CFloat64: return QStringLiteral("CFloat64");
        case Qgis::DataType::ARGB32: return QStringLiteral("ARGB32");
        case Qgis::DataType::ARGB32_Premultiplied: return QStringLiteral("ARGB32_Premultiplied");
        default: return QStringLiteral("Unknown");
    }
}

bool idHasAllowedPrefix(const QString &id, bool *isCustomTools = nullptr)
{
    if (isCustomTools)
        *isCustomTools = false;

    QString checkId = id;
    if (checkId.startsWith(QStringLiteral("processing:"))) {
        checkId = checkId.mid(11);
    }

    static const QStringList kAllowed = {
        QStringLiteral("rs:"),
        QStringLiteral("gdal:"),
        QStringLiteral("gdal_tools:"),
        QStringLiteral("otb:"),
        QStringLiteral("otb_tools:"),
        QStringLiteral("qgis:"),
        QStringLiteral("qgis_algorithms:"),
        QStringLiteral("opencv:"), // operator surface uses opencv: filters
        QStringLiteral("view:"),   // agent interaction view tools
        QStringLiteral("roi:"),    // agent interaction roi tools
        QStringLiteral("canvas:"), // agent interaction canvas tools
        QStringLiteral("layer:"),  // agent interaction layer tools
        QStringLiteral("raster:"), // agent raster display tools
        QStringLiteral("data:"),   // data manager tools
        QStringLiteral("spatial:"), // spatial inspection/catalog tools (ADR 0122)
    };
    for (const QString &prefix : kAllowed) {
        if (checkId.startsWith(prefix))
            return true;
    }
    if (id.startsWith(QStringLiteral("custom_tools:"))) {
        if (isCustomTools)
            *isCustomTools = true;
        return false;
    }
    return false;
}

/// Resolve a path for workspace checks. Relative paths are allowed without check.
/// Absolute paths must canonicalize under workspace root.
bool absolutePathOutsideWorkspace(const QString &pathValue, const QString &workspaceRoot, QString *detail)
{
    if (pathValue.isEmpty())
        return false;

    QString path = pathValue;
    if (path.startsWith(QLatin1Char('~'))) {
        path = QDir::homePath() + path.mid(1);
    }

    QString workspaceCanon = QDir(workspaceRoot).canonicalPath();
    if (workspaceCanon.isEmpty())
        workspaceCanon = QFileInfo(workspaceRoot).absoluteFilePath();
    if (workspaceCanon.isEmpty())
        return false;

    const QFileInfo fi(path);
    QString resolved;
    if (fi.isAbsolute()) {
        if (fi.exists()) {
            resolved = fi.canonicalFilePath();
        } else {
            // Non-existent output path: resolve parent dir + filename
            QDir parent = fi.dir();
            QString parentCanon = parent.canonicalPath();
            if (parentCanon.isEmpty())
                parentCanon = parent.absolutePath();
            resolved = QDir(parentCanon).filePath(fi.fileName());
        }
    } else {
        const QString joined = QDir(workspaceCanon).filePath(path);
        const QFileInfo fiJoined(joined);
        if (fiJoined.exists()) {
            resolved = fiJoined.canonicalFilePath();
        } else {
            QDir parent = fiJoined.dir();
            QString parentCanon = parent.canonicalPath();
            if (parentCanon.isEmpty())
                parentCanon = parent.absolutePath();
            resolved = QDir(parentCanon).filePath(fiJoined.fileName());
        }
    }

    const QString normResolved = QDir::cleanPath(resolved);
    const QString normWorkspace = QDir::cleanPath(workspaceCanon);

    if (normResolved == normWorkspace)
        return false;
    if (normResolved.startsWith(normWorkspace + QLatin1Char('/')))
        return false;

    if (detail) {
        *detail = QStringLiteral("Path outside SICNU_MCP_WORKSPACE: %1").arg(pathValue);
    }
    return true;
}

bool collectOutsideWorkspace(const QVariant &value, const QString &workspaceRoot, QString *detail)
{
    if (value.userType() == QMetaType::QString) {
        return absolutePathOutsideWorkspace(value.toString(), workspaceRoot, detail);
    }
    if (value.userType() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        for (const QVariant &item : list) {
            if (collectOutsideWorkspace(item, workspaceRoot, detail))
                return true;
        }
        return false;
    }
    if (value.userType() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            if (collectOutsideWorkspace(it.value(), workspaceRoot, detail))
                return true;
        }
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Meta-tool catalog for tools/list. Tool names, descriptions, and inputSchema
// are the client protocol contract (ADR 0022): the table below mirrors the
// previous construction — same names, descriptions, property types, and
// required-input sets.
// ---------------------------------------------------------------------------
struct MetaToolSchemaInput {
    const char *name;
    const char *type;
    const char *description;
    bool required;
};

struct MetaToolDef {
    const char *name;
    const char *description;
    QList<MetaToolSchemaInput> inputs;
};

const MetaToolDef kMetaTools[] = {
    { "list_algorithms",
      "List all available remote sensing and GIS processing algorithms (canonical "
      "catalog: rs: operators + provider algorithms). Compact discovery layer — "
      "id, name, group, tags, outputs, memory policy. Use get_algorithm_schema "
      "for the full parameter schema of a specific algorithm.",
      {} },
    { "search_algorithms",
      "Search/filter the canonical algorithm catalog by group, tag, purpose text, "
      "input or output type, or large-raster safety. Returns the same compact "
      "entries as list_algorithms.",
      { { "query", "string", "Free-text filter matched against id, name, group and purpose (case-insensitive). Empty = no text filter.", false },
        { "group", "string", "Exact group filter (e.g. 'spectral', 'change detection'). Optional.", false },
        { "input_type", "string", "Input data type filter (Raster/Vector/Table/Numeric/Integer/String/Boolean/Json). Optional.", false },
        { "output_type", "string", "Output data type filter (Raster/Vector/Table/Numeric/Integer/String/Boolean/Json). Optional.", false },
        { "large_raster_safe", "boolean", "When true, only streaming/multipass operators. Optional.", false } } },
    { "get_algorithm_schema",
      "Get the detailed input parameter JSON Schema, real output ports, and agent "
      "metadata for a specific algorithm.",
      { { "algorithm_id", "string", "Unique ID of the algorithm, e.g., 'rs:spectral_index'", true } } },
    { "preflight_algorithm",
      "Validate parameters and dataset compatibility WITHOUT executing: schema "
      "validation, raster dataset probes (size/bands/CRS/radiometric state), "
      "same-grid/CRS/band/radiometric checks, and a dynamic resource (RAM) "
      "estimate. Use before execute_algorithm to plan a run.",
      { { "algorithm_id", "string", "ID of the algorithm to preflight", true },
        { "parameters", "object", "Planned parameter name-value pairs", false } } },
    { "execute_algorithm",
      "Asynchronously run a processing algorithm with the specified parameters.",
      { { "algorithm_id", "string", "ID of the algorithm to execute", true },
        { "parameters", "object", "Parameter name-value pairs for the algorithm", false } } },
    { "get_execution_status",
      "Get progress, execution status, results, and the committed asset id of an "
      "ongoing or completed algorithm execution.",
      { { "execution_id", "string", "The execution ID returned by execute_algorithm", true } } },
    { "cancel_execution",
      "Cancel an actively running algorithm execution.",
      { { "execution_id", "string", "The execution ID of the run to cancel", true } } },
    { "list_operators",
      "List all registered RSOperator algorithms (legacy Agent surface: rs:/opencv:/gdal:/otb:).",
      {} },
    { "get_operator_schema",
      "Get JSON Schema and metadata for an RSOperator (e.g. 'rs:spectral_index').",
      { { "operator_id", "string", "Operator id, e.g. 'rs:spectral_index'", true } } },
    { "execute_operator",
      "Asynchronously run an RSOperator with JSON parameters. Returns execution_id.",
      { { "operator_id", "string", "Operator id to execute", true },
        { "parameters", "object", "JSON parameter object", false } } },
    { "list_layers",
      "List all raster and vector layers loaded in the current QGIS project.",
      {} },
    { "describe_dataset",
      "Get detailed layer metadata, including spatial extent, coordinate reference system (CRS), and band/field details.",
      { { "layer_id", "string", "Name or ID of the layer to describe", true } } },
    { "get_lineage",
      "Query a Data Manager asset's processing provenance and lineage: the "
      "deriving algorithm + parameters when the asset was produced, its input "
      "assets (derivedFrom), and any assets derived from it (derivedOutputsOf).",
      { { "asset_id", "string", "Data Manager asset id (UUID) to query", true } } },
    { "list_interaction_tools",
      "List all interactive GIS tools (view controls, layer navigation, canvas ROI).",
      {} },
    { "get_interaction_schema",
      "Get JSON Schema and parameters for an interaction tool (e.g. 'view:set_extent', 'roi:set').",
      { { "tool_name", "string", "Interaction tool name (e.g. 'view:get_state', 'view:set_extent', 'roi:set')", true } } },
    { "list_tools",
      "List all unified agent tools (Processing algorithms, Interaction/Canvas tools, Data tools). "
      "Returns category, name, and description per tool; input schemas are omitted by default "
      "(pull a candidate's full schema via get_tool_schema). Pass compact=false to embed every schema.",
      { { "category", "string", "Optional category filter: 'Processing', 'Interaction', 'Data', 'Custom'.", false },
        { "compact", "boolean", "Set false to embed per-entry input schemas (default: omitted).", false },
        { "limit", "integer", "Max entries per page (offset pagination). 0 = all. Optional.", false },
        { "cursor", "integer", "Offset of the first entry to return; pass nextCursor from the previous page. Optional.", false } } },
    { "search_tools",
      "Search unified agent tools by free text (e.g. 'show raster', 'roi', 'spectral'), group, tag, or input/output type.",
      { { "query", "string", "Free-text filter matched against name, group, purpose, tags, and description.", false },
        { "group", "string", "Exact or substring group filter. Optional.", false },
        { "tag", "string", "Tag filter. Optional.", false },
        { "input_type", "string", "Input data type filter. Optional.", false },
        { "output_type", "string", "Output data type filter. Optional.", false },
        { "compact", "boolean", "Set false to embed per-entry input schemas (default: omitted).", false },
        { "limit", "integer", "Max entries per page (offset pagination). 0 = all. Optional.", false },
        { "cursor", "integer", "Offset of the first entry to return; pass nextCursor from the previous page. Optional.", false } } },
    { "get_tool_schema",
      "Get parameter JSON Schema and metadata for any registered tool in the unified Agent Tool Catalog.",
      { { "tool_id", "string", "Unique ID of the tool, e.g. 'rs:spectral_index', 'canvas:draw_roi', 'data:list_layers'", true } } },
    { "run_workflow",
      "Submit an agent-generated spatial workflow (DAG) as pipeline JSON and execute it "
      "through the Task Center: steps reference registered operators (e.g. "
      "'rs:spectral_index'), connections declare the execution order, and upstream "
      "outputs flow into downstream inputs. Returns the pipeline id and one "
      "execution_id per step — poll them with get_execution_status or use "
      "get_workflow_status for the aggregate view.",
      { { "pipeline", "object", "Pipeline definition: {id, name, steps: [{id, title, operator, params, inputs: [{fromStepId, fromPort, toPort}]}]}. A JSON string is also accepted.", true },
        { "auto_load", "boolean", "Auto-load finished outputs as layers (headless MCP default false).", false } } },
    { "get_workflow_status",
      "Get the aggregate status of a workflow submitted with run_workflow: overall "
      "state plus per-step execution ids, statuses, and progress.",
      { { "pipeline_id", "integer", "Pipeline id returned by run_workflow.", true } } },
};

QVariantMap metaToolInputSchema(const MetaToolDef &def)
{
    QVariantMap schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    QVariantMap properties;
    QStringList required;
    for (const MetaToolSchemaInput &input : def.inputs) {
        QVariantMap prop;
        prop[QStringLiteral("type")] = QString::fromUtf8(input.type);
        prop[QStringLiteral("description")] = QString::fromUtf8(input.description);
        properties[QString::fromUtf8(input.name)] = prop;
        if (input.required)
            required.append(QString::fromUtf8(input.name));
    }
    schema[QStringLiteral("properties")] = properties;
    if (!required.isEmpty())
        schema[QStringLiteral("required")] = required;
    return schema;
}

/// MCP response envelope for a TaskCenter task: mcpStatusForTask shape plus
/// the execution/algorithm identity echoed from the caller's execution id.
/// When the DataManager is available and the task completed, resolves the
/// committed asset id (via the derivation record's task reference) so agents
/// can continue to get_lineage(asset_id) — the execute→status→lineage loop.
QVariantMap executionStatusResponse( sicnu::data::DataManager *dataManager,
                                     const QString &executionId,
                                     const sicnu::AlgorithmTaskInfo &info )
{
    QVariantMap result = mcpStatusForTask(info);
    result[QStringLiteral("execution_id")] = executionId;
    result[QStringLiteral("algorithm_id")] = info.algorithmId;
    // Asset resolution (#634): the committed payload already carries the
    // asset id (both committers stamp it); the provenance scan below is a
    // FALLBACK that walked every asset x provenance() per poll - O(N) on
    // every get_execution_status. Only run it when the payload lacks the id
    // AND the catalog is small enough for the scan to be cheap.
    const QVariant payloadAssetId = result.value( QStringLiteral( "result" ) ).toMap()
                                        .value( QStringLiteral( "assetId" ) );
    if ( dataManager && info.status == sicnu::TaskStatus::Completed
         && payloadAssetId.toString().isEmpty() )
    {
        const QString taskRef = QString::number( info.taskId );
        const auto snapshots = dataManager->assets();
        if ( snapshots.size() <= 200 )
        {
            for ( const auto &snapshot : snapshots )
            {
                const auto prov = dataManager->provenance( snapshot.id() );
                if ( prov && prov->taskReference == taskRef )
                {
                    result[QStringLiteral("asset_id")] = snapshot.id().toString();
                    result[QStringLiteral("asset_name")] = snapshot.displayName();
                    break;
                }
            }
        }
    }
    return result;
}

/// Attaches the ADR 0122 algorithm catalog sidecar (task/input/output/gpu/
/// accuracy) to a discovery entry when a manifest exists for the id. The
/// store loads data/processing/algorithm_meta/*.json once per process.
void attachCatalogEntry( QVariantMap &algMap, const QString &id )
{
    // One-time lazy load of data/processing/algorithm_meta/*.json.
    static const bool kMetaLoaded = [] {
        sicnu::processing::AlgorithmMetaStore::instance().loadDefaults();
        return true;
    }();
    Q_UNUSED( kMetaLoaded )
    const auto entry = sicnu::processing::AlgorithmMetaStore::instance().find( id.toStdString() );
    if ( entry )
        algMap[QStringLiteral("catalog")] = sicnu::processing::jsonValueToVariant( entry->toJson() );
}

} // namespace

QVariantMap mcpStatusForTask(const sicnu::AlgorithmTaskInfo &info)
{
    QVariantMap result;
    switch (info.status) {
    case sicnu::TaskStatus::Completed:
        result[QStringLiteral("status")] = QStringLiteral("completed");
        if (!info.resultPayload.isNull()) {
            result[QStringLiteral("result")] = sicnu::processing::jsonValueToVariant(info.resultPayload);
        }
        break;
    case sicnu::TaskStatus::Failed:
        result[QStringLiteral("status")] = QStringLiteral("failed");
        result[QStringLiteral("errorMessage")] = info.errorMessage;
        break;
    case sicnu::TaskStatus::Canceled:
        result[QStringLiteral("status")] = QStringLiteral("canceled");
        break;
    case sicnu::TaskStatus::Queued:
    case sicnu::TaskStatus::Running:
    case sicnu::TaskStatus::Paused:
    default:
        result[QStringLiteral("status")] = QStringLiteral("running");
        break;
    case sicnu::TaskStatus::WaitingResource:
        result[QStringLiteral("status")] = QStringLiteral("waiting_resource");
        break;
    case sicnu::TaskStatus::Cancelling:
        result[QStringLiteral("status")] = QStringLiteral("cancelling");
        break;
    }
    result[QStringLiteral("progress")] = info.progressPercentage;
    if (!info.logBuffer.isEmpty())
        result[QStringLiteral("progressText")] = info.logBuffer.last();
    return result;
}

// StdinReader implementation
void StdinReader::requestStop()
{
    m_stopRequested = true;
}

void StdinReader::run()
{
    constexpr size_t kMaxMcpLine = 4 * 1024 * 1024;
    std::string stdLine;
    while (!m_stopRequested && std::getline(std::cin, stdLine))
    {
        if (stdLine.size() > kMaxMcpLine)
        {
            // Oversized line: report parse error without allocating huge QString
            emit lineRead(QStringLiteral("__MCP_LINE_TOO_LONG__"));
            continue;
        }
        QString line = QString::fromStdString(stdLine).trimmed();
        if (!line.isEmpty())
        {
            emit lineRead(line);
        }
    }
}

// McpServer implementation
McpServer::McpServer(QObject *parent)
    : QObject(parent)
{
    ProcessingJobAdapter::registerProcessingJobExecutor();
    mDispatcher.setSourceTag(QStringLiteral("mcp"));
    mDispatcher.setInteractionActionHandler([](const std::string &name, const Json::Value &args) {
        return sicnu::agent::InteractionToolRegistry::instance().execute(name, args);
    });
}

McpServer::~McpServer()
{
    if (mReader)
    {
        mReader->requestStop();
        mReader->quit();
        if (!mReader->wait(1000) && mReader->isRunning())
        {
            mReader->terminate();
            mReader->wait(1000);
        }
    }
}

void McpServer::start(QCoreApplication *app)
{
    ProcessingJobAdapter::registerProcessingJobExecutor();
    mApp = app;
    mReader = new StdinReader(this);
    connect(mReader, &StdinReader::lineRead, this, &McpServer::onLineRead);
    if (mApp)
        connect(mReader, &QThread::finished, mApp, &QCoreApplication::quit);
    mReader->start();
    SICNU_LOG_SUCCESS(SicnuLogTags::MCP, "MCP Server started on stdio");
}

void McpServer::onLineRead(const QString &line)
{
    if (line == QStringLiteral("__MCP_LINE_TOO_LONG__"))
    {
        sendError(QVariant(), -32700, QStringLiteral("Parse error: line too long (exceeds 4 MiB)"));
        return;
    }
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        sendError(QVariant(), -32700, QStringLiteral("Parse error: ") + error.errorString());
        return;
    }

    if (!doc.isObject())
    {
        sendError(QVariant(), -32600, QStringLiteral("Invalid Request: expected JSON object"));
        return;
    }

    handleRequest(doc.object().toVariantMap());
}

void McpServer::handleRequest(const QVariantMap &request)
{
    QVariant id = request.value(QStringLiteral("id"));
    const bool isNotification = !request.contains(QStringLiteral("id"));
    // JSON-RPC 2.0: an explicit "id": null identifies a REQUEST that must be
    // answered with "id": null; only an ABSENT id means notification. An
    // absent id arrives here as an invalid QVariant and an explicit null as
    // an invalid one too, so tag the latter with a valid Nullptr variant —
    // QJsonValue::fromVariant serializes it as null (#644).
    if ( !isNotification && !id.isValid() )
        id = QVariant::fromValue<std::nullptr_t>( nullptr );
    QString method = request.value(QStringLiteral("method")).toString();
    QVariantMap params = request.value(QStringLiteral("params")).toMap();

    // MCP spec: requests other than initialize/ping before the handshake
    // are rejected with -32002 Server not initialized (#701.7) — the old
    // server answered everything, so a mis-ordered client got confusing
    // half-valid responses instead of the protocol error.
    if ( !m_initialized && method != QStringLiteral( "initialize" )
         && method != QStringLiteral( "ping" ) && !method.startsWith( QStringLiteral( "notifications/" ) ) )
    {
        sendError( id, -32002, QStringLiteral( "Server not initialized" ) );
        return;
    }

    SICNU_LOG_INFO(SicnuLogTags::MCP, QString("MCP request: %1 (id=%2)").arg(method).arg(id.toString()));

    if (method == QStringLiteral("initialize"))
    {
        QVariantMap result;
        // MCP spec: server responds with latest version it supports, not echo.
        static const QString kSupportedVersion = QStringLiteral("2024-11-05");
        result[QStringLiteral("protocolVersion")] = kSupportedVersion;
        m_initialized = true;

        QVariantMap capabilities;
        QVariantMap toolsCap;
        toolsCap[QStringLiteral("listChanged")] = false;
        capabilities[QStringLiteral("tools")] = toolsCap;
        result[QStringLiteral("capabilities")] = capabilities;

        QVariantMap serverInfo;
        serverInfo[QStringLiteral("name")] = QStringLiteral("exp-rs-mcp");
        serverInfo[QStringLiteral("version")] = QStringLiteral("1.0.0");
        result[QStringLiteral("serverInfo")] = serverInfo;

        sendResponse(id, result);
    }
    else if (method == QStringLiteral("notifications/cancelled"))
    {
        // MCP cancellation notification (#634): the client dropped a request
        // (e.g. user abort). Best-effort: when the cancelled request id maps
        // to a known tools/call execution, cancel its task so server-side
        // work actually stops.
        // MCP spec: notifications/cancelled carries { requestId } in params.
        // The params form is authoritative; the top-level form is accepted as
        // a defensive fallback (pre-spec clients, #645).
        const QVariantMap cancelledParams = request.value(QStringLiteral("params")).toMap();
        QString cancelledRpcId = cancelledParams.value(QStringLiteral("requestId")).toString();
        if (cancelledRpcId.isEmpty())
            cancelledRpcId = request.value(QStringLiteral("requestId")).toString();
        QVariantMap cancelledInfo = request.value(QStringLiteral("requestInfo")).toMap();
        Q_UNUSED(cancelledInfo)
        if ( !cancelledRpcId.isEmpty() )
        {
            const long taskId = m_cancelledRequestTasks.value( cancelledRpcId, -1 );
            if ( taskId > 0 )
                sicnu::TaskCenter::instance().cancelTask( taskId );
            // Cancellation is one-shot: consume the entry so the map cannot
            // grow without bound (#644).
            m_cancelledRequestTasks.remove( cancelledRpcId );
        }
        // Notifications never get a response.
        return;
    }
    else if (method == QStringLiteral("notifications/initialized"))
    {
        SICNU_LOG_INFO(SicnuLogTags::MCP, QStringLiteral("MCP client initialized notification received"));
        return;
    }
    else if (method == QStringLiteral("ping"))
    {
        QVariantMap result;
        sendResponse(id, result);
    }
    else if (method == QStringLiteral("resources/list"))
    {
        QVariantMap result;
        result[QStringLiteral("resources")] = QVariantList();
        sendResponse(id, result);
    }
    else if (method == QStringLiteral("prompts/list"))
    {
        QVariantMap result;
        result[QStringLiteral("prompts")] = QVariantList();
        sendResponse(id, result);
    }
    else if (method == QStringLiteral("tools/list"))
    {
        QVariantMap result;
        QVariantList tools;
        for (const MetaToolDef &def : kMetaTools) {
            QVariantMap tool;
            tool[QStringLiteral("name")] = QString::fromUtf8(def.name);
            tool[QStringLiteral("description")] = QString::fromUtf8(def.description);
            tool[QStringLiteral("inputSchema")] = metaToolInputSchema(def);
            tools.append(tool);
        }
        // ADR 0122: also expose the unified Agent Tool Catalog (algorithms,
        // interaction, data, spatial tools) with full JSON Schemas so
        // harness-side bridges (e.g. the Pi extension) enumerate one surface.
        // Only tools that tools/call can actually dispatch are listed, and
        // GUI-only interaction tools hidden in headless mode stay hidden here
        // too (same rule as handleListTools).
        const bool headlessNoGui = sicnu::agent::InteractionToolRegistry::instance().toolCount() == 0
            || sicnu::agent::InteractionToolRegistry::instance().findTool("view:get_state") == std::nullopt;
        for (const auto &catalogTool : sicnu::agent::tool_catalog::AgentToolCatalog::instance().listTools()) {
            const QString id = QString::fromStdString(catalogTool.name);
            if (!idHasAllowedPrefix(id, nullptr))
                continue;
            if (headlessNoGui && catalogTool.category == sicnu::agent::tool_catalog::ToolCategory::Interaction
                && (id.startsWith(QStringLiteral("view:")) || id.startsWith(QStringLiteral("roi:"))
                    || id.startsWith(QStringLiteral("canvas:")) || id.startsWith(QStringLiteral("layer:"))
                    || id.startsWith(QStringLiteral("raster:")))
                && !sicnu::agent::InteractionToolRegistry::instance().hasTool(catalogTool.name)) {
                continue;
            }
            QVariantMap tool;
            tool[QStringLiteral("name")] = id;
            tool[QStringLiteral("description")] = QString::fromStdString(catalogTool.description);
            tool[QStringLiteral("inputSchema")] = sicnu::processing::jsonValueToVariant(catalogTool.inputSchema);
            tools.append(tool);
        }
        result[QStringLiteral("tools")] = tools;
        sendResponse(id, result);
    }
    else if (method == QStringLiteral("tools/call"))
    {
        QString toolName = params.value(QStringLiteral("name")).toString();
        // MCP spec: tools/call arguments are OPTIONAL but must be an object
        // when present.
        QVariantMap arguments;
        const QVariant rawArguments = params.value(QStringLiteral("arguments"));
        if (!rawArguments.isValid() || rawArguments.isNull())
        {
            // absent: empty arguments
        }
        else if (rawArguments.type() != QVariant::Map)
        {
            sendError(id, -32602, QStringLiteral("tools/call 'arguments' must be an object"));
            return;
        }
        else
        {
            arguments = rawArguments.toMap();
        }

        QVariantMap callResult;
        QVariantList contentList;
        QVariantMap contentObj;
        contentObj[QStringLiteral("type")] = QStringLiteral("text");

        try
        {
            QVariantMap resultData;
            if (toolName == QStringLiteral("list_algorithms"))
            {
                resultData = handleListAlgorithms();
            }
            else if (toolName == QStringLiteral("search_algorithms"))
            {
                resultData = handleSearchAlgorithms(
                    arguments.value(QStringLiteral("query")).toString(),
                    arguments.value(QStringLiteral("group")).toString(),
                    arguments.value(QStringLiteral("input_type")).toString(),
                    arguments.value(QStringLiteral("output_type")).toString(),
                    arguments.value(QStringLiteral("large_raster_safe")).toBool());
            }
            else if (toolName == QStringLiteral("get_algorithm_schema"))
            {
                resultData = handleGetAlgorithmSchema(arguments.value(QStringLiteral("algorithm_id")).toString());
            }
            else if (toolName == QStringLiteral("preflight_algorithm"))
            {
                resultData = handlePreflightAlgorithm(arguments.value(QStringLiteral("algorithm_id")).toString(),
                                                      arguments.value(QStringLiteral("parameters")).toMap());
            }
            else if (toolName == QStringLiteral("execute_algorithm"))
            {
                resultData = handleExecuteAlgorithm(arguments.value(QStringLiteral("algorithm_id")).toString(), arguments.value(QStringLiteral("parameters")).toMap());
            }
            else if (toolName == QStringLiteral("list_operators"))
            {
                resultData = handleListOperators();
            }
            else if (toolName == QStringLiteral("get_operator_schema"))
            {
                resultData = handleGetOperatorSchema(arguments.value(QStringLiteral("operator_id")).toString());
            }
            else if (toolName == QStringLiteral("execute_operator"))
            {
                resultData = handleExecuteOperator(arguments.value(QStringLiteral("operator_id")).toString(),
                                                   arguments.value(QStringLiteral("parameters")).toMap());
            }
            else if (toolName == QStringLiteral("get_execution_status"))
            {
                resultData = handleGetExecutionStatus(arguments.value(QStringLiteral("execution_id")).toString());
            }
            else if (toolName == QStringLiteral("cancel_execution"))
            {
                resultData = handleCancelExecution(arguments.value(QStringLiteral("execution_id")).toString());
            }
            else if (toolName == QStringLiteral("list_layers") || toolName == QStringLiteral("data:list_layers"))
            {
                resultData = handleListLayers();
            }
            else if (toolName == QStringLiteral("describe_dataset") || toolName == QStringLiteral("data:describe_dataset"))
            {
                resultData = handleDescribeDataset(arguments.value(QStringLiteral("layer_id")).toString());
            }
            else if (toolName == QStringLiteral("get_lineage") || toolName == QStringLiteral("data:get_lineage"))
            {
                resultData = handleGetLineage(arguments.value(QStringLiteral("asset_id")).toString());
            }
            else if (toolName == QStringLiteral("list_interaction_tools"))
            {
                resultData = handleListInteractionTools();
            }
            else if (toolName == QStringLiteral("get_interaction_schema"))
            {
                const QString targetTool = arguments.value(QStringLiteral("tool_name")).toString().isEmpty()
                    ? arguments.value(QStringLiteral("tool_id")).toString()
                    : arguments.value(QStringLiteral("tool_name")).toString();
                resultData = handleGetInteractionSchema(targetTool);
            }
            else if (toolName == QStringLiteral("list_tools"))
            {
                // Compact mode (#643): omit per-entry input schemas — pull the
                // full schema for a candidate via get_tool_schema. Default
                // remains full-schema so existing clients are unaffected.
                resultData = handleListTools(arguments.value(QStringLiteral("category")).toString(),
                                             arguments.contains(QStringLiteral("compact"))
                                                 ? arguments.value(QStringLiteral("compact")).toBool()
                                                 : true,
                                             arguments.value(QStringLiteral("limit")).toInt(),
                                             arguments.value(QStringLiteral("cursor")).toInt());
            }
            else if (toolName == QStringLiteral("search_tools"))
            {
                resultData = handleSearchTools(
                    arguments.value(QStringLiteral("query")).toString(),
                    arguments.value(QStringLiteral("group")).toString(),
                    arguments.value(QStringLiteral("tag")).toString(),
                    arguments.value(QStringLiteral("input_type")).toString(),
                    arguments.value(QStringLiteral("output_type")).toString(),
                    arguments.contains(QStringLiteral("compact"))
                        ? arguments.value(QStringLiteral("compact")).toBool()
                        : true,
                    arguments.value(QStringLiteral("limit")).toInt(),
                    arguments.value(QStringLiteral("cursor")).toInt());
            }
            else if (toolName == QStringLiteral("get_tool_schema"))
            {
                resultData = handleGetToolSchema(arguments.value(QStringLiteral("tool_id")).toString());
            }
            else if (toolName == QStringLiteral("run_workflow"))
            {
                resultData = handleRunWorkflow(arguments);
            }
            else if (toolName == QStringLiteral("get_workflow_status"))
            {
                bool idOk = false;
                const long pipelineId = arguments.value(QStringLiteral("pipeline_id")).toLongLong(&idOk);
                if (!idOk || pipelineId < 0)
                    throw std::runtime_error("Invalid or missing pipeline_id");
                resultData = handleGetWorkflowStatus(pipelineId);
            }
            else if (toolName.startsWith(QStringLiteral("spatial:")))
            {
                resultData = handleSpatialToolCall(toolName, arguments);
            }
            else if (toolName.startsWith(QStringLiteral("view:")) ||
                     toolName.startsWith(QStringLiteral("roi:")) ||
                     toolName.startsWith(QStringLiteral("canvas:")) ||
                     toolName.startsWith(QStringLiteral("layer:")) ||
                     toolName.startsWith(QStringLiteral("raster:")))
            {
                resultData = dispatchToolCall(toolName, arguments, false);
            }
            else if (isToolIdAllowed(toolName))
            {
                resultData = dispatchToolCall(toolName, arguments, false);
            }
            else
            {
                if (isNotification)
                    return;
                // Unknown TOOL inside a valid tools/call is an invalid-params
                // problem (-32602), not an unknown method (#620).
                sendError(id, -32602, QStringLiteral("Unknown tool: ") + toolName);
                return;
            }

            // Remember the rpc-id -> task-id mapping so a later
            // notifications/cancelled for this request can cancel the task.
            if ( resultData.contains( QStringLiteral( "execution_id" ) ) )
            {
                bool tidOk = false;
                const long tid = resultData.value( QStringLiteral( "execution_id" ) )
                                     .toString()
                                     .mid( QStringLiteral( "task-" ).size() )
                                     .toLong( &tidOk );
                if ( tidOk && tid > 0 && !id.toString().isEmpty() )
                {
                    // JSON-RPC 2.0 request ids may be numbers OR strings —
                    // both must be cancellable (#644). Keep the map bounded.
                    m_cancelledRequestTasks.insert( id.toString(), tid );
                    while ( m_cancelledRequestTasks.size() > 1024 )
                        m_cancelledRequestTasks.erase( m_cancelledRequestTasks.begin() );
                }
            }

            QJsonDocument resultDoc = QJsonDocument::fromVariant(resultData);
            // Compact (#634): indented JSON inflated every tools/call payload
            // (whitespace is pure transport cost for the model).
            contentObj[QStringLiteral("text")] = QString::fromUtf8(resultDoc.toJson(QJsonDocument::Compact));
            contentList.append(contentObj);
            callResult[QStringLiteral("content")] = contentList;
            sendResponse(id, callResult);
        }
        // Tool EXECUTION failures are tool results with isError:true (MCP
        // spec) so the model can read the failure and react; JSON-RPC error
        // responses are reserved for protocol faults (#620). The structured
        // errorCode/errorCategory ride along in the result object.
        catch (const McpToolError &e)
        {
            if (!isNotification)
                sendToolErrorResult(id, QString::fromUtf8(e.what()), e.errorCode, e.errorCategory, e.retryable);
        }
        catch (const std::exception &e)
        {
            if (!isNotification)
                sendToolErrorResult(id, QString::fromUtf8(e.what()), QString(), QString());
        }
        catch (...)
        {
            if (!isNotification)
                sendToolErrorResult(id, QStringLiteral("Unknown error during tool execution"), QString(), QString());
        }
    }
    else
    {
        if (isNotification)
            return;
        sendError(id, -32601, QStringLiteral("Method not found: ") + method);
    }
}

void McpServer::sendToolErrorResult(const QVariant &id, const QString &message,
                                    const QString &errorCode, const QString &errorCategory,
                                    bool retryable)
{
    // MCP-spec failure surface (#620): a tools/call execution error is a
    // RESULT object with isError:true, never a JSON-RPC error response
    // (those are transport/protocol faults). The structured code/category
    // let agent runtimes classify (retry vs re-plan vs abort).
    QVariantMap contentObj;
    contentObj[QStringLiteral("type")] = QStringLiteral("text");
    contentObj[QStringLiteral("text")] = message;
    QVariantList contentList;
    contentList.append(contentObj);

    QVariantMap callResult;
    callResult[QStringLiteral("content")] = contentList;
    callResult[QStringLiteral("isError")] = true;
    if (!errorCode.isEmpty())
        callResult[QStringLiteral("errorCode")] = errorCode;
    if (!errorCategory.isEmpty())
        callResult[QStringLiteral("errorCategory")] = errorCategory;
    callResult[QStringLiteral("retryable")] = retryable;
    sendResponse(id, callResult);
}

void McpServer::sendResponse(const QVariant &id, const QVariantMap &result)
{
    // An absent id means notification (no response). An explicit "id": null
    // request arrives as a valid Nullptr marker and IS answered — the
    // response carries "id": null on the wire (#644).
    if (!id.isValid())
        return;
    if (id.metaType().id() == QMetaType::Nullptr)
    {
        QJsonObject resp;
        resp[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        resp[QStringLiteral("id")] = QJsonValue(QJsonValue::Null);
        const QJsonObject resultObj = QJsonObject::fromVariantMap(result);
        resp[QStringLiteral("result")] = resultObj;
        QJsonDocument doc(resp);
        std::cout << doc.toJson(QJsonDocument::Compact).constData() << std::endl;
        return;
    }

    QVariantMap response;
    response[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    response[QStringLiteral("id")] = id;
    response[QStringLiteral("result")] = result;

    QJsonDocument doc = QJsonDocument::fromVariant(response);
    std::cout << doc.toJson(QJsonDocument::Compact).constData() << std::endl;
}

void McpServer::sendError(const QVariant &id, int code, const QString &message)
{
    sendError(id, code, message, QVariantMap());
}

void McpServer::sendError(const QVariant &id, int code, const QString &message, const QVariantMap &data)
{
    // The explicit-null marker (Nullptr) is answerable like any other id; only
    // a truly ABSENT id (invalid, not Nullptr) suppresses regular error
    // responses (#644).
    const bool explicitNullId = id.metaType().id() == QMetaType::Nullptr;
    const bool absentId = !id.isValid() || ( id.isNull() && !explicitNullId );
    if (absentId && code != -32700 && code != -32600)
        return;

    const bool hasData = !data.isEmpty();

    if (absentId || explicitNullId)
    {
        QJsonObject resp;
        resp[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
        resp[QStringLiteral("id")] = QJsonValue(QJsonValue::Null);
        QJsonObject err;
        err[QStringLiteral("code")] = code;
        err[QStringLiteral("message")] = message;
        if (hasData)
            err[QStringLiteral("data")] = QJsonObject::fromVariantMap(data);
        resp[QStringLiteral("error")] = err;
        QJsonDocument doc(resp);
        std::cout << doc.toJson(QJsonDocument::Compact).constData() << std::endl;
        return;
    }

    QVariantMap response;
    response[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    response[QStringLiteral("id")] = id;

    QVariantMap errorObj;
    errorObj[QStringLiteral("code")] = code;
    errorObj[QStringLiteral("message")] = message;
    if (hasData)
        errorObj[QStringLiteral("data")] = data;
    response[QStringLiteral("error")] = errorObj;

    QJsonDocument doc = QJsonDocument::fromVariant(response);
    std::cout << doc.toJson(QJsonDocument::Compact).constData() << std::endl;
}

void McpServer::sendNotification(const QString &method, const QVariantMap &params)
{
    QVariantMap notification;
    notification[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    notification[QStringLiteral("method")] = method;
    notification[QStringLiteral("params")] = params;

    QJsonDocument doc = QJsonDocument::fromVariant(notification);
    std::cout << doc.toJson(QJsonDocument::Compact).constData() << std::endl;
}

// MCP Handlers implementation
QVariantMap McpServer::handleListAlgorithms()
{
    QVariantMap result;
    QVariantList algList;

    // Canonical Agent-facing catalog: the AtomicAlgorithmRegistry descriptors
    // (rs: operators with real input/output ports) plus provider algorithms
    // (gdal:/otb:/native:/qgis:) — the same sources ToolCallDispatcher resolves
    // at execution time, so list/get/execute all speak one catalog.
    auto &registry = sicnu::processing::AtomicAlgorithmRegistry::instance();
    const auto descriptors = registry.listDescriptors();
    QSet<QString> seenIds;
    seenIds.reserve(descriptors.size() + 200);

    for (const auto &desc : descriptors)
    {
        const QString id = QString::fromStdString(desc.id);
        seenIds.insert(id);

        QVariantMap algMap;
        algMap[QStringLiteral("id")] = id;
        algMap[QStringLiteral("displayName")] = QString::fromStdString(desc.displayName);
        algMap[QStringLiteral("group")] = QString::fromStdString(desc.group);
        algMap[QStringLiteral("description")] = QString::fromStdString(desc.description);
        algMap[QStringLiteral("source")] = QStringLiteral("rs");

        QVariantList tags;
        for (const auto &t : desc.agentMetadata.tags)
            tags.append(QString::fromStdString(t));
        algMap[QStringLiteral("tags")] = tags;
        algMap[QStringLiteral("memoryPolicy")] = QString::fromStdString(desc.agentMetadata.memoryPolicy);
        algMap[QStringLiteral("largeRasterSafe")] = desc.agentMetadata.largeRasterSafe;

        // Surface the descriptor's agent purpose (provider metadata() or
        // generated) under the same "metadata" key the provider branch
        // below uses, so both listing paths speak one shape (#620).
        if (!desc.agentMetadata.purpose.empty())
        {
            QVariantMap metaMap;
            metaMap[QStringLiteral("purpose")] = QString::fromStdString(desc.agentMetadata.purpose);
            algMap[QStringLiteral("metadata")] = metaMap;
        }

        QVariantList outList;
        for (const auto &out : desc.outputs)
        {
            QVariantMap outMap;
            outMap[QStringLiteral("name")] = QString::fromStdString(out.name);
            outMap[QStringLiteral("type")] = QString::fromUtf8(sicnu::processing::dataTypeToString(out.type).c_str());
            outList.append(outMap);
        }
        algMap[QStringLiteral("outputs")] = outList;

        attachCatalogEntry( algMap, id );

        algList.append(algMap);
    }

    // Provider algorithms reachable through the same dispatcher (findAdapter
    // fallback): enumerate the QGIS processing registry for discovery.
    if (QgsApplication::processingRegistry())
    {
        const QList<const QgsProcessingAlgorithm *> algs = QgsApplication::processingRegistry()->algorithms();
        for (const QgsProcessingAlgorithm *alg : algs)
        {
            if (!alg)
                continue;
            const QString id = alg->id();
            // rs: operators win when ids collide.
            if (seenIds.contains(id))
                continue;
            seenIds.insert(id);

            QVariantMap algMap;
            algMap[QStringLiteral("id")] = id;
            algMap[QStringLiteral("displayName")] = alg->displayName();
            algMap[QStringLiteral("group")] = alg->group();
            const QString help = alg->shortHelpString();
            algMap[QStringLiteral("description")] = help.isEmpty() ? alg->shortDescription() : help;
            algMap[QStringLiteral("source")] = QStringLiteral("provider");

            QVariantList tags;
            const QStringList algTags = alg->tags();
            for (const QString &t : algTags)
                tags.append(t);
            algMap[QStringLiteral("tags")] = tags;
            algMap[QStringLiteral("memoryPolicy")] = QStringLiteral("unknown");
            algMap[QStringLiteral("largeRasterSafe")] = false;

            // Preserve the provider's native metadata map (purpose, help, ...)
            // for backward compatibility with MCP clients.
            QVariantMap providerMeta = alg->metadata();
            if (!providerMeta.isEmpty())
                algMap[QStringLiteral("metadata")] = providerMeta;

            QVariantList outList;
            const auto outputs = alg->outputDefinitions();
            for (const QgsProcessingOutputDefinition *out : outputs)
            {
                if (!out)
                    continue;
                QVariantMap outMap;
                outMap[QStringLiteral("name")] = out->name();
                outMap[QStringLiteral("type")] = out->type();
                outList.append(outMap);
            }
            algMap[QStringLiteral("outputs")] = outList;

            algList.append(algMap);
        }
    }

    result[QStringLiteral("algorithms")] = algList;
    result[QStringLiteral("count")] = static_cast<int>(algList.size());
    return result;
}

QVariantMap McpServer::handleSearchAlgorithms(const QString &query, const QString &group,
                                              const QString &inputType, const QString &outputType,
                                              bool largeRasterSafeOnly)
{
    QVariantMap result = handleListAlgorithms();
    const QVariantList all = result.value(QStringLiteral("algorithms")).toList();
    QVariantList filtered;

    const QString needle = query.trimmed().toLower();
    for (const QVariant &entryVar : all)
    {
        const QVariantMap entry = entryVar.toMap();

        if (!group.isEmpty() && entry.value(QStringLiteral("group")).toString() != group)
            continue;
        if (largeRasterSafeOnly && !entry.value(QStringLiteral("largeRasterSafe")).toBool())
            continue;

        if (!inputType.isEmpty())
        {
            // Resolve the algorithm's input types via the canonical descriptor.
            const QString id = entry.value(QStringLiteral("id")).toString();
            const auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(id.toStdString());
            bool matched = false;
            if (adapter)
            {
                const auto &inputs = adapter->descriptor().inputs;
                for (const auto &port : inputs)
                {
                    if (QString::compare(QString::fromUtf8(sicnu::processing::dataTypeToString(port.type).c_str()), inputType, Qt::CaseInsensitive) == 0)
                    { matched = true; break; }
                }
            }
            if (!matched)
                continue;
        }
        if (!outputType.isEmpty())
        {
            const QString id = entry.value(QStringLiteral("id")).toString();
            const auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(id.toStdString());
            bool matched = false;
            if (adapter)
            {
                const auto &outputs = adapter->descriptor().outputs;
                for (const auto &port : outputs)
                {
                    if (QString::compare(QString::fromUtf8(sicnu::processing::dataTypeToString(port.type).c_str()), outputType, Qt::CaseInsensitive) == 0)
                    { matched = true; break; }
                }
            }
            if (!matched)
                continue;
        }

        if (!needle.isEmpty())
        {
            const QString haystack = (entry.value(QStringLiteral("id")).toString() + " "
                                      + entry.value(QStringLiteral("displayName")).toString() + " "
                                      + entry.value(QStringLiteral("group")).toString() + " "
                                      + entry.value(QStringLiteral("description")).toString()).toLower();
            if (!haystack.contains(needle))
                continue;
        }

        filtered.append(entryVar);
    }

    QVariantMap out;
    out[QStringLiteral("algorithms")] = filtered;
    out[QStringLiteral("count")] = static_cast<int>(filtered.size());
    return out;
}

QVariantMap McpServer::handleListOperators()
{
    QVariantMap result;
    QVariantList opList;

    auto &registry = sicnu::operators::RSOperatorRegistry::instance();
    const auto names = registry.operatorNames();
    for (const std::string &name : names) {
        auto op = registry.create(name);
        if (!op)
            continue;

        QVariantMap opMap;
        opMap[QStringLiteral("id")] = QString::fromStdString(op->name());
        opMap[QStringLiteral("displayName")] = QString::fromStdString(op->displayName());
        opMap[QStringLiteral("group")] = QString::fromStdString(op->group());
        opMap[QStringLiteral("description")] = QString::fromStdString(op->description());

        const Json::Value meta = op->metadata();
        if (meta.isObject() && !meta.empty()) {
            opMap[QStringLiteral("metadata")] = sicnu::processing::jsonObjectToVariantMap(meta);
        }

        opList.append(opMap);
    }

    result[QStringLiteral("operators")] = opList;
    result[QStringLiteral("count")] = static_cast<int>(opList.size());
    return result;
}

QVariantMap McpServer::handleGetOperatorSchema(const QString &operatorId)
{
    auto op = sicnu::operators::RSOperatorRegistry::instance().create(operatorId.toStdString());
    if (!op) {
        throw std::runtime_error(QStringLiteral("Operator not found: %1").arg(operatorId).toStdString());
    }

    QVariantMap result = sicnu::processing::jsonObjectToVariantMap(op->schema());
    result[QStringLiteral("operator_id")] = operatorId;
    result[QStringLiteral("metadata")] = sicnu::processing::jsonObjectToVariantMap(op->metadata());
    return result;
}

bool McpServer::isToolIdAllowed(const QString &toolId, QString *reason)
{
    bool isCustom = false;
    if (idHasAllowedPrefix(toolId, &isCustom))
        return true;

    if (isCustom || toolId.startsWith(QStringLiteral("custom_tools:"))) {
        if (envFlagEnabled("SICNU_MCP_TRUST_CUSTOM_TOOLS"))
            return true;
        if (reason) {
            *reason = QStringLiteral(
                "Tool id '%1' is blocked by default (custom_tools). "
                "Set SICNU_MCP_TRUST_CUSTOM_TOOLS=1 to allow.").arg(toolId);
        }
        return false;
    }

    if (reason) {
        *reason = QStringLiteral(
            "Tool id '%1' is not in the MCP allow-list "
            "(rs:, gdal:, gdal_tools:, otb:, otb_tools:, qgis:, qgis_algorithms:, opencv:, spatial:).").arg(toolId);
    }
    return false;
}

bool McpServer::validateWorkspacePaths(const QVariantMap &parameters, QString *reason)
{
    const QString workspace = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("SICNU_MCP_WORKSPACE"));
    if (workspace.isEmpty())
        return true;

    QString detail;
    if (collectOutsideWorkspace(parameters, workspace, &detail)) {
        if (reason)
            *reason = detail;
        return false;
    }
    return true;
}

bool McpServer::parseExecutionId(const QString &executionId, long *taskId)
{
    if (!executionId.startsWith(QStringLiteral("task-")))
        return false;
    bool ok = false;
    const long id = executionId.mid(5).toLong(&ok);
    if (!ok || id <= 0)
        return false;
    *taskId = id;
    return true;
}

QString McpServer::toExecutionId(long taskId)
{
    return QStringLiteral("task-%1").arg(taskId);
}

QVariantMap McpServer::dispatchToolCall(const QString &toolId, const QVariantMap &parameters, bool isOperatorCall)
{
    SICNU_LOG_INFO(SicnuLogTags::MCP, QString("Executing tool: %1").arg(toolId));

    QString denyReason;
    if (!isToolIdAllowed(toolId, &denyReason)) {
        SICNU_LOG_ERROR(SicnuLogTags::MCP, denyReason);
        throw std::runtime_error(denyReason.toStdString());
    }
    if (!validateWorkspacePaths(parameters, &denyReason)) {
        SICNU_LOG_ERROR(SicnuLogTags::MCP, denyReason);
        throw std::runtime_error(denyReason.toStdString());
    }

    // Tool calls enter the Task Center through the ToolCallDispatcher,
    // which parses the envelope, normalizes the id, and validates required
    // descriptor inputs before submission. MCP never blocks on completion.
    Json::Value envelope(Json::objectValue);
    envelope["name"] = toolId.toStdString();
    envelope["parameters"] = sicnu::processing::variantToJsonValue(parameters);

    const QString reason = mDispatcher.rejectionReason(envelope);
    if (!reason.isEmpty()) {
        if (reason.startsWith(QStringLiteral("Algorithm not registered:"))) {
            const QString msg = isOperatorCall
                ? (QStringLiteral("Operator not found: ") + toolId)
                : (QStringLiteral("Algorithm not found: ") + toolId);
            SICNU_LOG_ERROR(SicnuLogTags::MCP, msg);
            throw std::runtime_error(msg.toStdString());
        }
        SICNU_LOG_ERROR(SicnuLogTags::MCP, reason);
        throw std::runtime_error(reason.toStdString());
    }

    if (sicnu::processing::ToolCallDispatcher::isInteractionAction(toolId.toStdString())) {
        const Json::Value resVal = mDispatcher.dispatchAndAwait(envelope);
        if (resVal.isObject() && resVal.isMember("status") && resVal["status"].asString() == "error") {
            const QString errMsg = resVal.isMember("errorMessage")
                ? QString::fromStdString(resVal["errorMessage"].asString())
                : QStringLiteral("Interaction tool execution failed");
            const QString errCode = resVal.isMember("errorCode")
                ? QString::fromStdString(resVal["errorCode"].asString())
                : QStringLiteral("INTERACTION_ERROR");
            throw McpToolError(errMsg, errCode, QStringLiteral("runtime"));
        }
        return sicnu::processing::jsonObjectToVariantMap(resVal);
    }

    long taskId = -1;
    QString submitError;
    if (!mDispatcher.submit(envelope, {}, &submitError, &taskId) || taskId <= 0) {
        const QString msg = submitError.isEmpty()
            ? QStringLiteral("Failed to submit tool execution to Task Center")
            : submitError;
        SICNU_LOG_ERROR(SicnuLogTags::MCP, msg);
        throw std::runtime_error(msg.toStdString());
    }

    QVariantMap result;
    result[QStringLiteral("execution_id")] = toExecutionId(taskId);
    result[QStringLiteral("status")] = QStringLiteral("running");
    if (isOperatorCall) {
        result[QStringLiteral("operator_id")] = toolId;
    } else {
        result[QStringLiteral("algorithm_id")] = toolId;
    }
    return result;
}

QVariantMap McpServer::handleExecuteOperator(const QString &operatorId, const QVariantMap &parameters)
{
    return dispatchToolCall(operatorId, parameters, /*isOperatorCall=*/true);
}

QVariantMap McpServer::handleGetAlgorithmSchema(const QString &algorithmId)
{
    const auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(algorithmId.toStdString());
    if (!adapter)
    {
        throw std::runtime_error(QStringLiteral("Algorithm not found: %1").arg(algorithmId).toStdString());
    }

    const auto desc = adapter->descriptor();
    QVariantMap result = sicnu::processing::jsonObjectToVariantMap(desc.toInputSchema());
    result[QStringLiteral("algorithm_id")] = algorithmId;
    result[QStringLiteral("outputs")] = sicnu::processing::jsonObjectToVariantMap(desc.toOutputSchema());
    result[QStringLiteral("metadata")] = sicnu::processing::jsonObjectToVariantMap(desc.agentMetadata.toJson());
    attachCatalogEntry( result, algorithmId );
    return result;
}

QVariantMap McpServer::handlePreflightAlgorithm(const QString &algorithmId, const QVariantMap &parameters)
{
    // Preflight probes datasets (size/bands/CRS) at the caller-provided paths,
    // so it must honour the same SICNU_MCP_WORKSPACE containment as the
    // execute path - otherwise read-probing any file outside the workspace is
    // possible through this meta tool (#640).
    QString denyReason;
    if (!validateWorkspacePaths(parameters, &denyReason))
    {
        SICNU_LOG_ERROR(SicnuLogTags::MCP, denyReason);
        throw McpToolError(algorithmId + QStringLiteral(": ") + denyReason,
                           QStringLiteral("PATH_OUTSIDE_WORKSPACE"),
                           QStringLiteral("validation"));
    }

    const Json::Value paramsJson = sicnu::processing::variantToJsonValue(parameters);
    const Json::Value preflight = sicnu::processing::preflightAlgorithm(algorithmId.toStdString(), paramsJson);
    return sicnu::processing::jsonObjectToVariantMap(preflight);
}

QVariantMap McpServer::handleExecuteAlgorithm(const QString &algorithmId, const QVariantMap &parameters)
{
    return dispatchToolCall(algorithmId, parameters, /*isOperatorCall=*/false);
}

QVariantMap McpServer::handleGetExecutionStatus(const QString &executionId)
{
    long taskId = 0;
    if (!parseExecutionId(executionId, &taskId))
        throw std::runtime_error("Execution ID not found: " + executionId.toStdString());

    const sicnu::AlgorithmTaskInfo info = sicnu::TaskCenter::instance().getTaskInfo(taskId);
    if (info.taskId != taskId)
        throw std::runtime_error("Execution ID not found: " + executionId.toStdString());

    return executionStatusResponse(m_dataManager, executionId, info);
}

QVariantMap McpServer::handleCancelExecution(const QString &executionId)
{
    long taskId = 0;
    if (!parseExecutionId(executionId, &taskId))
        throw std::runtime_error("Execution ID not found: " + executionId.toStdString());

    if (!sicnu::TaskCenter::instance().cancelTask(taskId)) {
        // cancelTask returned false: the task is already terminal (or gone).
        // Report its ACTUAL status instead of a blanket "canceled" (ADR 0022).
        const sicnu::AlgorithmTaskInfo info = sicnu::TaskCenter::instance().getTaskInfo(taskId);
        if (info.taskId != taskId)
            throw std::runtime_error("Execution ID not found: " + executionId.toStdString());

        return executionStatusResponse(m_dataManager, executionId, info);
    }

    QVariantMap result;
    result[QStringLiteral("execution_id")] = executionId;
    result[QStringLiteral("status")] = QStringLiteral("canceled");
    return result;
}

QVariantMap McpServer::handleListLayers()
{
    QVariantMap result;
    QVariantList layerList;

    QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.begin(); it != layers.end(); ++it)
    {
        QgsMapLayer *layer = it.value();
        if (!layer)
            continue;

        QVariantMap layerMap;
        layerMap[QStringLiteral("id")] = layer->id();
        layerMap[QStringLiteral("name")] = layer->name();
        layerMap[QStringLiteral("type")] = (layer->type() == Qgis::LayerType::Raster) ? QStringLiteral("raster") : QStringLiteral("vector");
        layerMap[QStringLiteral("source")] = layer->source();

        layerList.append(layerMap);
    }

    // #688: MCP mode is always project-empty (autoLoad=false on every
    // submission path) — surface the DataManager asset catalog so the agent
    // can actually discover the session's committed outputs.
    if (layers.isEmpty() && m_dataManager)
    {
        for (const auto &asset : m_dataManager->assets())
        {
            QVariantMap layerMap;
            layerMap[QStringLiteral("id")] = asset.id().toString();
            layerMap[QStringLiteral("name")] = asset.displayName();
            layerMap[QStringLiteral("type")] = asset.kind() == sicnu::data::AssetKind::Raster
                                                   ? QStringLiteral("raster")
                                                   : QStringLiteral("vector");
            layerMap[QStringLiteral("source")] = asset.source().canonicalSource;
            const auto &structure = asset.structure();
            if (const auto *raster = std::get_if<sicnu::data::RasterStructure>(&structure))
                layerMap[QStringLiteral("bands")] = raster->bandCount;
            layerList.append(layerMap);
        }
    }

    result[QStringLiteral("layers")] = layerList;
    return result;
}

QVariantMap McpServer::handleDescribeDataset(const QString &layerId)
{
    QgsMapLayer *layer = QgsProject::instance()->mapLayer(layerId);
    if (!layer)
    {
        // Try resolving by name
        QList<QgsMapLayer *> layers = QgsProject::instance()->mapLayersByName(layerId);
        if (!layers.isEmpty())
        {
            layer = layers.first();
        }
    }

    if (!layer && m_dataManager)
    {
        // #688: headless fallback — resolve the target against the asset
        // catalog (by AssetId, then by display name) and answer from the
        // registered structure instead of failing with "Layer not found".
        const auto id = sicnu::data::AssetId::fromString(layerId);
        std::optional<sicnu::data::AssetSnapshot> snapshot;
        if (id)
            snapshot = m_dataManager->asset(*id);
        if (!snapshot)
        {
            for (const auto &candidate : m_dataManager->assets())
            {
                if (candidate.displayName() == layerId)
                {
                    snapshot = candidate;
                    break;
                }
            }
        }
        if (snapshot)
        {
            QVariantMap assetResult;
            assetResult[QStringLiteral("id")] = snapshot->id().toString();
            assetResult[QStringLiteral("name")] = snapshot->displayName();
            assetResult[QStringLiteral("source")] = snapshot->source().canonicalSource;
            const auto &structure = snapshot->structure();
            if (const auto *raster = std::get_if<sicnu::data::RasterStructure>(&structure))
            {
                assetResult[QStringLiteral("type")] = QStringLiteral("raster");
                assetResult[QStringLiteral("width")] = raster->width;
                assetResult[QStringLiteral("height")] = raster->height;
                assetResult[QStringLiteral("bands")] = raster->bandCount;
                assetResult[QStringLiteral("crs")] = raster->crsWkt;
                if (raster->extent.valid)
                {
                    assetResult[QStringLiteral("extent")] = QVariantMap{
                        {QStringLiteral("xmin"), raster->extent.minimumX},
                        {QStringLiteral("ymin"), raster->extent.minimumY},
                        {QStringLiteral("xmax"), raster->extent.maximumX},
                        {QStringLiteral("ymax"), raster->extent.maximumY}
                    };
                }
            }
            else if (const auto *vector = std::get_if<sicnu::data::VectorStructure>(&structure))
            {
                assetResult[QStringLiteral("type")] = QStringLiteral("vector");
                assetResult[QStringLiteral("layerCount")] = vector->layerCount;
                if (!vector->layers.isEmpty())
                    assetResult[QStringLiteral("crs")] = vector->layers.first().crsWkt;
            }
            return assetResult;
        }
    }

    if (!layer)
    {
        throw std::runtime_error("Layer not found: " + layerId.toStdString());
    }

    QVariantMap result;
    result[QStringLiteral("id")] = layer->id();
    result[QStringLiteral("name")] = layer->name();
    result[QStringLiteral("type")] = (layer->type() == Qgis::LayerType::Raster) ? QStringLiteral("raster") : QStringLiteral("vector");
    result[QStringLiteral("crs")] = layer->crs().authid();

    QgsRectangle extent = layer->extent();
    result[QStringLiteral("extent")] = QVariantMap{
        {QStringLiteral("xmin"), extent.xMinimum()},
        {QStringLiteral("ymin"), extent.yMinimum()},
        {QStringLiteral("xmax"), extent.xMaximum()},
        {QStringLiteral("ymax"), extent.yMaximum()}
    };

    if (layer->type() == Qgis::LayerType::Raster)
    {
        QgsRasterLayer *raster = qobject_cast<QgsRasterLayer *>(layer);
        if (raster && raster->dataProvider())
        {
            QgsRasterDataProvider *provider = raster->dataProvider();
            result[QStringLiteral("width")] = raster->width();
            result[QStringLiteral("height")] = raster->height();
            result[QStringLiteral("band_count")] = provider->bandCount();

            // Semantic band roles (SICNU_BAND_ROLE product metadata) so the
            // agent can select bands by role ("nir", "red", "qa") instead of
            // band numbers (ADR 0087). Empty when the raster carries none.
            GdalDatasetWrapper ds;
            const bool rolesAvailable = ds.open(raster->source());

            QVariantList bands;
            for (int i = 1; i <= provider->bandCount(); ++i)
            {
                QVariantMap bandMap;
                bandMap[QStringLiteral("index")] = i;
                bandMap[QStringLiteral("color_interpretation")] = provider->colorInterpretationName(i);
                bandMap[QStringLiteral("dataType")] = mcpDataTypeToString(provider->dataType(i));
                bandMap[QStringLiteral("has_nodata")] = provider->sourceHasNoDataValue(i);
                if (provider->sourceHasNoDataValue(i))
                {
                    bandMap[QStringLiteral("nodata_value")] = provider->sourceNoDataValue(i);
                }
                bandMap[QStringLiteral("role")] =
                    rolesAvailable ? ds.bandMetadataItem(i, "SICNU_BAND_ROLE") : QString();
                bands.append(bandMap);
            }
            result[QStringLiteral("bands")] = bands;
        }
    }
    else if (layer->type() == Qgis::LayerType::Vector)
    {
        QgsVectorLayer *vector = qobject_cast<QgsVectorLayer *>(layer);
        if (vector)
        {
            result[QStringLiteral("feature_count")] = static_cast<qlonglong>(vector->featureCount());

            QString geomType = QStringLiteral("Unknown");
            switch (vector->geometryType())
            {
                case Qgis::GeometryType::Point: geomType = QStringLiteral("Point"); break;
                case Qgis::GeometryType::Line: geomType = QStringLiteral("Line"); break;
                case Qgis::GeometryType::Polygon: geomType = QStringLiteral("Polygon"); break;
                case Qgis::GeometryType::Null: geomType = QStringLiteral("Null"); break;
                default: break;
            }
            result[QStringLiteral("geometry_type")] = geomType;

            QVariantList fields;
            QgsFields layerFields = vector->fields();
            for (int i = 0; i < layerFields.count(); ++i)
            {
                QVariantMap fieldMap;
                fieldMap[QStringLiteral("name")] = layerFields.at(i).name();
                fieldMap[QStringLiteral("type")] = layerFields.at(i).typeName();
                fields.append(fieldMap);
            }
            result[QStringLiteral("fields")] = fields;
        }
    }

    return result;
}

QVariantMap McpServer::handleGetLineage(const QString &assetIdText)
{
    if (!m_dataManager)
    {
        throw std::runtime_error("Data manager is not available");
    }
    const auto id = sicnu::data::AssetId::fromString(assetIdText);
    if (!id)
    {
        throw std::runtime_error("Invalid asset id: " + assetIdText.toStdString());
    }
    const auto snapshot = m_dataManager->asset(*id);
    if (!snapshot)
    {
        throw std::runtime_error("Asset not found: " + assetIdText.toStdString());
    }

    QVariantMap result;
    result[QStringLiteral("id")] = snapshot->id().toString();
    result[QStringLiteral("name")] = snapshot->displayName();
    result[QStringLiteral("source")] = snapshot->source().canonicalSource;

    // The deriving algorithm + parameters when this asset was produced.
    if (const auto prov = m_dataManager->provenance(*id))
    {
        result[QStringLiteral("provenance")] = prov->toJson().toVariantMap();
    }

    // Input assets this one was derived from, with display names resolved.
    QVariantList inputs;
    for (const auto &inputId : m_dataManager->derivedFrom(*id))
    {
        QVariantMap entry;
        entry[QStringLiteral("id")] = inputId.toString();
        if (const auto inputSnapshot = m_dataManager->asset(inputId))
        {
            entry[QStringLiteral("name")] = inputSnapshot->displayName();
        }
        inputs.append(entry);
    }
    result[QStringLiteral("derivedFrom")] = inputs;

    // Assets derived from this one.
    QVariantList outputs;
    for (const auto &outputId : m_dataManager->derivedOutputsOf(*id))
    {
        QVariantMap entry;
        entry[QStringLiteral("id")] = outputId.toString();
        if (const auto outputSnapshot = m_dataManager->asset(outputId))
        {
            entry[QStringLiteral("name")] = outputSnapshot->displayName();
        }
        outputs.append(entry);
    }
    result[QStringLiteral("derivedOutputsOf")] = outputs;

    return result;
}

QVariantMap McpServer::handleListInteractionTools()
{
    QVariantMap result;
    QVariantList tools;
    const auto toolList = sicnu::agent::InteractionToolRegistry::instance().listTools();
    for (const auto &def : toolList)
    {
        QVariantMap tool;
        tool[QStringLiteral("name")] = QString::fromStdString(def.name);
        tool[QStringLiteral("displayName")] = QString::fromStdString(def.displayName);
        tool[QStringLiteral("category")] = QString::fromStdString(def.category);
        tool[QStringLiteral("description")] = QString::fromStdString(def.description);
        tool[QStringLiteral("inputSchema")] = sicnu::processing::jsonObjectToVariantMap(def.inputSchema);
        tools.append(tool);
    }
    result[QStringLiteral("tools")] = tools;
    return result;
}

QVariantMap McpServer::handleGetInteractionSchema(const QString &toolName)
{
    const auto toolOpt = sicnu::agent::InteractionToolRegistry::instance().findTool(toolName.toStdString());
    if (!toolOpt.has_value())
    {
        throw std::runtime_error("Interaction tool not found: " + toolName.toStdString());
    }
    QVariantMap result;
    result[QStringLiteral("name")] = QString::fromStdString(toolOpt->name);
    result[QStringLiteral("displayName")] = QString::fromStdString(toolOpt->displayName);
    result[QStringLiteral("category")] = QString::fromStdString(toolOpt->category);
    result[QStringLiteral("description")] = QString::fromStdString(toolOpt->description);
    result[QStringLiteral("inputSchema")] = sicnu::processing::jsonObjectToVariantMap(toolOpt->inputSchema);
    return result;
}

QVariantMap McpServer::handleListTools(const QString &category, bool compact,
                                       int limit, int cursor)
{
    using namespace sicnu::agent::tool_catalog;
    std::optional<ToolCategory> catFilter = std::nullopt;
    if (!category.isEmpty()) {
        catFilter = tryParseToolCategory(category.toStdString());
        if (!catFilter.has_value()) {
            throw std::runtime_error(QStringLiteral("Unknown category: %1").arg(category).toStdString());
        }
    }

    const auto tools = AgentToolCatalog::instance().listTools(catFilter);
    QVariantList toolList;
    toolList.reserve(static_cast<int>(tools.size()));

    const bool headlessNoGui = sicnu::agent::InteractionToolRegistry::instance().toolCount() == 0
        || sicnu::agent::InteractionToolRegistry::instance().findTool("view:get_state") == std::nullopt;
    for (const auto &t : tools) {
        // In headless MCP (no GUI registry), hide GUI-only interaction tools
        // that would always fail with -32000.
        if (headlessNoGui && t.category == ToolCategory::Interaction) {
            const QString qname = QString::fromStdString(t.name);
            if (qname.startsWith(QStringLiteral("view:")) || qname.startsWith(QStringLiteral("roi:")) ||
                qname.startsWith(QStringLiteral("canvas:")) || qname.startsWith(QStringLiteral("layer:")) ||
                qname.startsWith(QStringLiteral("raster:"))) {
                // Only hide if not actually registered headlessly
                if (!sicnu::agent::InteractionToolRegistry::instance().hasTool(t.name))
                    continue;
            }
        }
        QVariantMap toolMap;
        toolMap[QStringLiteral("category")] = QString::fromStdString(toolCategoryToString(t.category));
        toolMap[QStringLiteral("name")] = QString::fromStdString(t.name);
        toolMap[QStringLiteral("description")] = QString::fromStdString(t.description.empty() ? t.displayName : t.description);
        if (!compact)
            toolMap[QStringLiteral("schema")] = sicnu::processing::jsonValueToVariant(t.inputSchema);
        toolList.append(toolMap);
    }

    // Opt-in offset pagination (#643): limit <= 0 keeps the full payload so
    // existing clients (Pi bridge) are unaffected. The listing is
    // deterministically ordered, so a numeric cursor is stable.
    QVariantMap result;
    if (limit > 0) {
        const int start = cursor > 0 ? cursor : 0;
        QVariantList page;
        int nextCursor = -1;
        for (int i = start; i < toolList.size(); ++i) {
            if (limit > 0 && page.size() >= limit) {
                nextCursor = i;
                break;
            }
            page.append(toolList.at(i));
        }
        result[QStringLiteral("tools")] = page;
        result[QStringLiteral("count")] = page.size();
        result[QStringLiteral("total")] = toolList.size();
        result[QStringLiteral("cursor")] = start;
        if (nextCursor >= 0)
            result[QStringLiteral("nextCursor")] = nextCursor;
        result[QStringLiteral("compact")] = compact;
        return result;
    }
    result[QStringLiteral("tools")] = toolList;
    result[QStringLiteral("count")] = toolList.size();
    result[QStringLiteral("compact")] = compact;
    return result;
}

QVariantMap McpServer::handleSearchTools(const QString &query, const QString &group,
                                        const QString &tag, const QString &inputType,
                                        const QString &outputType, bool compact,
                                        int limit, int cursor)
{
    using namespace sicnu::agent::tool_catalog;
    SearchQuery sq;
    sq.text = query.toStdString();
    sq.group = group.toStdString();
    sq.tag = tag.toStdString();
    sq.inputType = inputType.toStdString();
    sq.outputType = outputType.toStdString();

    const auto tools = AgentToolCatalog::instance().searchTools(sq);
    QVariantList toolList;
    toolList.reserve(static_cast<int>(tools.size()));

    const bool headlessNoGui2 = sicnu::agent::InteractionToolRegistry::instance().toolCount() == 0
        || sicnu::agent::InteractionToolRegistry::instance().findTool("view:get_state") == std::nullopt;
    for (const auto &t : tools) {
        if (headlessNoGui2 && t.category == ToolCategory::Interaction) {
            const QString qname = QString::fromStdString(t.name);
            if (qname.startsWith(QStringLiteral("view:")) || qname.startsWith(QStringLiteral("roi:")) ||
                qname.startsWith(QStringLiteral("canvas:")) || qname.startsWith(QStringLiteral("layer:")) ||
                qname.startsWith(QStringLiteral("raster:"))) {
                if (!sicnu::agent::InteractionToolRegistry::instance().hasTool(t.name))
                    continue;
            }
        }
        QVariantMap toolMap;
        toolMap[QStringLiteral("category")] = QString::fromStdString(toolCategoryToString(t.category));
        toolMap[QStringLiteral("name")] = QString::fromStdString(t.name);
        toolMap[QStringLiteral("description")] = QString::fromStdString(t.description.empty() ? t.displayName : t.description);
        if (!compact)
            toolMap[QStringLiteral("schema")] = sicnu::processing::jsonValueToVariant(t.inputSchema);
        toolList.append(toolMap);
    }

    // Opt-in offset pagination (#643): limit <= 0 keeps the full payload so
    // existing clients (Pi bridge) are unaffected. The listing is
    // deterministically ordered, so a numeric cursor is stable.
    QVariantMap result;
    if (limit > 0) {
        const int start = cursor > 0 ? cursor : 0;
        QVariantList page;
        int nextCursor = -1;
        for (int i = start; i < toolList.size(); ++i) {
            if (limit > 0 && page.size() >= limit) {
                nextCursor = i;
                break;
            }
            page.append(toolList.at(i));
        }
        result[QStringLiteral("tools")] = page;
        result[QStringLiteral("count")] = page.size();
        result[QStringLiteral("total")] = toolList.size();
        result[QStringLiteral("cursor")] = start;
        if (nextCursor >= 0)
            result[QStringLiteral("nextCursor")] = nextCursor;
        result[QStringLiteral("compact")] = compact;
        return result;
    }
    result[QStringLiteral("tools")] = toolList;
    result[QStringLiteral("count")] = toolList.size();
    result[QStringLiteral("compact")] = compact;
    return result;
}

QVariantMap McpServer::handleGetToolSchema(const QString &toolId)
{
    using namespace sicnu::agent::tool_catalog;
    auto tool = AgentToolCatalog::instance().findTool(toolId.toStdString());
    if (!tool) {
        throw std::runtime_error(QStringLiteral("Unknown tool: %1").arg(toolId).toStdString());
    }

    QVariantMap result;
    result[QStringLiteral("category")] = QString::fromStdString(toolCategoryToString(tool->category));
    result[QStringLiteral("name")] = QString::fromStdString(tool->name);
    result[QStringLiteral("displayName")] = QString::fromStdString(tool->displayName);
    result[QStringLiteral("description")] = QString::fromStdString(tool->description);
    result[QStringLiteral("schema")] = sicnu::processing::jsonValueToVariant(tool->inputSchema);
    return result;
}

// ---------------------------------------------------------------------------
// Spatial workflow & spatial tool layer (ADR 0122)
// ---------------------------------------------------------------------------

QVariantMap McpServer::handleRunWorkflow(const QVariantMap &arguments)
{
    const QVariant pipelineArg = arguments.value(QStringLiteral("pipeline"));
    QString pipelineJson;
    if (pipelineArg.typeId() == QMetaType::QString) {
        pipelineJson = pipelineArg.toString();
    } else if (pipelineArg.canConvert<QVariantMap>()) {
        const QJsonDocument doc(QJsonObject::fromVariantMap(pipelineArg.toMap()));
        pipelineJson = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    }
    if (pipelineJson.trimmed().isEmpty())
        throw std::runtime_error("Missing required parameter: pipeline");

    // run_workflow submits straight into TaskCenter, so it must satisfy the
    // same SICNU_MCP_WORKSPACE containment as execute_algorithm - otherwise
    // step params can read/write any caller-chosen path (#640). The pipeline
    // may arrive as JSON text, so validate the parsed structure rather than
    // the raw argument strings.
    {
        QVariant pipelineValue = pipelineArg;
        if (pipelineArg.typeId() == QMetaType::QString) {
            const QJsonDocument doc = QJsonDocument::fromJson(pipelineJson.toUtf8());
            if (doc.isObject())
                pipelineValue = doc.object().toVariantMap();
        }
        QVariantMap containmentArgs;
        containmentArgs.insert(QStringLiteral("pipeline"), pipelineValue);
        QString denyReason;
        if (!validateWorkspacePaths(containmentArgs, &denyReason))
        {
            SICNU_LOG_ERROR(SicnuLogTags::MCP, denyReason);
            throw McpToolError(QStringLiteral("run_workflow: ") + denyReason,
                               QStringLiteral("PATH_OUTSIDE_WORKSPACE"),
                               QStringLiteral("validation"));
        }
    }

    const bool autoLoad = arguments.value(QStringLiteral("auto_load")).toBool();

    const long pipelineId = sicnu::TaskCenter::instance().submitPipelineJson(
        pipelineJson.toStdString(), autoLoad);
    if (pipelineId < 0)
        throw std::runtime_error(
            "Invalid pipeline definition: expected {id, steps: [{id, operator, params, inputs}]}");

    QVariantMap result;
    result[QStringLiteral("pipeline_id")] = static_cast<qlonglong>(pipelineId);

    const sicnu::PipelineExecutionInfo info = sicnu::TaskCenter::instance().getPipelineInfo(pipelineId);
    QVariantList steps;
    for (const auto &stepId : info.orderedStepIds) {
        const auto taskIdIt = info.stepToTaskId.find(stepId);
        if (taskIdIt == info.stepToTaskId.end())
            continue;
        const sicnu::AlgorithmTaskInfo task =
            sicnu::TaskCenter::instance().getTaskInfo(taskIdIt.value());
        QVariantMap step;
        step[QStringLiteral("step_id")] = QString::fromStdString(stepId);
        step[QStringLiteral("execution_id")] = toExecutionId(taskIdIt.value());
        step[QStringLiteral("algorithm_id")] = task.algorithmId;
        step[QStringLiteral("status")] = mcpStatusForTask(task).value(QStringLiteral("status"));
        steps.append(step);
    }
    result[QStringLiteral("steps")] = steps;
    result[QStringLiteral("stepCount")] = steps.size();
    result[QStringLiteral("status")] = QStringLiteral("running");
    return result;
}

QVariantMap McpServer::handleGetWorkflowStatus(long pipelineId)
{
    const sicnu::PipelineExecutionInfo info =
        sicnu::TaskCenter::instance().getPipelineInfo(pipelineId);
    if (info.pipelineId < 0)
        throw std::runtime_error(QStringLiteral("Unknown pipeline id: %1").arg(pipelineId).toStdString());

    QVariantMap result;
    result[QStringLiteral("pipeline_id")] = static_cast<qlonglong>(pipelineId);
    result[QStringLiteral("definition_id")] = info.definitionId;
    result[QStringLiteral("isCompleted")] = info.isCompleted;
    result[QStringLiteral("isFailed")] = info.isFailed;
    if (!info.errorMessage.isEmpty())
        result[QStringLiteral("errorMessage")] = info.errorMessage;

    QVariantList steps;
    for (const auto &stepId : info.orderedStepIds) {
        QVariantMap step;
        step[QStringLiteral("step_id")] = QString::fromStdString(stepId);
        const auto taskIdIt = info.stepToTaskId.find(stepId);
        if (taskIdIt != info.stepToTaskId.end()) {
            const sicnu::AlgorithmTaskInfo task =
                sicnu::TaskCenter::instance().getTaskInfo(taskIdIt.value());
            step[QStringLiteral("execution_id")] = toExecutionId(taskIdIt.value());
            step[QStringLiteral("algorithm_id")] = task.algorithmId;
            const QVariantMap status = mcpStatusForTask(task);
            step[QStringLiteral("status")] = status.value(QStringLiteral("status"));
            step[QStringLiteral("progress")] = status.value(QStringLiteral("progress"));
        } else {
            step[QStringLiteral("status")] = QStringLiteral("skipped");
        }
        steps.append(step);
    }
    result[QStringLiteral("steps")] = steps;
    return result;
}

QVariantMap McpServer::handleSpatialToolCall(const QString &toolId, const QVariantMap &parameters)
{
    auto &registry = sicnu::agent::spatial_tools::SpatialToolRegistry::instance();
    // Idempotent: a tools/call may arrive before anything constructed the
    // AgentToolCatalog (which is what normally registers the built-ins).
    registry.registerBuiltinTools();
    const auto tool = registry.find(toolId.toStdString());
    if (!tool)
        throw McpToolError(QStringLiteral("Unknown spatial tool: %1").arg(toolId),
                           QStringLiteral("UNKNOWN_TOOL"),
                           QStringLiteral("validation"));

    // The ADR-0122 inline path must honour the same SICNU_MCP_WORKSPACE
    // containment as dispatchToolCall: raster/vector inspect GDALOpen any
    // caller path, which would otherwise probe the filesystem outside the
    // configured workspace (#620).
    QString denyReason;
    if (!validateWorkspacePaths(parameters, &denyReason))
    {
        SICNU_LOG_ERROR(SicnuLogTags::MCP, denyReason);
        throw McpToolError(toolId + QStringLiteral(": ") + denyReason,
                           QStringLiteral("PATH_OUTSIDE_WORKSPACE"),
                           QStringLiteral("validation"));
    }

    const Json::Value input = sicnu::processing::variantToJsonValue(parameters);
    const std::string schemaError =
        sicnu::agent::spatial_tools::validateAgainstRequired(input, (*tool)->inputSchema());
    if (!schemaError.empty())
        throw McpToolError(QString::fromStdString(toolId.toStdString() + ": " + schemaError),
                           QStringLiteral("INVALID_PARAMETER"),
                           QStringLiteral("validation"));

    SICNU_LOG_INFO(SicnuLogTags::MCP, QString("Executing spatial tool: %1").arg(toolId));
    const auto result = (*tool)->execute(input);
    if (!result.success)
    {
        // Preserve the existing message contract; callers that ignore `data`
        // still see the same human-readable text. Codes are normalized to the
        // structured taxonomy at this boundary (#644): the spatial tools'
        // lowercase io codes (local_file_not_found, gdal_open_failed,
        // provider_open_failed) map to DATA_IO, MODEL_NOT_FOUND to
        // MODEL_NOT_READY; SpatialToolResult::retryable is forwarded instead
        // of being dropped.
        QString errorCode = QString::fromStdString(result.errorCode);
        const QString upper = errorCode.toUpper();
        if (upper == QStringLiteral("LOCAL_FILE_NOT_FOUND")
            || upper == QStringLiteral("GDAL_OPEN_FAILED")
            || upper == QStringLiteral("PROVIDER_OPEN_FAILED"))
            errorCode = QStringLiteral("DATA_IO");
        else if (upper == QStringLiteral("MODEL_NOT_FOUND"))
            errorCode = QStringLiteral("MODEL_NOT_READY");
        throw McpToolError(QString::fromStdString(toolId.toStdString() + ": " + result.error),
                           errorCode,
                           QString::fromStdString(result.errorCategory),
                           -32000,
                           result.retryable);
    }

    QVariantMap out;
    out[QStringLiteral("status")] = QStringLiteral("ok");
    out[QStringLiteral("result")] = sicnu::processing::jsonValueToVariant(result.output);
    return out;
}
