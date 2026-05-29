// src/processing/providers/qgis_algorithms/provider.h
#pragma once

#include <processing/qgsprocessingprovider.h>

class QgisAlgorithmsProvider : public QgsProcessingProvider
{
    Q_OBJECT

public:
    QgisAlgorithmsProvider();
    QString id() const override { return QStringLiteral( "qgis_algorithms" ); }
    QString name() const override { return QStringLiteral( "QGIS Basic Algorithms" ); }
    QIcon icon() const override;

protected:
    void loadAlgorithms() override;
};
