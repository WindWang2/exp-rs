/***************************************************************************
 * rs_pipeline_runner.h  —  Headless pipeline executor for RSOperators
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QString>
#include <QVariantMap>

namespace sicnu::data { class DataManager; }
namespace sicnu::app { class ProjectContext; }
namespace sicnu::workflow { class WorkflowRun; }
class PluginHost;
class SicnuAppInterface;

namespace sicnu::cli {

/**
 * \brief Headless multi-step pipeline executor (ADR 0016).
 *
 * Converts CLI pipeline JSON into a TaskCenter DAG and runs it only through
 * `TaskCenter::submitPipeline` — no local step-index execution loop.
 *
 * Pipeline JSON format:
 * \code{.json}
 * {
 *   "name": "NDVI then smooth",
 *   "steps": [
 *     {"id": "s1", "operator": "rs:spectral_index", "params": {"input": "in.tif", "output": "ndvi.tif", "index": "NDVI"}},
 *     {"id": "s2", "operator": "opencv:gaussian_blur", "params": {"input": "$s1.output", "output": "smooth.tif", "kernelSize": 5}}
 *   ]
 * }
 * \endcode
 *
 * Sequential steps are parent-gated automatically. Optional `$stepId.output`
 * placeholders are resolved by TaskCenter before launching dependents.
 *
 * Progress/log callbacks remain for stdout / GUI / log file reporting.
 */
class RsPipelineRunner {
public:
    struct StepResult {
        std::string operatorName;
        Json::Value params;
        Json::Value result;
        bool success = false;
        std::string errorMessage;
        int errorCode = 0;
    };

    struct PipelineResult {
        bool success = false;
        std::vector<StepResult> steps;
        std::string errorMessage;
    };

    /**
     * \brief Callback types for progress and log reporting.
     */
    using ProgressCallback = std::function<void(int stepIndex, int totalSteps,
                                                double stepProgress,
                                                const std::string& message)>;
    using LogCallback = std::function<void(const std::string& level,
                                           const std::string& message)>;

    /**
     * \brief Construct a runner with optional callbacks.
     */
    explicit RsPipelineRunner(ProgressCallback progressCallback = {},
                              LogCallback logCallback = {});

    ~RsPipelineRunner();

    /**
     * \brief Register a Python plugin directory to load before running the pipeline.
     * Returns false (setting `errorOut`) for an empty or nonexistent directory.
     * Loading itself stays lazy — it happens on the first run.
     */
    bool addPythonPluginDirectory(const std::string& dirPath, std::string* errorOut = nullptr);

    /**
     * \brief Optional DataManager receiving completed step outputs as
     * TaskTemporary Data Assets (ADR 0023). Null by default: no registration.
     */
    void setAssetRegistry(sicnu::data::DataManager* dataManager);

    /**
     * \brief Run a pipeline from a JSON string.
     *
     * @param pipelineJson JSON object with a "steps" array.
     * @return PipelineResult with per-step outcomes.
     */
    PipelineResult runFromJson(const Json::Value& pipelineJson);

    /**
     * \brief Run a pipeline from a file path.
     *
     * @param filePath Path to a .json pipeline file.
     * @return PipelineResult with per-step outcomes.
     */
    PipelineResult runFromFile(const std::string& filePath);

    /**
     * \brief Resume an interrupted/crashed tracked run from its checkpoint
     * (#668 production surface, paired with the CLI --resume flag and the
     * MCP resume_workflow tool).
     *
     * Startup reconciliation runs first: checkpoints left in a Running state
     * by a crashed process are transitioned to Interrupted (idempotent for
     * already-terminal runs). Steps whose recorded output still exists on
     * disk are NOT re-executed; only the remaining steps are resubmitted.
     * The returned PipelineResult reports every step of the run (pre-completed
     * steps carry their recorded output).
     *
     * @param runId Tracked run id (see --list-runs / checkpoint files).
     * @return PipelineResult; errorMessage explains an unresumable run.
     */
    PipelineResult resumeRun(const std::string& runId);

    /**
     * \brief Validate a pipeline JSON without executing it.
     *
     * @param pipelineJson JSON object to validate.
     * @param errorMessage Receives validation error text on failure.
     * @return true if the pipeline is well-formed.
     */
    static bool validatePipelineJson(const Json::Value& pipelineJson,
                                     std::string* errorMessage = nullptr);

private:
    void reportProgress(int stepIndex, int totalSteps, double stepProgress,
                        const std::string& message) const;
    void reportLog(const std::string& level, const std::string& message) const;
    /// Registers one completed step's output file as a TaskTemporary asset and
    /// attaches its derivation record (shared by the fresh-run loop and the
    /// resume pre-resolved loop — adversarial review of #724). The workflow
    /// context (when the caller has one) is stamped into the derivation
    /// record's workflowId/workflowRunId/stepId (#727: lineage must carry the
    /// step relationship, not only a task reference).
    void registerOutputAsset(const QString &path, const QString &algorithmId,
                             const QVariantMap &parameterMap, const QString &taskReference,
                             const QString &workflowId = QString(),
                             const QString &workflowRunId = QString(),
                             const QString &stepId = QString());
    /// Registration order follows the data dependency (#727): checkpoint-served
    /// (pre-crash) assets and derivations first, fresh outputs after — a fresh
    /// downstream step's lineage can only resolve if its checkpoint-served
    /// input is already a registered asset. Registers every completed plan of
    /// @a run that has no task in the current (resume) pipeline.
    void registerCheckpointServedOutputs(const sicnu::workflow::WorkflowRun *run);
    /// Registers the FRESH pipeline's completed outputs (post-execution loop).
    void registerStepOutputs(long pipelineId);
    bool ensurePythonPluginsLoaded();
    /// Owns the run's DataManager (unless setAssetRegistry injected one) and
    /// wires both process-wide catalog seams on the owning thread. Called by
    /// every run entry point (fresh + resume), independent of the Python
    /// plugin stack (adversarial review of #724).
    bool ensureCatalogSeams();

    ProgressCallback m_progressCallback;
    LogCallback m_logCallback;
    sicnu::data::DataManager* m_dataManager = nullptr;
    std::unique_ptr<sicnu::data::DataManager> m_ownedDataManager;
    // Headless plugin stack (ADR 0023, TICKET-14), created lazily on first use.
    // Declaration order is destruction-safety: the PluginHost tears down first,
    // then the interface, then the context that owns the DataManager plugins see.
    // Members are shared_ptr (not unique_ptr) so the out-of-line destructor can
    // compile even when SICNU_EMBED_PYTHON is off and these types are incomplete
    // in this TU (shared_ptr does not require a complete type at destruction).
    std::shared_ptr<sicnu::app::ProjectContext> m_projectContext;
    std::shared_ptr<SicnuAppInterface> m_appInterface;
    std::shared_ptr<PluginHost> m_pluginHost;
    std::vector<std::string> m_pythonPluginDirs;
};

} // namespace sicnu::cli
