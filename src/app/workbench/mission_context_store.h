/***************************************************************************
 * mission_context_store.h — D18 persistence for MissionContext
 *
 * Dual-write contract (D-M5):
 *   1. Sidecar `<projectStem>.mission.json` beside the project file (primary
 *      for headless / CLI / explicit restore).
 *   2. Optional project XML element `sicnuMissionContext` on the QGIS document
 *      root so .qgs/.qgz carries the mission without requiring the sidecar.
 *
 * Deliberately NOT embedded inside DataProjectSerializer's `sicnuDataManager`
 * block — that path has v3 governance downgrade guards; mixing mission JSON
 * there risks silent refusal or format coupling. Sidecar + sibling XML is the
 * documented dual-write.
 ***************************************************************************/
#pragma once

#include "app/workbench/mission_context.h"

class QDomDocument;

namespace sicnu::app
{

/// Derive the sidecar path for a project file (".qgz"/".qgs" → ".mission.json").
QString missionSidecarPathForProject( const QString &projectFilePath );

/// Atomic-ish write: temp file in same directory then rename.
bool saveMissionContextToSidecar( const QString &projectFilePath, const MissionContext &ctx,
                                  QString *error = nullptr );

bool loadMissionContextFromSidecar( const QString &projectFilePath, MissionContext &out,
                                    QString *error = nullptr );

/// Dual-write into QGIS project XML (sibling of sicnuDataManager). Fail-closed
/// only on missing document root; empty mission is a no-op success.
bool writeMissionContextToProjectXml( QDomDocument &document, const MissionContext &ctx,
                                      QString *error = nullptr );

/// Read mission from project XML. Returns false when element missing or invalid
/// (does not mutate @p out on failure unless partial parse succeeded — fail-closed).
bool readMissionContextFromProjectXml( const QDomDocument &document, MissionContext &out,
                                       QString *error = nullptr );

/// Project save path helper: write sidecar (when path non-empty) AND XML.
/// Sidecar failure is reported but XML is still attempted (and vice versa).
bool persistMissionContextWithProject( const QString &projectFilePath, QDomDocument &document,
                                       const MissionContext &ctx, QString *error = nullptr );

/// Project open helper: prefer sidecar when present; else XML. Missing both is
/// not an error (fresh project) — returns true with @p loaded=false.
bool restoreMissionContextWithProject( const QString &projectFilePath, const QDomDocument &document,
                                       MissionContext &out, bool *loaded = nullptr,
                                       QString *error = nullptr );

} // namespace sicnu::app
