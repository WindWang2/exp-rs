#include "python_algorithm_adapter.h"

namespace sicnu {

PythonAlgorithmAdapter::PythonAlgorithmAdapter(AlgorithmDescriptor desc, ExecuteCallback executor)
    : m_desc(std::move(desc))
    , m_executor(std::move(executor))
{
}

bool PythonAlgorithmAdapter::validateParameters(const QVariantMap& params, QString& error) const
{
    Q_UNUSED(params);
    Q_UNUSED(error);
    return true;
}

bool PythonAlgorithmAdapter::execute(const QVariantMap& params, std::function<void(double)> progressCallback, QString& error)
{
    if (m_executor) {
        return m_executor(params, progressCallback, error);
    }
    error = QStringLiteral("No execution handler provided for Python algorithm");
    return false;
}

} // namespace sicnu
