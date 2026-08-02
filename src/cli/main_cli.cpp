#include "rs_pipeline_runner.h"

#include "operators/framework/rs_operator_registry.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "python/isolated/python_plugin_host.h"
#include "data/data_manager.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>

#include <iostream>
#include <memory>

using namespace sicnu::cli;
namespace operators = sicnu::operators;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("sicnu_geo_rs_cli");
    QCoreApplication::setApplicationVersion("0.9.2-dev");

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

    parser.process(app);

    ensureGdalInit();

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
