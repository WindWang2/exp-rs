/***************************************************************************
 * mission_tool_host_install.h — wire the mission tools into the process
 *
 * Track: glm53-mission-runtime-13 (Mission Runtime 13.0)
 *
 * ONE installer for both execution modes of the binary:
 *   - the desktop shell (setupWorkbenchInfrastructure),
 *   - the headless `--mcp` branch (src/app/main.cpp).
 *
 * It installs the single-authority store-backed authority plus the live
 * providers (project path from QgsProject, run status from the execution
 * authorities, layer liveness from the project's layer registry). Without
 * this call the mission:* tools fail closed rather than guess.
 *
 * Qt Widgets/QGIS (QgsProject) — compiled into the desktop shell only.
 ***************************************************************************/
#pragma once

namespace sicnu::app
{

/// Install the process-wide MissionToolHost. Idempotent.
void installMissionToolHost();

} // namespace sicnu::app
