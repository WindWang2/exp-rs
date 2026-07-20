/***************************************************************************
 * otb_operator_base.h  —  Base class for OTB CLI-based RSOperators
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator.h"

#include <QString>
#include <QStringList>
#include <QProcessEnvironment>

#include <json/json.h>

#include <vector>

namespace sicnu::operators::otb {

// Re-export shared JSON helpers for subclasses (same call style as before).
using params::fileExists;
using params::requireString;
using params::getString;
using params::getInt;
using params::getDouble;
using params::getBool;
using params::getEnum;
using params::getStringArray;

/**
 * Base class for operators that delegate execution to an external OTB CLI
 * application (otbcli_*).
 *
 * Subclasses define:
 *   - otbApplicationName()   e.g. "Segmentation"
 *   - buildOtbArgs()         convert JSON params to OTB CLI arguments
 *   - optionally buildResult() to customize the JSON return value
 *
 * The base class handles:
 *   - Locating the OTB CLI binary via ToolPathManager
 *   - Setting OTB_APPLICATION_PATH, PATH and LC_NUMERIC=C environment
 *   - Running the process without blocking the caller's event loop
 *   - Forwarding stdout/stderr as log messages
 *   - Parsing "N%" progress tokens from OTB output
 *   - Cooperative cancellation via context.isCancelled()
 *   - Converting process failures into typed RSOperatorError exceptions
 */
class OtbOperatorBase : public RSOperator {
public:
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;

protected:
    /** OTB application name without the "otbcli_" prefix, e.g. "Segmentation". */
    virtual QString otbApplicationName() const = 0;

    /** Build the argument list for the OTB CLI from JSON parameters. */
    virtual QStringList buildOtbArgs(const Json::Value& params,
                                     RSOperatorContext& context) const = 0;

    /**
     * Build the result JSON object after a successful OTB run.
     * Default implementation returns { "output": params["output"] }.
     */
    virtual Json::Value buildResult(const Json::Value& params,
                                    RSOperatorContext& context) const;

    /** Validates that params is an object and contains required string "output". */
    void validateCommonParams(const Json::Value& params) const;

    /**
     * Builds a JSON Schema root from parameter/output descriptions.
     * Automatically prepends "output" to @p required.
     */
    Json::Value buildSchema(const std::string& title,
                            const std::string& description,
                            const Json::Value& params,
                            const Json::Value& outputs,
                            const std::vector<std::string>& required) const;

private:
    bool runOtbProcess(const QString& program, const QStringList& args,
                       RSOperatorContext& context);

    QProcessEnvironment buildProcessEnvironment(const QString& toolPath) const;

    /** Attempt to parse a progress percentage from an OTB log line. */
    bool parseProgress(const QString& line, double& progress) const;
};

} // namespace sicnu::operators::otb
