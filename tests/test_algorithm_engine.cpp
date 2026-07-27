#include <catch2/catch_test_macros.hpp>

#include "processing/framework/algorithm_engine.h"

class DummyCustomTaskAdapter : public sicnu::TaskAlgorithmAdapter {
public:
    sicnu::AlgorithmDescriptor descriptor() const override {
        sicnu::AlgorithmDescriptor desc;
        desc.id = QStringLiteral("dummy_custom_algo");
        desc.name = QStringLiteral("Dummy Custom Algorithm");
        desc.group = QStringLiteral("Test Group");
        desc.description = QStringLiteral("A dummy algorithm for testing AlgorithmEngine");
        return desc;
    }

    bool validateParameters(const QVariantMap& params, QString& error) const override {
        if (!params.contains(QStringLiteral("input"))) {
            error = QStringLiteral("Missing required parameter: input");
            return false;
        }
        return true;
    }

    bool execute(const QVariantMap& params, std::function<void(double)> progressCallback, QString& error) override {
        Q_UNUSED(params);
        if (progressCallback) {
            progressCallback(0.5);
            progressCallback(1.0);
        }
        return true;
    }
};

TEST_CASE("AlgorithmEngine - Register and Find Custom Adapter", "[processing][algorithm_engine]") {
    auto& engine = sicnu::AlgorithmEngine::instance();
    engine.clear();

    REQUIRE(engine.registeredAlgorithms().isEmpty());

    auto dummyAdapter = std::make_shared<DummyCustomTaskAdapter>();
    engine.registerAlgorithm(dummyAdapter);

    auto list = engine.registeredAlgorithms();
    REQUIRE(list.size() == 1);
    REQUIRE(list.first().id == QStringLiteral("dummy_custom_algo"));
    REQUIRE(list.first().name == QStringLiteral("Dummy Custom Algorithm"));

    auto found = engine.findAlgorithm(QStringLiteral("dummy_custom_algo"));
    REQUIRE(found != nullptr);
    REQUIRE(found->descriptor().id == QStringLiteral("dummy_custom_algo"));
}

TEST_CASE("AlgorithmEngine - Parameter Validation", "[processing][algorithm_engine]") {
    auto& engine = sicnu::AlgorithmEngine::instance();
    engine.clear();

    auto dummyAdapter = std::make_shared<DummyCustomTaskAdapter>();
    engine.registerAlgorithm(dummyAdapter);

    QString error;
    QVariantMap invalidParams;
    bool isValid = engine.validateParameters(QStringLiteral("dummy_custom_algo"), invalidParams, error);
    REQUIRE_FALSE(isValid);
    REQUIRE(error.contains(QStringLiteral("Missing required parameter")));

    QVariantMap validParams;
    validParams.insert(QStringLiteral("input"), QStringLiteral("/path/to/raster.tif"));
    isValid = engine.validateParameters(QStringLiteral("dummy_custom_algo"), validParams, error);
    REQUIRE(isValid);

    bool unregisteredValid = engine.validateParameters(QStringLiteral("unknown_algo"), validParams, error);
    REQUIRE_FALSE(unregisteredValid);
    REQUIRE(error.contains(QStringLiteral("Algorithm not registered")));
}
