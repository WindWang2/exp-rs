#pragma once

#include "algorithm_provider_adapter.h"
#include "python_algorithm_adapter.h"
#include <QMap>
#include <memory>

namespace sicnu {

/**
 * AlgorithmProviderAdapter implementation for Python processing algorithms executing
 * in out-of-process Python worker processes (ADR 0014 out-of-process isolation).
 */
class PythonProcessingProviderAdapter : public AlgorithmProviderAdapter
{
public:
    explicit PythonProcessingProviderAdapter( QString providerId = QStringLiteral( "python_plugins" ),
                                               QString providerName = QStringLiteral( "Python Plugins" ) );

    ~PythonProcessingProviderAdapter() override = default;

    QString providerId() const override;
    QString providerName() const override;
    ProviderResourceProfile resourceProfile() const override;
    void initialize() override;
    void discoverAlgorithms( AlgorithmEngine &engine ) override;

    /**
     * Register or update a Python algorithm in this provider.
     * The algorithm descriptor will have its resourceProfile forced to PythonWorkerProcess.
     */
    void addAlgorithm( processing::AtomicAlgorithmAdapterPtr algoAdapter );
    void removeAlgorithm( const QString &algoId );

private:
    QString m_providerId;
    QString m_providerName;
    bool m_initialized = false;
    AlgorithmEngine *m_boundEngine = nullptr;
    QMap<QString, processing::AtomicAlgorithmAdapterPtr> m_algorithms;
};

} // namespace sicnu
