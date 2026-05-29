// src/processing/providers/gdal_tools/provider.h
#pragma once

#include <processing/qgsprocessingprovider.h>

class GdalToolsProvider : public QgsProcessingProvider
{
    Q_OBJECT

public:
    GdalToolsProvider();
    QString id() const override { return "gdal_tools"; }
    QString name() const override { return "GDAL Tools"; }
    QIcon icon() const override;

protected:
    void loadAlgorithms() override;
    bool supportsNonFileBasedOutput() const override { return false; }
};
