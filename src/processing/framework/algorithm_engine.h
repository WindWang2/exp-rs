#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <memory>

#include "algorithm_provider_adapter.h"

namespace sicnu {

class AlgorithmEngine {
public:
    static AlgorithmEngine& instance();

    void registerProvider( AlgorithmProviderAdapterPtr provider );
    QList<AlgorithmProviderAdapterPtr> registeredProviders() const;

    void initialize();

private:
    AlgorithmEngine() = default;
    ~AlgorithmEngine() = default;
    AlgorithmEngine(const AlgorithmEngine&) = delete;
    AlgorithmEngine& operator=(const AlgorithmEngine&) = delete;

    QMap<QString, AlgorithmProviderAdapterPtr> m_providers;
};

} // namespace sicnu
