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
  /**
   * Save preflight (#1747): the exact downgrade condition write() refuses,
   * answerable BEFORE QgsProject::write() runs. QgsProject::writeProject
   * cannot abort a save, so a refusal inside write() still overwrote the
   * file with no SICNU extension block — callers must check this first and
   * refuse the save so the on-disk governed state survives untouched.
   */
  static data::Result<void> canWrite(const ProjectContext &context);

  data::Result<void> write(QDomDocument &document,
                           const ProjectContext &context) const;
  data::Result<void> read(const QDomDocument &document, QgsProject &project,
                          ProjectContext &context) const;
};

} // namespace sicnu::app
