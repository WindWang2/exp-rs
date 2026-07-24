#pragma once

#include "data/data_result.h"

class QDomDocument;
class QgsProject;

namespace sicnu::app {

class ProjectContext;

/**
 * Persists SICNU Data identity alongside QGIS' authoritative presentation XML.
 *
 * The extension stores only project-persistent source descriptions. Existing
 * QGIS map-layer XML remains responsible for renderers, order, groups, and
 * visibility.
 */
class DataProjectSerializer {
public:
  data::Result<void> write(QDomDocument &document,
                           const ProjectContext &context) const;
  data::Result<void> read(const QDomDocument &document, QgsProject &project,
                          ProjectContext &context) const;
};

} // namespace sicnu::app
