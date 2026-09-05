/***************************************************************************
 * rs_operator_registry.h  —  Factory registry for RSOperator instances
 ***************************************************************************/
#pragma once

#include "rs_operator.h"
#include "sicnu_operators_export.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::operators {

/**
 * Singleton registry that maps operator names to factory functions.
 *
 * The registry is thread-safe for registration and lookup. Operator
 * implementations are typically registered at program startup using the
 * REGISTER_RS_OPERATOR macro.
 */
class SICNU_OPERATORS_EXPORT RSOperatorRegistry {
public:
    using FactoryFn = std::function<std::unique_ptr<RSOperator>()>;

    static RSOperatorRegistry& instance();

    /**
     * Registers an operator factory under the given name.
     * Replaces any existing registration with the same name.
     */
    void registerOperator(const std::string& name, FactoryFn factory);

    /**
     * Removes a registration. Returns true when a factory was removed.
     * Used by the plugin runtime to revoke contributions on unload.
     */
    bool unregisterOperator(const std::string& name);

    /**
     * Returns true if an operator with the given name is registered.
     */
    bool hasOperator(const std::string& name) const;

    /**
     * Creates a new operator instance by name.
     * Returns nullptr if the operator is not registered.
     */
    std::unique_ptr<RSOperator> create(const std::string& name) const;

    /**
     * Lists all registered operator names.
     */
    std::vector<std::string> operatorNames() const;

    /**
     * Returns a JSON array containing schema() for every registered operator.
     */
    Json::Value listSchemas() const;

private:
    RSOperatorRegistry() = default;
    ~RSOperatorRegistry() = default;
    RSOperatorRegistry(const RSOperatorRegistry&) = delete;
    RSOperatorRegistry& operator=(const RSOperatorRegistry&) = delete;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, FactoryFn> m_factories;
};

/**
 * Convenience macro for registering an operator class in a .cpp file.
 *
 * Usage:
 *   REGISTER_RS_OPERATOR(MyOperator, "my:operator")
 */
#define REGISTER_RS_OPERATOR(ClassName, OperatorId)                       \
    namespace {                                                           \
    struct ClassName##Registrar {                                         \
        ClassName##Registrar() {                                          \
            sicnu::operators::RSOperatorRegistry::instance().registerOperator( \
                OperatorId,                                               \
                []() -> std::unique_ptr<sicnu::operators::RSOperator> {   \
                    return std::make_unique<ClassName>();                 \
                });                                                       \
        }                                                                 \
    } ClassName##RegistrarInstance;                                       \
    }

} // namespace sicnu::operators
