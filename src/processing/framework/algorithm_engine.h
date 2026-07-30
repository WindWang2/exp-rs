#pragma once

#include <QString>
#include <QVariantMap>
#include <QList>
#include <QMap>
#include <memory>
#include <functional>

#include "qgsprocessingalgorithm.h"
#include "qgsprocessingcontext.h"
#include "qgsprocessingfeedback.h"

#include "algorithm_provider_adapter.h"

namespace sicnu {

struct AlgorithmDescriptor {
    QString id;
    QString name;
    QString group;
    QString description;
    QVariantMap parameterSchema;
    ProviderResourceProfile resourceProfile = ProviderResourceProfile::InProcessThread;
};

class TaskAlgorithmAdapter {
public:
    virtual ~TaskAlgorithmAdapter() = default;
    virtual AlgorithmDescriptor descriptor() const = 0;
    virtual bool validateParameters(const QVariantMap& params, QString& error) const = 0;
    virtual bool execute(const QVariantMap& params, std::function<void(double)> progressCallback, QString& error) = 0;
};

class QgsProcessingAlgorithmAdapter : public TaskAlgorithmAdapter {
public:
    explicit QgsProcessingAlgorithmAdapter(std::unique_ptr<QgsProcessingAlgorithm> algo,
                                           ProviderResourceProfile profile = ProviderResourceProfile::InProcessThread);
    ~QgsProcessingAlgorithmAdapter() override = default;

    AlgorithmDescriptor descriptor() const override;
    bool validateParameters(const QVariantMap& params, QString& error) const override;
    bool execute(const QVariantMap& params, std::function<void(double)> progressCallback, QString& error) override;

    QgsProcessingAlgorithm* algorithm() const { return m_algo.get(); }

private:
    std::unique_ptr<QgsProcessingAlgorithm> m_algo;
    ProviderResourceProfile m_resourceProfile = ProviderResourceProfile::InProcessThread;
};

class AlgorithmEngine {
public:
    static AlgorithmEngine& instance();

    void registerAlgorithm(std::shared_ptr<TaskAlgorithmAdapter> adapter);
    void registerProcessingAlgorithm(std::unique_ptr<QgsProcessingAlgorithm> algo);

    void registerProvider( AlgorithmProviderAdapterPtr provider );
    QList<AlgorithmProviderAdapterPtr> registeredProviders() const;

    QList<AlgorithmDescriptor> registeredAlgorithms() const;
    std::shared_ptr<TaskAlgorithmAdapter> findAlgorithm(const QString& id) const;

    void initialize();
    void populateFromProcessingRegistry();

    bool validateParameters(const QString& id, const QVariantMap& params, QString& error) const;
    bool executeAlgorithm(const QString& id, const QVariantMap& params, std::function<void(double)> progressCallback, QString& error);
    void clear();

private:
    AlgorithmEngine() = default;
    ~AlgorithmEngine() = default;
    AlgorithmEngine(const AlgorithmEngine&) = delete;
    AlgorithmEngine& operator=(const AlgorithmEngine&) = delete;

    QMap<QString, std::shared_ptr<TaskAlgorithmAdapter>> m_adapters;
    QMap<QString, AlgorithmProviderAdapterPtr> m_providers;
};

} // namespace sicnu
