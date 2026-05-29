// src/processing/providers/otb_tools/provider.h
#pragma once

#include <processing/qgsprocessingprovider.h>

class OtbToolsProvider : public QgsProcessingProvider
{
    Q_OBJECT

public:
    OtbToolsProvider();
    QString id() const override { return "otb_tools"; }
    QString name() const override { return "OTB Tools"; }
    QIcon icon() const override;
    QgsProcessingProvider *clone() const override;

protected:
    void loadAlgorithms() override;
};
