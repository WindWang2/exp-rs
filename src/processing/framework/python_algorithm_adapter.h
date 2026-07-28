#pragma once

#include "algorithm_engine.h"
#include <functional>
#include <memory>
#include <QString>

namespace sicnu {

/**
 * Adapter exposing out-of-process Python algorithms to AlgorithmEngine via IPC callbacks.
 */
class PythonAlgorithmAdapter : public TaskAlgorithmAdapter {
public:
    using ExecuteCallback = std::function<bool(const QVariantMap& params,
                                              std::function<void(double)> progress,
                                              QString& error)>;

    PythonAlgorithmAdapter(AlgorithmDescriptor desc, ExecuteCallback executor);
    ~PythonAlgorithmAdapter() override = default;

    AlgorithmDescriptor descriptor() const override { return m_desc; }
    bool validateParameters(const QVariantMap& params, QString& error) const override;
    bool execute(const QVariantMap& params, std::function<void(double)> progressCallback, QString& error) override;

private:
    AlgorithmDescriptor m_desc;
    ExecuteCallback m_executor;
};

} // namespace sicnu
