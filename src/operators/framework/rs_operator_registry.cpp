/***************************************************************************
 * rs_operator_registry.cpp  —  RSOperatorRegistry implementation
 ***************************************************************************/
#include "rs_operator_registry.h"

namespace sicnu::operators {

RSOperatorRegistry& RSOperatorRegistry::instance() {
    static RSOperatorRegistry registry;
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
