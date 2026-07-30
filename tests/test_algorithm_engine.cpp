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

#include "processing/framework/python_processing_provider_adapter.h"

TEST_CASE("AlgorithmEngine - PythonProcessingProviderAdapter resource profile and discovery", "[processing][algorithm_engine][python_provider]") {
    auto& engine = sicnu::AlgorithmEngine::instance();
    engine.clear();

    engine.initialize();

    auto providers = engine.registeredProviders();
    sicnu::AlgorithmProviderAdapterPtr pythonProvider = nullptr;
    for (const auto& p : providers) {
        if (p && p->providerId() == QStringLiteral("python_plugins")) {
            pythonProvider = p;
            break;
        }
    }

    REQUIRE(pythonProvider != nullptr);
    CHECK(pythonProvider->providerName() == QStringLiteral("Python Plugins"));
    CHECK(pythonProvider->resourceProfile() == sicnu::ProviderResourceProfile::PythonWorkerProcess);

    auto adapterPtr = std::dynamic_pointer_cast<sicnu::PythonProcessingProviderAdapter>(pythonProvider);
    REQUIRE(adapterPtr != nullptr);

    sicnu::AlgorithmDescriptor desc;
    desc.id = QStringLiteral("py:test_algorithm_108");
    desc.name = QStringLiteral("Test Python Algo 108");
    desc.group = QStringLiteral("Python Test");
    desc.description = QStringLiteral("Test algorithm for issue 108");
    desc.resourceProfile = sicnu::ProviderResourceProfile::PythonWorkerProcess;

    auto pyAlgo = std::make_shared<sicnu::PythonAlgorithmAdapter>(desc, [](const QVariantMap&, std::function<void(double)>, QString&) {
        return true;
    });

    adapterPtr->addAlgorithm(pyAlgo);

    auto found = engine.findAlgorithm(QStringLiteral("py:test_algorithm_108"));
    REQUIRE(found != nullptr);
    CHECK(found->descriptor().id == QStringLiteral("py:test_algorithm_108"));
    CHECK(found->descriptor().resourceProfile == sicnu::ProviderResourceProfile::PythonWorkerProcess);

    // Verify discoverAlgorithms re-populates stored algorithms into a cleared engine
    engine.clear();
    CHECK(engine.findAlgorithm(QStringLiteral("py:test_algorithm_108")) == nullptr);

    pythonProvider->discoverAlgorithms(engine);
    CHECK(engine.findAlgorithm(QStringLiteral("py:test_algorithm_108")) != nullptr);
}


