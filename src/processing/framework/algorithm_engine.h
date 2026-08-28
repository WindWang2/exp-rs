#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QMutex>
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

    // registeredProviders() is read from TaskCenter submit paths (worker
    // threads) while registerProvider() may still be running during startup
    // — the mutex closes the formal data race even though registration is
    // expected to finish before the first submit (#634).
    QMap<QString, AlgorithmProviderAdapterPtr> m_providers;
    mutable QMutex m_providersMutex;
};

} // namespace sicnu
