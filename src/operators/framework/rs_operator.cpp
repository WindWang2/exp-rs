/***************************************************************************
 * rs_operator.cpp  —  Default implementations for RSOperator
 ***************************************************************************/
#include "rs_operator.h"
#include "rs_operation_logger.h"

namespace sicnu::operators {
namespace {

// #1578: setRange declarations used to be display-only metadata — 72
// declarations across the operator families were never checked at runtime,
// so an out-of-range knob reached the algorithm (or an otbcli subprocess)
// as an unbounded value instead of a typed InvalidParameter refusal.
// Enforcement is opt-in per declaration: parameters without minimum/
// maximum, non-numeric values and absent parameters keep their legacy
// handling (defaults and typed accessors inside run()).
void validateDeclaredRanges(const Json::Value& schemaRoot, const Json::Value& params) {
    if (!params.isObject())
        return;
    const Json::Value& properties = schemaRoot["properties"];
    if (!properties.isObject())
        return;
    for (const std::string& name : params.getMemberNames()) {
        if (!properties.isMember(name))
            continue;
        const Json::Value& declaration = properties[name];
        if (!declaration.isMember("minimum") || !declaration.isMember("maximum"))
            continue;
        const Json::Value& value = params[name];
        if (!value.isNumeric())
            continue;
        const double numeric = value.asDouble();
        const double minimum = declaration["minimum"].asDouble();
        const double maximum = declaration["maximum"].asDouble();
        if (numeric < minimum || numeric > maximum) {
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "parameter '" + name + "' value " +
                                      std::to_string(numeric) +
                                      " is outside the declared [" +
                                      std::to_string(minimum) + "," +
                                      std::to_string(maximum) + "] range");
        }
    }
}

} // namespace


std::string RSOperator::displayName() const {
    return name();
}

std::string RSOperator::group() const {
    return "general";
}

std::string RSOperator::description() const {
    return {};
}

Json::Value RSOperator::schema() const {
    Json::Value properties(Json::objectValue);
    Json::Value outputs(Json::objectValue);
    return schema::makeRootSchema(displayName(), description(), properties, outputs);
}

Json::Value RSOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["name"] = name();
    meta["displayName"] = displayName();
    meta["group"] = group();
    meta["description"] = description();
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["purpose"] = "";
    meta["useCases"] = Json::Value(Json::arrayValue);
    meta["prerequisites"] = Json::Value(Json::arrayValue);
    meta["limitations"] = Json::Value(Json::arrayValue);
    meta["workflowHints"] = Json::Value(Json::arrayValue);
    return meta;
}

Json::Value RSOperator::executionEstimate() const {
    // Default: unknown/auto (no execution-resource declaration). Operators
    // that can quantify their large-raster behavior override this.
    return Json::Value(Json::objectValue);
}

Json::Value RSOperator::estimateExecution(const Json::Value& /*params*/) const {
    // Dynamic estimate seam: default to the static typical-input estimate.
    // Operators that can derive working set from parameters override this.
    return executionEstimate();
}

Json::Value RSOperator::execute(const Json::Value& params, RSOperatorContext& context) {
    auto& logger = RSOperationLogger::instance();
    const size_t handle = logger.beginRun(name(), params);

    const auto start = std::chrono::steady_clock::now();

    try {
        validateDeclaredRanges(schema(), params);
        Json::Value result = run(params, context);
        const auto end = std::chrono::steady_clock::now();
        const double durationMs =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

        logger.finishRun(handle, result, durationMs);
        return result;
    } catch (const RSOperatorError& e) {
        const auto end = std::chrono::steady_clock::now();
        const double durationMs =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        logger.failRun(handle, static_cast<int>(e.code()), e.message(), durationMs);
        throw;
    } catch (const std::exception& e) {
        const auto end = std::chrono::steady_clock::now();
        const double durationMs =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
        logger.failRun(handle, static_cast<int>(ErrorCode::Unknown), e.what(), durationMs);
        throw;
    }
}

} // namespace sicnu::operators
