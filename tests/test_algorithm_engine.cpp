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

TEST_CASE("AlgorithmEngine - AlgorithmProviderAdapter registration seam", "[processing][algorithm_engine][provider]") {
    auto& engine = sicnu::AlgorithmEngine::instance();
    engine.clear();

    class StubProviderAdapter : public sicnu::AlgorithmProviderAdapter {
    public:
        QString providerId() const override { return QStringLiteral("stub_provider"); }
        QString providerName() const override { return QStringLiteral("Stub Provider"); }
        sicnu::ProviderResourceProfile resourceProfile() const override {
            return sicnu::ProviderResourceProfile::InProcessThread;
        }
        void initialize() override { m_initialized = true; }
        void discoverAlgorithms(sicnu::AlgorithmEngine &eng) override {
            eng.registerAlgorithm(std::make_shared<DummyCustomTaskAdapter>());
        }
        bool m_initialized = false;
    };

    auto provider = std::make_shared<StubProviderAdapter>();
    engine.registerProvider(provider);

    REQUIRE(provider->m_initialized);
    REQUIRE(engine.registeredProviders().size() == 1);
    REQUIRE(engine.registeredProviders().first()->providerId() == QStringLiteral("stub_provider"));
    REQUIRE(engine.findAlgorithm(QStringLiteral("dummy_custom_algo")) != nullptr);
    REQUIRE(engine.registeredAlgorithms().first().id == QStringLiteral("dummy_custom_algo"));
}
