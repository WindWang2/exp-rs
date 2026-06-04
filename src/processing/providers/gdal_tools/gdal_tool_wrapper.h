// src/processing/providers/gdal_tools/gdal_tool_wrapper.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include <QProcess>

class GdalToolWrapper : public QgsProcessingAlgorithm
{
public:
    GdalToolWrapper() = default;

    // Subclasses implement these
    virtual QString toolName() const = 0;
    virtual QString displayName() const = 0;
    virtual QString group() const { return "GDAL"; }
    virtual QString groupId() const override { return "gdal"; }
    virtual QStringList buildArgs(const QVariantMap &parameters,
                                  QgsProcessingContext &context,
                                  QgsProcessingFeedback *feedback) = 0;

    // Common implementation
    QVariantMap processAlgorithm(const QVariantMap &parameters,
                                 QgsProcessingContext &context,
                                 QgsProcessingFeedback *feedback) override;

    Qgis::ProcessingAlgorithmFlags flags() const override { return Qgis::ProcessingAlgorithmFlag::SupportsBatch; }

protected:
    // Helper to run an external tool
    bool runExternalTool(const QString &program, const QStringList &args,
                         QgsProcessingFeedback *feedback);

    // Common parameter helpers
    void addInputRasterLayerParameter(const QString &name = "INPUT",
                                      const QString &description = "Input raster layer");
    void addOutputRasterLayerParameter(const QString &name = "OUTPUT",
                                       const QString &description = "Output raster layer");
    void addInputVectorLayerParameter(const QString &name = "INPUT",
                                      const QString &description = "Input vector layer");
    void addOutputVectorLayerParameter(const QString &name = "OUTPUT",
                                       const QString &description = "Output vector layer");
    void addExtentParameter(const QString &name = "EXTENT");
    void addCrsParameter(const QString &name = "TARGET_CRS",
                         const QString &description = "Target CRS");
};
