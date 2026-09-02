#include "rs_operator_registry.h"

namespace sicnu::operators {

namespace rs { void initBuiltinRsOperators(); }
namespace gdal { void initBuiltinGdalOperators(); }
#ifdef SICNU_HAS_OPENCV
namespace opencv { void initBuiltinOpenCvOperators(); }
#endif
namespace otb { void initBuiltinOtbOperators(); }

} // namespace sicnu::operators

namespace sicnu::operators::rs {
/// Published by instance() while its call_once chain runs so family init
/// functions can register operators without re-entering instance().
/// Defined in rs_operators_init.cpp.
extern RSOperatorRegistry *sRegistryUnderConstruction;
}

namespace sicnu::operators {

RSOperatorRegistry& RSOperatorRegistry::instance() {
    static std::once_flag initFlag;
    static RSOperatorRegistry registry;
    std::call_once(initFlag, []() {
        // registry's address is constant for the process lifetime; assign it
        // directly rather than capturing (static locals cannot be captured).
        // Every family init function registers through this pointer (never
        // via instance()) — re-entering the same call_once from inside the
        // chain leaves the init guard unreleased and the next instance()
        // re-runs registration against a cleared factory map (#707).
        sicnu::operators::rs::sRegistryUnderConstruction = &registry;
        rs::initBuiltinRsOperators();
        gdal::initBuiltinGdalOperators();
#ifdef SICNU_HAS_OPENCV
        opencv::initBuiltinOpenCvOperators();
#endif
        otb::initBuiltinOtbOperators();
        sicnu::operators::rs::sRegistryUnderConstruction = nullptr;
    });
    return registry;
}

void RSOperatorRegistry::registerOperator(const std::string& name, FactoryFn factory) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_factories[name] = std::move(factory);
}

bool RSOperatorRegistry::hasOperator(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_factories.find(name) != m_factories.end();
}

std::unique_ptr<RSOperator> RSOperatorRegistry::create(const std::string& name) const {
    FactoryFn factory;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_factories.find(name);
        if (it == m_factories.end()) {
            return nullptr;
        }
        factory = it->second;
    }
    return factory();
}

std::vector<std::string> RSOperatorRegistry::operatorNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> names;
    names.reserve(m_factories.size());
    for (const auto& pair : m_factories) {
        names.push_back(pair.first);
    }
    return names;
}

Json::Value RSOperatorRegistry::listSchemas() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Json::Value array(Json::arrayValue);
    for (const auto& pair : m_factories) {
        auto op = pair.second();
        if (op) {
            Json::Value entry = op->schema();
            entry["name"] = pair.first;
            array.append(entry);
        }
    }
    return array;
}

} // namespace sicnu::operators
