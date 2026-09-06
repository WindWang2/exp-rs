/***************************************************************************
 * gdal_pansharpen_operator.cpp
 ***************************************************************************/
#include "gdal_pansharpen_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/tools/tool_path_manager.h"

#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace sicnu::operators::gdal {

using params::fileExists;
using params::requireString;

Json::Value GdalPanSharpenOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["pan"] = makeRasterParam("pan", "Panchromatic raster (high resolution)");
    props["ms"] = makeRasterParam("ms", "Multispectral raster (to be sharpened)");
    props["output"] = makeOutputParam("output", "Output raster", "tif");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Output raster path");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"pan", "ms", "output"});
    stampDeterminismGrade(root, "tolerance");
    return root;
}

Json::Value GdalPanSharpenOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("pansharpen");
    meta["tags"].append("fusion");
    meta["purpose"] = "External gdal_pansharpen.py pan-sharpening.";
    meta["limitations"].append("Requires the GDAL utility to be configured in tool paths.");
    return meta;
}

Json::Value GdalPanSharpenOperator::executionEstimate() const {
    // External subprocess: memory is owned by the CLI process, not this job.
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = 0;
    return est;
}

Json::Value GdalPanSharpenOperator::run(const Json::Value& params,
                                        RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string panPath = requireString(params, "pan");
    const std::string msPath = requireString(params, "ms");
    const std::string outputPath = requireString(params, "output");

    if (!fileExists(panPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Panchromatic raster not found: " + panPath);
    }
    if (!fileExists(msPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Multispectral raster not found: " + msPath);
    }

    const QString program = ToolPathManager::instance().gdalToolPath(
        QStringLiteral( "gdal_pansharpen.py" ));
    if (program.isEmpty()) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "gdal_pansharpen.py is not configured; set the GDAL tool path first");
    }

    QStringList args;
    args << QString::fromStdString( panPath )
         << QString::fromStdString( msPath )
         << QString::fromStdString( outputPath )
         << QStringLiteral( "-r" ) << QStringLiteral( "bilinear" )
         << QStringLiteral( "-of" ) << QStringLiteral( "GTiff" )
         << QStringLiteral( "-co" ) << QStringLiteral( "COMPRESS=LZW" );

    // Best-effort output cleanup (#647): a canceled or failed subprocess must
    // not leave a truncated product behind looking like a result.
    struct OutputCleanup {
        QString path;
        bool committed = false;
        ~OutputCleanup() { if (!path.isEmpty() && !committed) QFile::remove(path); }
    } cleanup{ QString::fromStdString( outputPath ) };

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);
    if (!proc.waitForStarted(10000)) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Failed to start gdal_pansharpen.py: " + proc.errorString().toStdString());
    }

    // Graceful cancel ladder (ports the OtbOperatorBase pattern): terminate()
    // first so large GDAL writes can flush, then kill() after a grace period.
    const auto terminateGracefully = [&proc]() {
        proc.terminate();
        if (!proc.waitForFinished(5000))
            proc.kill();
        proc.waitForFinished(2000);
    };
    // Watchdog: a hung subprocess must not pin the JobEngine worker forever.
    QElapsedTimer watchdog;
    watchdog.start();
    const qint64 maxRuntimeMs = 60 * 60 * 1000;

    // Cooperative cancellation: poll in short waits so a Task Center cancel
    // terminates the subprocess instead of orphaning it.
    while (proc.state() == QProcess::Running) {
        try {
            context.throwIfCancelled();
        } catch (...) {
            terminateGracefully();
            throw;
        }
        if (watchdog.elapsed() > maxRuntimeMs) {
            terminateGracefully();
            throw RSOperatorError(ErrorCode::ComputationError,
                                  "gdal_pansharpen.py exceeded the maximum runtime (3600 s)"
                                  " and was terminated.");
        }
        if (proc.waitForFinished(200)) {
            break;
        }
        const QByteArray output = proc.readAllStandardOutput();
        if (!output.isEmpty()) {
            context.logInfo(QString::fromUtf8(output).toStdString());
        }
    }
    context.throwIfCancelled();

    const QByteArray remaining = proc.readAllStandardOutput();
    if (!remaining.isEmpty()) {
        context.logInfo(QString::fromUtf8(remaining).toStdString());
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "gdal_pansharpen.py failed with exit code "
                                  + std::to_string(proc.exitCode()));
    }

    cleanup.committed = true;
    context.reportProgress(1.0, "Pan-sharpening complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = "gdal_pansharp";
    return result;
}

} // namespace sicnu::operators::gdal
