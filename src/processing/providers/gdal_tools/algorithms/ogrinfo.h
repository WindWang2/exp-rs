// src/processing/providers/gdal_tools/algorithms/ogrinfo.h
#pragma once

#include "../gdal_tool_wrapper.h"

class OgrInfoAlgorithm : public GdalToolWrapper
{
public:
    OgrInfoAlgorithm() = default;

    QString name() const override { return "ogrinfo"; }
    QString displayName() const override { return "OGR Info (Vector Information)"; }
    QString group() const override { return "Vector Information"; }
    QString groupId() const override { return "vectorinformation"; }
    QStringList tags() const override { return { QObject::tr( "ogrinfo" ), QObject::tr( "information" ), QObject::tr( "vector" ), QObject::tr( "metadata" ), QObject::tr( "ogr" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "ogrinfo"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OgrInfoAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
