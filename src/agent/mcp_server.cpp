#include "mcp_server.h"
#include "core/sicnu_logging.h"
#include "env_flag.h"

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/json_params_converter.h"
#include "shell/processing_job_adapter.h"

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
        QStringLiteral("qgis:"),
        QStringLiteral("qgis_algorithms:"),
        QStringLiteral("opencv:"), // operator surface uses opencv: filters
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

    // Only enforce on absolute paths / home-expanded paths
    QString path = pathValue;
    if (path.startsWith(QLatin1Char('~'))) {
        path = QDir::homePath() + path.mid(1);
    }
    const QFileInfo fi(path);
    if (!fi.isAbsolute())
        return false;

    QString workspaceCanon = QDir(workspaceRoot).canonicalPath();
    if (workspaceCanon.isEmpty())
        workspaceCanon = QFileInfo(workspaceRoot).absoluteFilePath();
    if (workspaceCanon.isEmpty())
        return false;

    QString resolved;
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
};

struct MetaToolDef {
    const char *name;
    const char *description;
    QList<MetaToolSchemaInput> inputs;
};

const MetaToolDef kMetaTools[] = {
    { "list_algorithms",
      "List all available remote sensing and GIS processing algorithms.",
      {} },
    { "get_algorithm_schema",
      "Get the detailed input parameter JSON Schema for a specific algorithm.",
      { { "algorithm_id", "string", "Unique ID of the algorithm, e.g., 'qgis:rs_band_math'" } } },
    { "execute_algorithm",
      "Asynchronously run a processing algorithm with the specified parameters.",
      { { "algorithm_id", "string", "ID of the algorithm to execute" },
        { "parameters", "object", "Parameter name-value pairs for the algorithm" } } },
    { "get_execution_status",
      "Get progress, execution status, and results of an ongoing or completed algorithm execution.",
      { { "execution_id", "string", "The execution ID returned by execute_algorithm" } } },
    { "cancel_execution",
      "Cancel an actively running algorithm execution.",
      { { "execution_id", "string", "The execution ID of the run to cancel" } } },
    { "list_operators",
      "List all registered RSOperator algorithms (Agent-ready kernel: rs:/opencv:/gdal:/otb:).",
      {} },
    { "get_operator_schema",
      "Get JSON Schema and metadata for an RSOperator (e.g. 'rs:spectral_index').",
      { { "operator_id", "string", "Operator id, e.g. 'gdal:reproject'" } } },
    { "execute_operator",
      "Asynchronously run an RSOperator with JSON parameters. Returns execution_id.",
      { { "operator_id", "string", "Operator id to execute" },
        { "parameters", "object", "JSON parameter object" } } },
    { "list_layers",
      "List all raster and vector layers loaded in the current QGIS project.",
      {} },
    { "describe_dataset",
      "Get detailed layer metadata, including spatial extent, coordinate reference system (CRS), and band/field details.",
      { { "layer_id", "string", "Name or ID of the layer to describe" } } },
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
        required.append(QString::fromUtf8(input.name));
    }
    schema[QStringLiteral("properties")] = properties;
    if (!required.isEmpty())
        schema[QStringLiteral("required")] = required;
    return schema;
}

/// MCP response envelope for a TaskCenter task: mcpStatusForTask shape plus
/// the execution/algorithm identity echoed from the caller's execution id.
/// Shared by get_execution_status and cancel_execution's terminal fallback.
QVariantMap executionStatusResponse(const QString &executionId, const sicnu::AlgorithmTaskInfo &info)
{
    QVariantMap result = mcpStatusForTask(info);
    result[QStringLiteral("execution_id")] = executionId;
    result[QStringLiteral("algorithm_id")] = info.algorithmId;
    return result;
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
    std::string stdLine;
    while (!m_stopRequested && std::getline(std::cin, stdLine))
    {
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
    , mDispatcher(
          // rs: operator calls are typed Task Center submissions. autoLoad=false:
          // MCP has no canvas — results travel via get_execution_status.
          [](const QString &algorithmId, const QVariantMap &params) -> long {
              return sicnu::TaskCenter::instance().enqueueTask(
                  algorithmId, params, /*autoLoad=*/false, sicnu::TaskPriority::Normal,
                  QList<long>(), /*autoDispatch=*/true);
          },
          // MCP never uses completion callbacks; status is polled via
          // get_execution_status, so the watcher is a no-op.
          [](long, sicnu::processing::ToolCallDispatcher::CompletionCallback) {})
{
    ProcessingJobAdapter::registerProcessingJobExecutor();
}

McpServer::~McpServer()
{
    if (mReader)
    {
        mReader->requestStop();
        mReader->wait(3000);
    }
}

void McpServer::start(QCoreApplication *app)
{
    ProcessingJobAdapter::registerProcessingJobExecutor();
    mApp = app;
    mReader = new StdinReader(this);
    connect(mReader, &StdinReader::lineRead, this, &McpServer::onLineRead);
    mReader->start();
    SICNU_LOG_SUCCESS(SicnuLogTags::MCP, "MCP Server started on stdio");
}

void McpServer::onLineRead(const QString &line)
{
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
    QString method = request.value(QStringLiteral("method")).toString();
    QVariantMap params = request.value(QStringLiteral("params")).toMap();

    SICNU_LOG_INFO(SicnuLogTags::MCP, QString("MCP request: %1 (id=%2)").arg(method).arg(id.toString()));

    if (method == QStringLiteral("tools/list"))
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
        result[QStringLiteral("tools")] = tools;
        sendResponse(id, result);
    }
    else if (method == QStringLiteral("tools/call"))
    {
        QString toolName = params.value(QStringLiteral("name")).toString();
        QVariantMap arguments = params.value(QStringLiteral("arguments")).toMap();

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
            else if (toolName == QStringLiteral("get_algorithm_schema"))
            {
                resultData = handleGetAlgorithmSchema(arguments.value(QStringLiteral("algorithm_id")).toString());
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
            else if (toolName == QStringLiteral("list_layers"))
            {
                resultData = handleListLayers();
            }
            else if (toolName == QStringLiteral("describe_dataset"))
            {
                resultData = handleDescribeDataset(arguments.value(QStringLiteral("layer_id")).toString());
            }
            else
            {
                sendError(id, -32601, QStringLiteral("Method not found: ") + toolName);
                return;
            }

            QJsonDocument resultDoc = QJsonDocument::fromVariant(resultData);
            contentObj[QStringLiteral("text")] = QString::fromUtf8(resultDoc.toJson(QJsonDocument::Indented));
            contentList.append(contentObj);
            callResult[QStringLiteral("content")] = contentList;
            sendResponse(id, callResult);
        }
        catch (const std::exception &e)
        {
            sendError(id, -32000, QString::fromUtf8(e.what()));
        }
        catch (...)
        {
            sendError(id, -32000, QStringLiteral("Unknown error during tool execution"));
        }
    }
    else
    {
        sendError(id, -32601, QStringLiteral("Method not found: ") + method);
    }
}

void McpServer::sendResponse(const QVariant &id, const QVariantMap &result)
{
    QVariantMap response;
    response[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    response[QStringLiteral("id")] = id;
    response[QStringLiteral("result")] = result;

    QJsonDocument doc = QJsonDocument::fromVariant(response);
    std::cout << doc.toJson(QJsonDocument::Compact).constData() << std::endl;
}

void McpServer::sendError(const QVariant &id, int code, const QString &message)
{
    QVariantMap response;
    response[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    response[QStringLiteral("id")] = id;

    QVariantMap errorObj;
    errorObj[QStringLiteral("code")] = code;
    errorObj[QStringLiteral("message")] = message;
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

    QList<const QgsProcessingAlgorithm *> algs = QgsApplication::processingRegistry()->algorithms();
    for (const QgsProcessingAlgorithm *alg : algs)
    {
        if (!alg)
            continue;

        QVariantMap algMap;
        algMap[QStringLiteral("id")] = alg->id();
        algMap[QStringLiteral("displayName")] = alg->displayName();
        algMap[QStringLiteral("group")] = alg->group();
        algMap[QStringLiteral("tags")] = alg->tags();

        QVariantMap meta = alg->metadata();
        if (!meta.isEmpty())
        {
            algMap[QStringLiteral("metadata")] = meta;
        }

        algList.append(algMap);
    }

    result[QStringLiteral("algorithms")] = algList;
    return result;
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
        QVariantMap err;
        err[QStringLiteral("error")] = QStringLiteral("Operator not found: ") + operatorId;
        return err;
    }

    QVariantMap result = sicnu::processing::jsonObjectToVariantMap(op->schema());
    result[QStringLiteral("operator_id")] = operatorId;
    result[QStringLiteral("metadata")] = sicnu::processing::jsonObjectToVariantMap(op->metadata());
    return result;
}

bool McpServer::isAlgorithmIdAllowed(const QString &algorithmId, QString *reason)
{
    bool isCustom = false;
    if (idHasAllowedPrefix(algorithmId, &isCustom))
        return true;

    if (isCustom || algorithmId.startsWith(QStringLiteral("custom_tools:"))) {
        if (envFlagEnabled("SICNU_MCP_TRUST_CUSTOM_TOOLS"))
            return true;
        if (reason) {
            *reason = QStringLiteral(
                "Algorithm id '%1' is blocked by default (custom_tools). "
                "Set SICNU_MCP_TRUST_CUSTOM_TOOLS=1 to allow.").arg(algorithmId);
        }
        return false;
    }

    if (reason) {
        *reason = QStringLiteral(
            "Algorithm id '%1' is not in the MCP allow-list "
            "(rs:, gdal:, gdal_tools:, otb:).").arg(algorithmId);
    }
    return false;
}

bool McpServer::isOperatorIdAllowed(const QString &operatorId, QString *reason)
{
    bool isCustom = false;
    if (idHasAllowedPrefix(operatorId, &isCustom))
        return true;

    if (isCustom || operatorId.startsWith(QStringLiteral("custom_tools:"))) {
        if (envFlagEnabled("SICNU_MCP_TRUST_CUSTOM_TOOLS"))
            return true;
        if (reason) {
            *reason = QStringLiteral(
                "Operator id '%1' is blocked by default (custom_tools). "
                "Set SICNU_MCP_TRUST_CUSTOM_TOOLS=1 to allow.").arg(operatorId);
        }
        return false;
    }

    // Operators also use opencv: which is already in allowed prefixes.
    if (reason) {
        *reason = QStringLiteral(
            "Operator id '%1' is not in the MCP allow-list "
            "(rs:, gdal:, gdal_tools:, otb:, opencv:).").arg(operatorId);
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

QVariantMap McpServer::handleExecuteOperator(const QString &operatorId, const QVariantMap &parameters)
{
    SICNU_LOG_INFO(SicnuLogTags::MCP, QString("Executing operator: %1").arg(operatorId));

    QString denyReason;
    if (!isOperatorIdAllowed(operatorId, &denyReason)) {
        SICNU_LOG_ERROR(SicnuLogTags::MCP, denyReason);
        throw std::runtime_error(denyReason.toStdString());
    }
    if (!validateWorkspacePaths(parameters, &denyReason)) {
        SICNU_LOG_ERROR(SicnuLogTags::MCP, denyReason);
        throw std::runtime_error(denyReason.toStdString());
    }

    // rs: operator calls enter the Task Center through the ToolCallDispatcher,
    // which re-parses the envelope, normalizes the id, and validates required
    // descriptor inputs before the sink (ADR 0022). MCP never blocks on
    // completion — results travel via get_execution_status.
    Json::Value envelope(Json::objectValue);
    envelope["name"] = operatorId.toStdString();
    envelope["parameters"] = sicnu::processing::variantToJsonValue(parameters);

    const QString reason = mDispatcher.rejectionReason(envelope);
    if (!reason.isEmpty()) {
        // Historical contract: an unresolvable operator reports
        // "Operator not found: <id>" with the same text as before.
        if (reason.startsWith(QStringLiteral("Algorithm not registered:"))) {
            const QString msg = QStringLiteral("Operator not found: ") + operatorId;
            SICNU_LOG_ERROR(SicnuLogTags::MCP, msg);
            throw std::runtime_error(msg.toStdString());
        }
        SICNU_LOG_ERROR(SicnuLogTags::MCP, reason);
        throw std::runtime_error(reason.toStdString());
    }

    long taskId = -1;
    QString submitError;
    if (!mDispatcher.submit(envelope, {}, &submitError, &taskId) || taskId <= 0) {
        const QString msg = submitError.isEmpty()
            ? QStringLiteral("Failed to submit operator to Task Center")
            : submitError;
        SICNU_LOG_ERROR(SicnuLogTags::MCP, msg);
        throw std::runtime_error(msg.toStdString());
    }

    QVariantMap result;
    result[QStringLiteral("execution_id")] = toExecutionId(taskId);
    result[QStringLiteral("status")] = QStringLiteral("running");
    result[QStringLiteral("operator_id")] = operatorId;
    return result;
}

QVariantMap McpServer::handleGetAlgorithmSchema(const QString &algorithmId)
{
    std::unique_ptr<QgsProcessingAlgorithm> alg(QgsApplication::processingRegistry()->createAlgorithmById(algorithmId));
    if (!alg)
    {
        QVariantMap err;
        err[QStringLiteral("error")] = QStringLiteral("Algorithm not found: ") + algorithmId;
        return err;
    }

    return alg->toJsonSchema();
}

QVariantMap McpServer::handleExecuteAlgorithm(const QString &algorithmId, const QVariantMap &parameters)
{
    SICNU_LOG_INFO(SicnuLogTags::MCP, QString("Executing algorithm: %1").arg(algorithmId));

    QString denyReason;
    if (!isAlgorithmIdAllowed(algorithmId, &denyReason)) {
        SICNU_LOG_ERROR(SicnuLogTags::MCP, denyReason);
        throw std::runtime_error(denyReason.toStdString());
    }
    if (!validateWorkspacePaths(parameters, &denyReason)) {
        SICNU_LOG_ERROR(SicnuLogTags::MCP, denyReason);
        throw std::runtime_error(denyReason.toStdString());
    }

    QString lookupId = algorithmId;
    if (lookupId.startsWith(QStringLiteral("processing:"))) {
        lookupId = lookupId.mid(11);
    }

    bool exists = false;
    if (QgsApplication::processingRegistry()) {
        if (QgsApplication::processingRegistry()->algorithmById(lookupId)) {
            exists = true;
        }
    }
    if (!exists) {
        if (sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(lookupId.toStdString())) {
            exists = true;
        }
    }
    if (!exists) {
        if (sicnu::operators::RSOperatorRegistry::instance().create(lookupId.toStdString())) {
            exists = true;
        }
    }

    if (!exists) {
        const QString msg = QStringLiteral("Algorithm not found: ") + algorithmId;
        SICNU_LOG_ERROR(SicnuLogTags::MCP, msg);
        throw std::runtime_error(msg.toStdString());
    }

    // Provider algorithms (gdal:/otb:/qgis:) run directly in the Task Center
    // with autoDispatch so they progress to a terminal state (ADR 0022).
    QString effectiveAlgId = algorithmId;
    if (!effectiveAlgId.startsWith(QStringLiteral("processing:")) &&
        !effectiveAlgId.startsWith(QStringLiteral("rs:")))
    {
        effectiveAlgId = QString::fromStdString(ProcessingJobAdapter::processingAlgorithmId(algorithmId));
    }

    const long taskId = sicnu::TaskCenter::instance().enqueueTask(
        effectiveAlgId, parameters, /*autoLoad=*/false, sicnu::TaskPriority::Normal,
        QList<long>(), /*autoDispatch=*/true);

    QVariantMap result;
    result[QStringLiteral("execution_id")] = toExecutionId(taskId);
    result[QStringLiteral("status")] = QStringLiteral("running");
    return result;
}

QVariantMap McpServer::handleGetExecutionStatus(const QString &executionId)
{
    long taskId = 0;
    if (!parseExecutionId(executionId, &taskId))
        throw std::runtime_error("Execution ID not found: " + executionId.toStdString());

    const sicnu::AlgorithmTaskInfo info = sicnu::TaskCenter::instance().getTaskInfo(taskId);
    if (info.taskId != taskId)
        throw std::runtime_error("Execution ID not found: " + executionId.toStdString());

    return executionStatusResponse(executionId, info);
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

        return executionStatusResponse(executionId, info);
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
