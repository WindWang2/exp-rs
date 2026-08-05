#include <catch2/catch_test_macros.hpp>

#include "processing/framework/algorithm_engine.h"
#include "processing/framework/python_processing_provider_adapter.h"
#include "processing/framework/atomic_algorithm_registry.h"

TEST_CASE("AlgorithmEngine - AlgorithmProviderAdapter registration seam", "[processing][algorithm_engine][provider]") {
    auto& engine = sicnu::AlgorithmEngine::instance();

    class StubProviderAdapter : public sicnu::AlgorithmProviderAdapter {
    public:
        QString providerId() const override { return QStringLiteral("stub_provider"); }
        QString providerName() const override { return QStringLiteral("Stub Provider"); }
        sicnu::ProviderResourceProfile resourceProfile() const override {
            return sicnu::ProviderResourceProfile::InProcessThread;
        }
        void initialize() override { m_initialized = true; }
        void discoverAlgorithms(sicnu::AlgorithmEngine &) override {}
        bool m_initialized = false;
    };

    auto provider = std::make_shared<StubProviderAdapter>();
    engine.registerProvider(provider);

    REQUIRE(provider->m_initialized);
    bool foundStub = false;
    for (const auto& p : engine.registeredProviders()) {
        if (p && p->providerId() == QStringLiteral("stub_provider")) {
            foundStub = true;
            break;
        }
    }
    REQUIRE(foundStub);
}

TEST_CASE("AlgorithmEngine - PythonProcessingProviderAdapter resource profile and discovery", "[processing][algorithm_engine][python_provider]") {
    auto& engine = sicnu::AlgorithmEngine::instance();
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

    sicnu::processing::AlgorithmDescriptor desc;
    desc.id = "py:test_algorithm_108";
    desc.displayName = "Test Python Algo 108";
    desc.group = "Python Test";
    desc.description = "Test algorithm for issue 108";

    auto pyAlgo = std::make_shared<sicnu::processing::PythonAlgorithmAdapter>(desc, [](const Json::Value&, sicnu::processing::ProgressCallback) {
        return Json::Value(Json::objectValue);
    });

    adapterPtr->addAlgorithm(pyAlgo);

    auto found = sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter("py:test_algorithm_108");
    REQUIRE(found != nullptr);
    CHECK(found->descriptor().id == "py:test_algorithm_108");

    // Verify discoverAlgorithms re-populates stored algorithms into AtomicAlgorithmRegistry
    sicnu::processing::AtomicAlgorithmRegistry::instance().unregisterAdapter("py:test_algorithm_108");
    CHECK(sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter("py:test_algorithm_108") == nullptr);

    pythonProvider->discoverAlgorithms(engine);
    CHECK(sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter("py:test_algorithm_108") != nullptr);
}
