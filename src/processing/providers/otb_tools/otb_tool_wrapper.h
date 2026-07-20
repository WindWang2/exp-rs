// src/processing/providers/otb_tools/otb_tool_wrapper.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include <QProcess>

#include "algorithm_help_catalog.h"

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

    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), applicationName(), tags() );
    }

    // Common implementation
    QVariantMap processAlgorithm(const QVariantMap &parameters,
                                 QgsProcessingContext &context,
                                 QgsProcessingFeedback *feedback) override;

    /// Shell-style CLI preview for dialog (does not execute).
    QString commandLinePreview( const QVariantMap &parameters,
                                QgsProcessingContext &context );

protected:
    // Helper to run an external OTB application
    bool runOtbApplication(const QString &program, const QStringList &args, QgsProcessingFeedback *feedback);

    // Null-safe layer source extraction helpers
    static QString rasterLayerSource(const QVariant &var);
    static QString vectorLayerSource(const QVariant &var);
};
