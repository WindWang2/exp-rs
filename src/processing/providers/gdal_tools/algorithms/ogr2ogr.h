// src/processing/providers/gdal_tools/algorithms/ogr2ogr.h
#pragma once

#include "../gdal_tool_wrapper.h"

class Ogr2OgrAlgorithm : public GdalToolWrapper
{
public:
    Ogr2OgrAlgorithm() = default;

    QString name() const override { return "ogr2ogr"; }
    QString displayName() const override { return "OGR2OGR (Vector Format Conversion)"; }
    QString group() const override { return "Vector Conversion"; }
    QString groupId() const override { return "vectorconversion"; }
    QStringList tags() const override { return { QObject::tr( "ogr2ogr" ), QObject::tr( "convert" ), QObject::tr( "vector" ), QObject::tr( "format" ), QObject::tr( "ogr" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "ogr2ogr"; }

    QgsProcessingAlgorithm *createInstance() const override { return new Ogr2OgrAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
