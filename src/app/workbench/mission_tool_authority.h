/***************************************************************************
 * mission_tool_authority.h — app-side authority for the mission tools
 *
 * Track: glm53-mission-runtime-13 (Mission Runtime 13.0)
 *
 * Implements the agent surface's MissionAuthority abstraction over the
 * single-authority runtime store. Lives in the app layer (never in
 * sicnu_agent): it is the seam that keeps the shared agent library free of
 * the QGIS-dependent persistence chain while the desktop shell and the
 * headless `--mcp` branch of the same binary share ONE implementation.
 *
 * Qt Core + Qt Xml only (no QGIS, no Widgets).
 ***************************************************************************/
#pragma once

#include "agent/spatial_tools/mission_tools.h"

#include <QDomDocument>
#include <QString>

namespace sicnu::app
{

/// Single-authority runtime store behind the `mission:*` tools.
class MissionStoreAuthority final : public sicnu::agent::spatial_tools::MissionAuthority
{
public:
    bool load( const QString &projectPath, const QDomDocument &document,
               sicnu::app::MissionRuntimeState &state, QString &errorCode,
               QString &errorMessage ) override;
    bool commit( const QString &projectPath, QDomDocument &document,
                 sicnu::app::MissionRuntimeState &state, QString &errorCode,
                 QString &errorMessage ) override;
};

} // namespace sicnu::app
