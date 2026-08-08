// raster_layer_combo.h — shared raster layer picker (C5)
#pragma once

#include <QComboBox>

class QgsRasterLayer;

/**
 * Shared raster-layer picker used by product-aware dialogs (change
 * detection, post-classification comparison, ...): lists the project's valid
 * raster layers and resolves the current selection to a QgsRasterLayer —
 * one canonical raster-selection widget instead of per-dialog
 * QgsProject::mapLayers() loops (C5).
 */
class RasterLayerCombo : public QComboBox
{
    Q_OBJECT

public:
    explicit RasterLayerCombo( QWidget *parent = nullptr );

    /// Populate from the project's valid raster layers (clears first).
    void populate();

    /// Id of the currently selected layer (empty when none selected).
    QString currentLayerId() const;

    /// Currently selected raster layer, or nullptr when none / invalid.
    QgsRasterLayer *currentRasterLayer() const;

    /// Select the layer with @p id if present; no-op otherwise.
    void selectLayer( const QString &id );
};
