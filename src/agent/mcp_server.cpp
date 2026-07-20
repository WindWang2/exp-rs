#include "mcp_server.h"
#include "core/sicnu_logging.h"

#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator.h"

#include <iostream>
#include <QJsonDocument>
#include <qgis.h>
#include <json/json.h>

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
}
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

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
#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingfeedback.h>
#include <qgsfeedback.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsrectangle.h>
#include <qgsfields.h>

namespace {

bool envFlagEnabled(const char *name)
{
    const QByteArray v = qgetenv(name);
    if (v.isEmpty())
        return false;
    const QString s = QString::fromUtf8(v).trimmed().toLower();
    return s == QLatin1String("1") || s == QLatin1String("true") || s == QLatin1String("yes")
           || s == QLatin1String("on");
}

bool idHasAllowedPrefix(const QString &id, bool *isCustomTools = nullptr)
{
    if (isCustomTools)
        *isCustomTools = false;

    static const QStringList kAllowed = {
        QStringLiteral("rs:"),
        QStringLiteral("gdal:"),
        QStringLiteral("gdal_tools:"),
        QStringLiteral("otb:"),
        QStringLiteral("opencv:"), // operator surface uses opencv: filters
    };
    for (const QString &prefix : kAllowed) {
        if (id.startsWith(prefix))
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

} // namespace

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

// AlgorithmWorker implementation
AlgorithmWorker::AlgorithmWorker(const QString &execId, QgsProcessingAlgorithm *algo, const QVariantMap &parameters, std::shared_ptr<AlgorithmExecution> execState, QObject *parent)
    : QThread(parent)
    , mExecId(execId)
    , mAlgo(algo)
    , mParameters(parameters)
    , mState(execState)
{
}

AlgorithmWorker::~AlgorithmWorker()
{
    wait();
}

namespace {

QVariant jsonValueToVariant(const Json::Value &value);
Json::Value variantToJsonValue(const QVariant &value);

QVariantMap jsonObjectToVariantMap(const Json::Value &obj)
{
    QVariantMap map;
    if (!obj.isObject())
        return map;
    for (const auto &name : obj.getMemberNames()) {
        map.insert(QString::fromStdString(name), jsonValueToVariant(obj[name]));
    }
    return map;
}

QVariant jsonValueToVariant(const Json::Value &value)
{
    if (value.isNull())
        return QVariant();
    if (value.isBool())
        return value.asBool();
    if (value.isInt() || value.isUInt())
        return value.asInt();
    if (value.isInt64() || value.isUInt64())
        return static_cast<qlonglong>(value.asInt64());
    if (value.isDouble())
        return value.asDouble();
    if (value.isString())
        return QString::fromStdString(value.asString());
    if (value.isArray()) {
        QVariantList list;
        for (Json::ArrayIndex i = 0; i < value.size(); ++i)
            list.append(jsonValueToVariant(value[i]));
        return list;
    }
    if (value.isObject())
        return jsonObjectToVariantMap(value);
    return QVariant();
}

Json::Value variantToJsonValue(const QVariant &value)
{
    if (!value.isValid() || value.isNull())
        return Json::Value(Json::nullValue);

    switch (value.userType()) {
    case QMetaType::Bool:
        return Json::Value(value.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return Json::Value(static_cast<Json::Int64>(value.toLongLong()));
    case QMetaType::Double:
    case QMetaType::Float:
        return Json::Value(value.toDouble());
    case QMetaType::QString:
        return Json::Value(value.toString().toStdString());
    case QMetaType::QVariantList: {
        Json::Value arr(Json::arrayValue);
        const QVariantList list = value.toList();
        for (const QVariant &item : list)
            arr.append(variantToJsonValue(item));
        return arr;
    }
    case QMetaType::QStringList: {
        Json::Value arr(Json::arrayValue);
        const QStringList list = value.toStringList();
        for (const QString &item : list)
            arr.append(item.toStdString());
        return arr;
    }
    case QMetaType::QVariantMap: {
        Json::Value obj(Json::objectValue);
        const QVariantMap map = value.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            obj[it.key().toStdString()] = variantToJsonValue(it.value());
        return obj;
    }
    default:
        // Fallback: stringify unknown types
        return Json::Value(value.toString().toStdString());
    }
}

/**
 * Background worker that runs an RSOperator without blocking the MCP I/O thread.
 */
class OperatorWorker : public QThread
{
public:
    OperatorWorker(const QString &execId,
                   const QString &operatorId,
                   const QVariantMap &parameters,
                   std::shared_ptr<AlgorithmExecution> execState,
                   std::shared_ptr<std::atomic<bool>> cancelFlag,
                   QObject *parent = nullptr)
        : QThread(parent)
        , mExecId(execId)
        , mOperatorId(operatorId)
        , mParameters(parameters)
        , mState(std::move(execState))
        , mCancelFlag(std::move(cancelFlag))
    {
    }

    ~OperatorWorker() override { wait(); }

protected:
    void run() override
    {
        try {
            auto op = sicnu::operators::RSOperatorRegistry::instance().create(
                mOperatorId.toStdString());
            if (!op) {
                QMutexLocker locker(&mState->mutex);
                mState->error = QStringLiteral("Operator not found: ") + mOperatorId;
                mState->completed = true;
                return;
            }

            sicnu::operators::RSOperatorContext context;
            auto state = mState;
            context.setCancelFlag(mCancelFlag.get());
            context.setProgressCallback([state](double progress, const std::string &message) {
                QMutexLocker locker(&state->mutex);
                state->progress = progress;
                state->progressText = QString::fromStdString(message);
            });

            const Json::Value params = variantToJsonValue(mParameters);
            const Json::Value result = op->execute(params, context);

            QMutexLocker locker(&mState->mutex);
            if (mState->canceled || (mCancelFlag && mCancelFlag->load())) {
                mState->canceled = true;
                mState->completed = true;
                return;
            }
            mState->result = jsonObjectToVariantMap(result.isObject() ? result : Json::Value(Json::objectValue));
            if (!result.isObject()) {
                mState->result[QStringLiteral("value")] = jsonValueToVariant(result);
            }
            mState->progress = 1.0;
            mState->completed = true;
        } catch (const sicnu::operators::RSOperatorError &e) {
            QMutexLocker locker(&mState->mutex);
            if (e.code() == sicnu::operators::ErrorCode::Cancelled) {
                mState->canceled = true;
            } else {
                mState->error = QString::fromStdString(e.message());
            }
            mState->completed = true;
        } catch (const std::exception &e) {
            QMutexLocker locker(&mState->mutex);
            mState->error = QString::fromUtf8(e.what());
            mState->completed = true;
        } catch (...) {
            QMutexLocker locker(&mState->mutex);
            mState->error = QStringLiteral("Unknown operator error");
            mState->completed = true;
        }
    }

private:
    QString mExecId;
    QString mOperatorId;
    QVariantMap mParameters;
    std::shared_ptr<AlgorithmExecution> mState;
    std::shared_ptr<std::atomic<bool>> mCancelFlag;
};

} // namespace

class McpFeedback : public QgsProcessingFeedback
{
public:
    McpFeedback(std::shared_ptr<AlgorithmExecution> state) : mState(state) {}

    void setProgressText(const QString &text) override {
        QgsProcessingFeedback::setProgressText(text);
        QMutexLocker locker(&mState->mutex);
        mState->progressText = text;
    }

    void reportError(const QString &error, bool fatalError = false) override {
        QgsProcessingFeedback::reportError(error, fatalError);
        QMutexLocker locker(&mState->mutex);
        mState->error = error;
    }

    void pushInfo(const QString &info) override {
        QgsProcessingFeedback::pushInfo(info);
        QMutexLocker locker(&mState->mutex);
        if (mState->progressText.isEmpty() || mState->progressText == info) {
            mState->progressText = info;
        }
    }

private:
    std::shared_ptr<AlgorithmExecution> mState;
};

void AlgorithmWorker::run()
{
    QgsProcessingContext context;
    McpFeedback feedback(mState);

    // Track progress and check cancellation (DirectConnection since we only write to mutex-protected state)
    connect(&feedback, &QgsFeedback::progressChanged, this, [this, &feedback](double p) {
        QMutexLocker locker(&mState->mutex);
        mState->progress = p;
        // Propagate MCP cancellation to feedback
        if (mState->canceled && !feedback.isCanceled()) {
            feedback.cancel();
        }
    }, Qt::DirectConnection);

    bool ok = false;
    try {
        QVariantMap res = mAlgo->run(mParameters, context, &feedback, &ok);
        QMutexLocker locker(&mState->mutex);
        mState->result = res;
        mState->completed = true;
        if (!ok && mState->error.isEmpty())
        {
            mState->error = QStringLiteral("Algorithm run reported failure (ok is false)");
        }
    } catch (const std::exception &e) {
        QMutexLocker locker(&mState->mutex);
        mState->error = QString::fromUtf8(e.what());
        mState->completed = true;
    } catch (...) {
        QMutexLocker locker(&mState->mutex);
        mState->error = QStringLiteral("Unknown exception occurred during execution");
        mState->completed = true;
    }
}

// McpServer implementation
McpServer::McpServer(QObject *parent)
    : QObject(parent)
{
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

        // list_algorithms tool
        QVariantMap listAlgTool;
        listAlgTool[QStringLiteral("name")] = QStringLiteral("list_algorithms");
        listAlgTool[QStringLiteral("description")] = QStringLiteral("List all available remote sensing and GIS processing algorithms.");
        listAlgTool[QStringLiteral("inputSchema")] = QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap()}
        };
        tools.append(listAlgTool);

        // get_algorithm_schema tool
        QVariantMap schemaTool;
        schemaTool[QStringLiteral("name")] = QStringLiteral("get_algorithm_schema");
        schemaTool[QStringLiteral("description")] = QStringLiteral("Get the detailed input parameter JSON Schema for a specific algorithm.");
        schemaTool[QStringLiteral("inputSchema")] = QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap{
                {QStringLiteral("algorithm_id"), QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("description"), QStringLiteral("Unique ID of the algorithm, e.g., 'qgis:rs_band_math'")}
                }}
            }},
            {QStringLiteral("required"), QStringList{QStringLiteral("algorithm_id")}}
        };
        tools.append(schemaTool);

        // execute_algorithm tool
        QVariantMap execTool;
        execTool[QStringLiteral("name")] = QStringLiteral("execute_algorithm");
        execTool[QStringLiteral("description")] = QStringLiteral("Asynchronously run a processing algorithm with the specified parameters.");
        execTool[QStringLiteral("inputSchema")] = QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap{
                {QStringLiteral("algorithm_id"), QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("description"), QStringLiteral("ID of the algorithm to execute")}
                }},
                {QStringLiteral("parameters"), QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("description"), QStringLiteral("Parameter name-value pairs for the algorithm")}
                }}
            }},
            {QStringLiteral("required"), QStringList{QStringLiteral("algorithm_id"), QStringLiteral("parameters")}}
        };
        tools.append(execTool);

        // get_execution_status tool
        QVariantMap statusTool;
        statusTool[QStringLiteral("name")] = QStringLiteral("get_execution_status");
        statusTool[QStringLiteral("description")] = QStringLiteral("Get progress, execution status, and results of an ongoing or completed algorithm execution.");
        statusTool[QStringLiteral("inputSchema")] = QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap{
                {QStringLiteral("execution_id"), QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("description"), QStringLiteral("The execution ID returned by execute_algorithm")}
                }}
            }},
            {QStringLiteral("required"), QStringList{QStringLiteral("execution_id")}}
        };
        tools.append(statusTool);

        // cancel_execution tool
        QVariantMap cancelTool;
        cancelTool[QStringLiteral("name")] = QStringLiteral("cancel_execution");
        cancelTool[QStringLiteral("description")] = QStringLiteral("Cancel an actively running algorithm execution.");
        cancelTool[QStringLiteral("inputSchema")] = QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap{
                {QStringLiteral("execution_id"), QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("description"), QStringLiteral("The execution ID of the run to cancel")}
                }}
            }},
            {QStringLiteral("required"), QStringList{QStringLiteral("execution_id")}}
        };
        tools.append(cancelTool);

        // list_operators tool (RSOperator kernel)
        QVariantMap listOpsTool;
        listOpsTool[QStringLiteral("name")] = QStringLiteral("list_operators");
        listOpsTool[QStringLiteral("description")] = QStringLiteral(
            "List all registered RSOperator algorithms (Agent-ready kernel: rs:/opencv:/gdal:/otb:).");
        listOpsTool[QStringLiteral("inputSchema")] = QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap()}
        };
        tools.append(listOpsTool);

        // get_operator_schema tool
        QVariantMap opSchemaTool;
        opSchemaTool[QStringLiteral("name")] = QStringLiteral("get_operator_schema");
        opSchemaTool[QStringLiteral("description")] = QStringLiteral(
            "Get JSON Schema and metadata for an RSOperator (e.g. 'rs:spectral_index').");
        opSchemaTool[QStringLiteral("inputSchema")] = QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap{
                {QStringLiteral("operator_id"), QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("description"), QStringLiteral("Operator id, e.g. 'gdal:reproject'")}
                }}
            }},
            {QStringLiteral("required"), QStringList{QStringLiteral("operator_id")}}
        };
        tools.append(opSchemaTool);

        // execute_operator tool
        QVariantMap execOpTool;
        execOpTool[QStringLiteral("name")] = QStringLiteral("execute_operator");
        execOpTool[QStringLiteral("description")] = QStringLiteral(
            "Asynchronously run an RSOperator with JSON parameters. Returns execution_id.");
        execOpTool[QStringLiteral("inputSchema")] = QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap{
                {QStringLiteral("operator_id"), QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("description"), QStringLiteral("Operator id to execute")}
                }},
                {QStringLiteral("parameters"), QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("description"), QStringLiteral("JSON parameter object")}
                }}
            }},
            {QStringLiteral("required"), QStringList{QStringLiteral("operator_id"), QStringLiteral("parameters")}}
        };
        tools.append(execOpTool);

        // list_layers tool
        QVariantMap listLayersTool;
        listLayersTool[QStringLiteral("name")] = QStringLiteral("list_layers");
        listLayersTool[QStringLiteral("description")] = QStringLiteral("List all raster and vector layers loaded in the current QGIS project.");
        listLayersTool[QStringLiteral("inputSchema")] = QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap()}
        };
        tools.append(listLayersTool);

        // describe_dataset tool
        QVariantMap describeTool;
        describeTool[QStringLiteral("name")] = QStringLiteral("describe_dataset");
        describeTool[QStringLiteral("description")] = QStringLiteral("Get detailed layer metadata, including spatial extent, coordinate reference system (CRS), and band/field details.");
        describeTool[QStringLiteral("inputSchema")] = QVariantMap{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QVariantMap{
                {QStringLiteral("layer_id"), QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("description"), QStringLiteral("Name or ID of the layer to describe")}
                }}
            }},
            {QStringLiteral("required"), QStringList{QStringLiteral("layer_id")}}
        };
        tools.append(describeTool);

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
            opMap[QStringLiteral("metadata")] = jsonObjectToVariantMap(meta);
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

    QVariantMap result = jsonObjectToVariantMap(op->schema());
    result[QStringLiteral("operator_id")] = operatorId;
    result[QStringLiteral("metadata")] = jsonObjectToVariantMap(op->metadata());
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

    if (!sicnu::operators::RSOperatorRegistry::instance().hasOperator(operatorId.toStdString())) {
        SICNU_LOG_ERROR(SicnuLogTags::MCP, QString("Operator not found: %1").arg(operatorId));
        throw std::runtime_error("Operator not found: " + operatorId.toStdString());
    }

    mMutex.lock();
    mExecutionCounter++;
    QString execId = QStringLiteral("op_exec_%1").arg(mExecutionCounter);
    auto state = std::make_shared<AlgorithmExecution>();
    state->id = execId;
    state->algorithmId = operatorId;
    mExecutions[execId] = state;
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    mOperatorCancelFlags[execId] = cancelFlag;
    mMutex.unlock();

    OperatorWorker *worker = new OperatorWorker(execId, operatorId, parameters, state, cancelFlag, this);
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();

    QVariantMap result;
    result[QStringLiteral("execution_id")] = execId;
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

    QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->createAlgorithmById(algorithmId);
    if (!alg)
    {
        SICNU_LOG_ERROR(SicnuLogTags::MCP, QString("Algorithm not found: %1").arg(algorithmId));
        throw std::runtime_error("Algorithm not found: " + algorithmId.toStdString());
    }

    mMutex.lock();
    mExecutionCounter++;
    QString execId = QStringLiteral("exec_%1").arg(mExecutionCounter);
    auto state = std::make_shared<AlgorithmExecution>();
    state->id = execId;
    state->algorithmId = algorithmId;
    mExecutions[execId] = state;
    mMutex.unlock();

    // Start Worker
    AlgorithmWorker *worker = new AlgorithmWorker(execId, alg, parameters, state, this);
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();

    QVariantMap result;
    result[QStringLiteral("execution_id")] = execId;
    result[QStringLiteral("status")] = QStringLiteral("running");
    return result;
}

QVariantMap McpServer::handleGetExecutionStatus(const QString &executionId)
{
    QMutexLocker locker(&mMutex);
    if (!mExecutions.contains(executionId))
    {
        throw std::runtime_error("Execution ID not found: " + executionId.toStdString());
    }

    auto state = mExecutions[executionId];
    QMutexLocker stateLocker(&state->mutex);
    QVariantMap result;
    result[QStringLiteral("execution_id")] = state->id;
    result[QStringLiteral("algorithm_id")] = state->algorithmId;
    result[QStringLiteral("progress")] = state->progress;
    result[QStringLiteral("progressText")] = state->progressText;

    if (!state->error.isEmpty())
    {
        result[QStringLiteral("status")] = QStringLiteral("failed");
        result[QStringLiteral("error")] = state->error;
    }
    else if (state->completed)
    {
        result[QStringLiteral("status")] = QStringLiteral("completed");
        result[QStringLiteral("result")] = state->result;
    }
    else if (state->canceled)
    {
        result[QStringLiteral("status")] = QStringLiteral("canceled");
    }
    else
    {
        result[QStringLiteral("status")] = QStringLiteral("running");
    }

    return result;
}

QVariantMap McpServer::handleCancelExecution(const QString &executionId)
{
    QMutexLocker locker(&mMutex);
    if (!mExecutions.contains(executionId))
    {
        throw std::runtime_error("Execution ID not found: " + executionId.toStdString());
    }

    auto state = mExecutions[executionId];
    {
        QMutexLocker stateLocker(&state->mutex);
        state->canceled = true;
    }
    if (mOperatorCancelFlags.contains(executionId)) {
        mOperatorCancelFlags[executionId]->store(true);
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
