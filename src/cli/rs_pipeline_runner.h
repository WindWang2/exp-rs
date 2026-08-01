/***************************************************************************
 * rs_pipeline_runner.h  —  Headless pipeline executor for RSOperators
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <functional>
#include <string>
#include <vector>

namespace sicnu::data { class DataManager; }

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
    void registerStepOutputs(long pipelineId);

    ProgressCallback m_progressCallback;
    LogCallback m_logCallback;
    sicnu::data::DataManager* m_dataManager = nullptr;
};

} // namespace sicnu::cli
