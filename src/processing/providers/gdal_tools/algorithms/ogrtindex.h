// src/processing/providers/gdal_tools/algorithms/ogrtindex.h
#pragma once

#include "../gdal_tool_wrapper.h"

class OgrTindexAlgorithm : public GdalToolWrapper
{
public:
    OgrTindexAlgorithm() = default;

    QString name() const override { return "ogrtindex"; }
    QString displayName() const override { return "OGR Tile Index (Vector Tile Index)"; }
    QString group() const override { return "Vector Conversion"; }
    QString groupId() const override { return "vectorconversion"; }
    QStringList tags() const override { return { QObject::tr( "ogrtindex" ), QObject::tr( "tile index" ), QObject::tr( "vector" ), QObject::tr( "ogr" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "ogrtindex"; }

    QgsProcessingAlgorithm *createInstance() const override { return new OgrTindexAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
