/***************************************************************************
 * rs_operator.cpp  —  Default implementations for RSOperator
 ***************************************************************************/
#include "rs_operator.h"
#include "rs_operation_logger.h"

namespace sicnu::operators {

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

Json::Value RSOperator::execute(const Json::Value& params, RSOperatorContext& context) {
    auto& logger = RSOperationLogger::instance();
    const size_t handle = logger.beginRun(name(), params);

    const auto start = std::chrono::steady_clock::now();

    try {
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
