#include "rs_pipeline_runner.h"

#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/framework/algorithm_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "jobs/job_engine.h"
#include "processing/framework/task_center.h"
#include "python/isolated/python_plugin_host.h"
#include "data/data_manager.h"
#include "data/execution_fingerprint.h"
#include "workflow/workflow_run_coordinator.h"
#include "workflow/workflow_checkpoint.h"
#include "processing/framework/algorithm_meta_store.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>

#include <csignal>
#include <iostream>
#include <memory>

#include <qgsapplication.h>
#include "app/app_paths.h"

using namespace sicnu::cli;
namespace operators = sicnu::operators;

// g_cliInterrupted / cliIsInterrupted are defined in rs_pipeline_runner.cpp
// so targets that link the runner without the CLI main() still resolve them (#455).
namespace sicnu::cli {
extern volatile sig_atomic_t g_cliInterrupted;
} // namespace sicnu::cli

namespace {
void handleSignal( int )
{
    sicnu::cli::g_cliInterrupted = 1;
}

struct ShutdownGuard {
    ~ShutdownGuard() {
        sicnu::TaskCenter::instance().shutdown();
        sicnu::jobs::JobEngine::instance().shutdown();
        QgsApplication::exitQgis();
    }
};
}

int main(int argc, char *argv[])
{
    std::signal( SIGINT, handleSignal );
    std::signal( SIGTERM, handleSignal );

    QgsApplication app(argc, argv, false);
    app.setApplicationName("sicnu_geo_rs_cli");
    QCoreApplication::setApplicationVersion("0.9.2-dev");
    ShutdownGuard shutdownGuard;

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "SICNU GEO RS — headless pipeline executor (TaskCenter).\n"
        "Runs a JSON-defined DAG of algorithm steps without GUI via TaskCenter.");
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption pipelineOption(
        QStringList() << "p" << "pipeline",
        "Path to pipeline JSON file.",
        "file");
    parser.addOption(pipelineOption);

    const QCommandLineOption listOption(
        QStringList() << "l" << "list",
        "List all registered operators and exit.");
    parser.addOption(listOption);

    const QCommandLineOption schemaOption(
        QStringList() << "s" << "schema",
        "Print JSON Schema for a specific operator and exit.",
        "operator");
    parser.addOption(schemaOption);

    const QCommandLineOption pythonPluginOption(
        QStringList() << "python-plugin",
        "Load the Python plugin directory before running the pipeline (repeatable).",
        "dir");
    parser.addOption(pythonPluginOption);

    const QCommandLineOption listRunsOption(
        QStringList() << "list-runs",
        "List checkpointed workflow runs (id, state, steps) and exit. Runs left "
        "Running by a crashed process are shown as interrupted.");
    parser.addOption(listRunsOption);

    const QCommandLineOption resumeOption(
        QStringList() << "resume",
        "Resume a checkpointed run by id (see --list-runs): steps whose recorded "
        "output still exists are not re-executed.",
        "run_id");
    parser.addOption(resumeOption);

    const QCommandLineOption noCacheOption(
        QStringList() << "no-execution-cache",
        "Disable the revision-aware execution cache for this run (it is enabled "
        "via SICNU_EXECUTION_CACHE=1). Re-executes every deterministic step.");
    parser.addOption(noCacheOption);

    const QCommandLineOption exportCatalogOption(
        QStringList() << "export-catalog",
        "Regenerate the algorithm catalog sidecars from the descriptor-resolved "
        "registry state (ADR 0122 / #707: descriptors are the single source of "
        "truth; shipped sidecars are generated artifacts, never hand-edited). "
        "Writes one <id>.json per algorithm into the given directory.",
        "dir");
    parser.addOption(exportCatalogOption);

    parser.process(app);

    if (parser.isSet(noCacheOption)) {
        sicnu::data::ExecutionResultCache::instance().setEnabled(false);
    }

    ensureGdalInit();

    // Bootstrap QgsApplication on main thread before JobEngine workers can log
    // (issue #365: first QgsMessageLog from worker lazily constructs
    // QgsApplication::members() off main thread → Q_ASSERT → SIGABRT).
    QgsApplication::setPrefixPath(AppPaths::prefixPath(), true);
    QgsApplication::initQgis();

    // Initialize AlgorithmEngine facade (registers providers and tool paths)
    sicnu::AlgorithmEngine::instance().initialize();

    // ADR 0062: bridge the unified registry to JobEngine so provider algorithms (gdal:/otb:/qgis:)
    // become executable when submitted as jobs.
    sicnu::jobs::JobEngine::instance().setFallbackExecutor(
        []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx ) {
            const auto adapter = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( req.algorithmId );
            if ( !adapter )
                throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
            sicnu::processing::ProgressCallback progressBridge;
            progressBridge = [&ctx]( int percent, const std::string &message ) {
                ctx.reportProgress( percent / 100.0, message );
            };
            return adapter->execute( req.params, progressBridge,
                                     [&ctx]() { return ctx.isCancelled(); } );
        } );

    // List operators
    if (parser.isSet(listOption)) {
        const auto names = operators::RSOperatorRegistry::instance().operatorNames();
        std::cout << "Registered operators (" << names.size() << "):\n";
        for (const auto& name : names) {
            std::cout << "  " << name << "\n";
        }
        return 0;
    }

    // Print operator schema
    if (parser.isSet(schemaOption)) {
        const std::string opName = parser.value(schemaOption).toStdString();
        auto op = operators::RSOperatorRegistry::instance().create(opName);
        if (!op) {
            std::cerr << "Unknown operator: " << opName << "\n";
            return 1;
        }
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "  ";
        std::cout << Json::writeString(builder, op->schema()) << "\n";
        return 0;
    }

    // Regenerate the algorithm catalog sidecars from descriptors (#707).
    if (parser.isSet(exportCatalogOption)) {
        const QString outDir = parser.value(exportCatalogOption);
        if (!QDir().mkpath(outDir)) {
            std::cerr << "Cannot create output directory: " << outDir.toStdString() << "\n";
            return 1;
        }
        auto &store = sicnu::processing::AlgorithmMetaStore::instance();
        store.loadDefaults();
        const auto descriptors =
            sicnu::processing::AtomicAlgorithmRegistry::instance().listDescriptors();
        int written = 0;
        for (const auto &desc : descriptors) {
            sicnu::processing::AlgorithmMetaEntry entry =
                store.resolveAgainstDescriptor(desc.id, desc.agentMetadata, nullptr)
                    .value_or(sicnu::processing::AlgorithmMetaEntry{});
            entry.id = desc.id;
            // Fill entry fields from the descriptor when the sidecar overlay
            // left them unset: the generated artifact is the descriptor's
            // resolved truth, not a copy of the previous file.
            if (entry.task.empty())
                entry.task = desc.agentMetadata.taskFamily;
            if (entry.notes.empty())
                entry.notes = desc.agentMetadata.notes;
            if (entry.tags.empty())
                entry.tags = desc.agentMetadata.tags;
            if (desc.agentMetadata.gpuDeclared) {
                entry.gpu = desc.agentMetadata.gpuAccelerated;
                entry.gpuDeclared = true;
            }
            // Primary input/output contract: the first RASTER/VECTOR port
            // (schema property order puts scalars like band numbers first —
            // dataTypeToString of those would claim "Integer" as the data
            // contract). Empty when no data port exists.
            const auto primaryDataKind = []( const std::vector<sicnu::processing::PortDescriptor> &ports ) {
                for ( const auto &port : ports )
                {
                    const std::string kind = sicnu::processing::dataTypeToString( port.type );
                    if ( kind == "Raster" )
                        return std::string( "raster" );
                    if ( kind == "Vector" )
                        return std::string( "vector" );
                }
                return std::string();
            };
            if ( entry.input.empty() )
                entry.input = primaryDataKind( desc.inputs );
            if ( entry.output.empty() )
                entry.output = primaryDataKind( desc.outputs );
            // Only ship files that carry at least one catalog-worthy fact:
            // id + defaults alone would be noise for every thin adapter.
            const bool hasContent = !entry.task.empty() || !entry.notes.empty()
                                    || !entry.tags.empty() || entry.gpuDeclared
                                    || entry.accuracy >= 0.0;
            if (!hasContent)
                continue;
            // Naming contract from ADR 0122: ':' AND '_' both map to '-'
            // (rs:spectral_index -> rs-spectral-index), matching the
            // hand-authored files this generator replaces.
            const QString fileName =
                QString::fromStdString(desc.id)
                    .replace(QLatin1Char(':'), QLatin1Char('-'))
                    .replace(QLatin1Char('_'), QLatin1Char('-'))
                + QStringLiteral(".json");
            QFile f(QDir(outDir).filePath(fileName));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                std::cerr << "Cannot write " << f.fileName().toStdString() << "\n";
                return 1;
            }
            Json::StreamWriterBuilder w;
            w["indentation"] = "  ";
            const std::string text = Json::writeString(w, entry.toJson());
            f.write(text.c_str());
            ++written;
        }
        std::cout << "Generated " << written << " catalog sidecars in "
                  << outDir.toStdString() << "\n";
        return 0;
    }

    // List checkpointed runs (--list-runs, #668): recover-then-list so a
    // checkpoint left Running by a crashed process shows as interrupted.
    if (parser.isSet(listRunsOption)) {
        auto &coordinator = sicnu::workflow::WorkflowRunCoordinator::instance();
        const auto recovery = coordinator.recoverAtStartup(/*autoResume=*/false);
        const QString dir = coordinator.checkpointDirectory();
        std::cout << "Checkpointed runs in " << dir.toStdString() << ":\n";
        const QStringList checkpoints =
            sicnu::workflow::WorkflowCheckpointManager().listCheckpoints(dir);
        if (checkpoints.isEmpty()) {
            std::cout << "  (none)\n";
            return 0;
        }
        for (const QString &file : checkpoints) {
            QString loadError;
            auto run = sicnu::workflow::WorkflowCheckpointManager().loadCheckpoint(
                file, &loadError);
            if (!run) {
                std::cout << "  <un-readable checkpoint: "
                          << QFileInfo(file).fileName().toStdString() << " — "
                          << loadError.toStdString() << ">\n";
                continue;
            }
            const auto plans = run->stepPlans();
            int completed = 0;
            for (const auto &plan : plans)
                completed += (plan.status == "Completed") ? 1 : 0;
            std::cout << "  " << run->runId() << "  state="
                      << sicnu::workflow::workflowRunStateToString(run->state())
                      << "  steps=" << plans.size() << " (completed " << completed
                      << ")  workflow=" << run->workflowId() << "\n";
        }
        if (!recovery.errors.isEmpty()) {
            for (const QString &note : recovery.errors)
                std::cerr << "recovery warning: " << note.toStdString() << "\n";
        }
        return 0;
    }

    // Resume a checkpointed run (--resume <run_id>, #668 production surface).
    const QString resumeRunId = parser.value(resumeOption);
    if (!resumeRunId.isEmpty()) {
        auto progressCb = [](int stepIndex, int totalSteps, double stepProgress,
                             const std::string& message) {
            const int percent = static_cast<int>(stepProgress * 100.0);
            std::cout << "[Step " << (stepIndex + 1) << "/" << totalSteps
                      << " " << percent << "%] " << message << "\n";
        };
        auto logCb = [](const std::string& level, const std::string& message) {
            std::cout << "[" << level << "] " << message << "\n";
        };
        RsPipelineRunner runner(progressCb, logCb);
        const auto result = runner.resumeRun(resumeRunId.toStdString());
        if (!result.success) {
            std::cerr << "Resume failed: " << result.errorMessage << "\n";
            return 1;
        }
        std::cout << "Resumed run succeeded (" << result.steps.size() << " steps)\n";
        return 0;
    }

    // Run pipeline
    const QString pipelinePath = parser.value(pipelineOption);
    if (pipelinePath.isEmpty()) {
        std::cerr << "No pipeline specified. Use --pipeline <file.json>\n";
        parser.showHelp(1);
    }

    auto progressCb = [](int stepIndex, int totalSteps, double stepProgress,
                         const std::string& message) {
        const int percent = static_cast<int>(stepProgress * 100.0);
        std::cout << "[Step " << (stepIndex + 1) << "/" << totalSteps
                  << " " << percent << "%] " << message << "\n";
    };

    auto logCb = [](const std::string& level, const std::string& message) {
        std::cout << "[" << level << "] " << message << "\n";
    };

    RsPipelineRunner runner(progressCb, logCb);
    for (const QString& dir : parser.values(pythonPluginOption)) {
        std::string error;
        if (!runner.addPythonPluginDirectory(dir.toStdString(), &error)) {
            std::cerr << "Invalid Python plugin directory: " << error << "\n";
            return 1;
        }
    }

    const auto result = runner.runFromFile(pipelinePath.toStdString());

    if (!result.success) {
        std::cerr << "Pipeline failed: " << result.errorMessage << "\n";
        return 1;
    }

    std::cout << "Pipeline succeeded (" << result.steps.size() << " steps)\n";
    return 0;
}
