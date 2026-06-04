// src/processing/providers/gdal_tools/algorithms/gdal_translate.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalTranslateAlgorithm : public GdalToolWrapper
{
public:
    GdalTranslateAlgorithm() = default;

    QString name() const override { return "gdal_translate"; }
    QString displayName() const override { return "GDAL Translate (Format Conversion)"; }
    QString group() const override { return "Raster Conversion"; }
    QString groupId() const override { return "rasterconversion"; }
    QStringList tags() const override { return { QObject::tr( "gdal_translate" ), QObject::tr( "translate" ), QObject::tr( "format" ), QObject::tr( "conversion" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdal_translate"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalTranslateAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
