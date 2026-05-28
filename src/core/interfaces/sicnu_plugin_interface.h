// src/core/interfaces/sicnu_plugin_interface.h
#pragma once

#include <QString>
#include <QWidget>
#include <QIcon>
#include <QAction>
#include <QList>

class QgsMapCanvas;
class QgsLayerTreeView;

/**
 * @brief Interface for SICNU GEO RS plugins
 *
 * All plugins must implement this interface to be loaded by the PluginManager.
 * Plugins can contribute UI widgets, menu actions, and toolbar actions.
 */
class SicnuPluginInterface
{
public:
    virtual ~SicnuPluginInterface() = default;

    // Plugin metadata
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const = 0;
    virtual QIcon icon() const = 0;

    // Lifecycle
    virtual bool initialize(QgsMapCanvas *canvas, QgsLayerTreeView *layerTree) = 0;
    virtual void unload() = 0;

    // UI contributions (optional)
    virtual QWidget *createWidget(QWidget *parent = nullptr) { Q_UNUSED(parent); return nullptr; }
    virtual QList<QAction*> menuActions() { return {}; }
    virtual QList<QAction*> toolbarActions() { return {}; }
};

#define SicnuPluginInterface_iid "org.sicnu.SicnuPluginInterface/1.0"
Q_DECLARE_INTERFACE(SicnuPluginInterface, SicnuPluginInterface_iid)
