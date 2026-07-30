#pragma once

#include "algorithm_provider_adapter.h"

#include <functional>
#include <memory>

class QgsProcessingProvider;

namespace sicnu {

/**
 * Adapts a QGIS QgsProcessingProvider factory into AlgorithmEngine's
 * AlgorithmProviderAdapter seam (ADR Algorithm Provider Adapter / resource profiles).
 *
 * Ownership of the created QgsProcessingProvider is transferred to
 * QgsApplication::processingRegistry() on initialize().
 */
class QgsProcessingProviderAdapter : public AlgorithmProviderAdapter
{
public:
    using ProviderFactory = std::function<QgsProcessingProvider *()>;

    QgsProcessingProviderAdapter( QString providerId,
                                  QString providerName,
                                  ProviderResourceProfile profile,
                                  ProviderFactory factory );

    QString providerId() const override;
    QString providerName() const override;
    ProviderResourceProfile resourceProfile() const override;
    void initialize() override;
    void discoverAlgorithms( AlgorithmEngine &engine ) override;

private:
    QString m_providerId;
    QString m_providerName;
    ProviderResourceProfile m_profile = ProviderResourceProfile::InProcessThread;
    ProviderFactory m_factory;
    bool m_initialized = false;
};

} // namespace sicnu
