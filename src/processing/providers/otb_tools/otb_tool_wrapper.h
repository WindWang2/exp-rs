// src/processing/providers/otb_tools/otb_tool_wrapper.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include <QProcess>

class OtbToolWrapper : public QgsProcessingAlgorithm
{
public:
    OtbToolWrapper() = default;

    // Subclasses implement these
    virtual QString applicationName() const = 0;
    virtual QString displayName() const = 0;
    virtual QString group() const { return "OTB"; }
    virtual QString groupId() const override { return "otb"; }
    virtual QStringList buildArgs(const QVariantMap &parameters,
                                  QgsProcessingContext &context,
                                  QgsProcessingFeedback *feedback) = 0;

    // Common implementation
    QVariantMap processAlgorithm(const QVariantMap &parameters,
                                 QgsProcessingContext &context,
                                 QgsProcessingFeedback *feedback) override;

protected:
    // Helper to run an external OTB application
    bool runOtbApplication(const QStringList &args, QgsProcessingFeedback *feedback);
};
