#pragma once

#include <QObject>
#include <processing/qgsprocessingprovider.h>

class SicnuNativeAlgorithms : public QgsProcessingProvider
{
    Q_OBJECT

public:
    explicit SicnuNativeAlgorithms( QObject *parent = nullptr );

    QString id() const override { return QStringLiteral( "sicnu_native" ); }
    QString name() const override { return QStringLiteral( "SICNU Native" ); }
    QIcon icon() const override;
    QString longName() const override { return QStringLiteral( "SICNU Native Algorithms" ); }

protected:
    void loadAlgorithms() override;
};
