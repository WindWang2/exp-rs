#pragma once
#include <QString>
// Initializes QgsApplication with bundled data paths. Idempotent.
// dataRoot points at the dir holding srs.db/qgis.db/resources (spec §9).
void antigravity_init(const QString &dataRoot);
