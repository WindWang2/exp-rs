// dialog_utils.cpp — Shared utilities for dialog UI
#include "dialog_utils.h"

#include <QComboBox>
#include <qgsproject.h>
#include <raster/qgsrasterlayer.h>

void populateRasterLayerCombo(QComboBox *combo, bool clearFirst)
{
    if (!combo) return;

    if (clearFirst)
        combo->clear();

    const auto layers = QgsProject::instance()->mapLayers();
    for (auto it = layers.constBegin(); it != layers.constEnd(); ++it) {
        auto *rl = qobject_cast<QgsRasterLayer *>(it.value());
        if (rl && rl->isValid()) {
            combo->addItem(rl->name(), QVariant::fromValue(rl));
        }
    }
}
