// dialog_utils.h — Shared utilities for dialog UI
#pragma once

#include "dialog_help_catalog.h"

class QComboBox;

/**
 * Populate a combo box with all valid raster layers from the current project.
 * Each item stores the QgsRasterLayer* as QVariant data.
 *
 * @param combo        Target combo box
 * @param clearFirst   If true, clears existing items first (default: true)
 */
void populateRasterLayerCombo(QComboBox *combo, bool clearFirst = true);
