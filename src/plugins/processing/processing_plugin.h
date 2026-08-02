#pragma once

#include <QObject>
#include "core/interfaces/sicnu_plugin_interface.h"

class QgsProcessingToolboxTreeView;

class ProcessingPlugin : public QObject, public SicnuPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SicnuPluginInterface_iid)
    Q_INTERFACES(SicnuPluginInterface)

public:
    explicit ProcessingPlugin(QObject *parent = nullptr);

    QString name() const override { return "Processing"; }
    QString description() const override { return "Processing toolbox and algorithms"; }
    QString version() const override { return "1.0.0"; }
    QIcon icon() const override;

    bool initialize(SicnuAppInterface *iface) override;
    void unload() override;

    QWidget *createWidget(QWidget *parent) override;
    QList<QAction*> menuActions() override;

private:
    QgsProcessingToolboxTreeView *m_toolboxView = nullptr;
};
