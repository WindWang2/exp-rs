#pragma once

#include <QString>
#include <memory>
#include <vector>

namespace sicnu {

class AlgorithmEngine;

enum class ProviderResourceProfile {
    InProcessThread,
    PythonWorkerProcess,
    ExternalCliSubprocess,
    QgsTaskThread
};

class AlgorithmProviderAdapter {
public:
    virtual ~AlgorithmProviderAdapter() = default;
    virtual QString providerId() const = 0;
    virtual QString providerName() const = 0;
    virtual ProviderResourceProfile resourceProfile() const = 0;
    virtual void initialize() = 0;
    virtual void discoverAlgorithms( AlgorithmEngine &engine ) = 0;
};

using AlgorithmProviderAdapterPtr = std::shared_ptr<AlgorithmProviderAdapter>;

} // namespace sicnu
