// generic_cli_algorithm.h — Generic CLI tool wrapper for user-defined tools
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include <QJsonObject>

/**
 * A generic processing algorithm that wraps any CLI tool.
 * Configuration is loaded from a JSON file that defines:
 * - tool name and command
 * - parameter definitions
 * - argument mapping
 * - output handling
 *
 * Example JSON config:
 * {
 *   "id": "my_tool",
 *   "name": "My Custom Tool",
 *   "command": "my_tool_cli",
 *   "parameters": [
 *     {"name": "INPUT", "type": "raster", "description": "Input raster"},
 *     {"name": "THRESHOLD", "type": "number", "default": 0.5},
 *     {"name": "OUTPUT", "type": "output_raster"}
 *   ],
 *   "args": ["--input", "{INPUT}", "--threshold", "{THRESHOLD}", "--output", "{OUTPUT}"]
 * }
 */
class GenericCliAlgorithm : public QgsProcessingAlgorithm
{
public:
    GenericCliAlgorithm(const QJsonObject &config, const QString &providerId);

    QString name() const override;
    QString displayName() const override;
    QString group() const override;
    QString groupId() const override;
    QStringList tags() const override;
    QgsProcessingAlgorithm *createInstance() const override;

protected:
    void initAlgorithm(const QVariantMap &configuration) override;
    QVariantMap processAlgorithm(const QVariantMap &parameters,
                                  QgsProcessingContext &context,
                                  QgsProcessingFeedback *feedback) override;

private:
    QJsonObject m_config;
    QString m_providerId;
    QStringList buildArgs(const QVariantMap &parameters, QgsProcessingFeedback *feedback) const;
};
