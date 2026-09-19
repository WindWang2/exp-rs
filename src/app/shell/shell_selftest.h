/***************************************************************************
 * shell_selftest.h — headless startup/shutdown contract checks
 *
 * Track 02 (issue #1037 F-1031-P0-registry): the previous "test" for the
 * command-registry ordering was a source-text grep — it stayed green when the
 * function body was gutted. This seam runs against the REAL QgisDesktopWindow
 * (production binary, SICNU_SHELL_SELFTEST=1) and fails non-zero when any
 * lifecycle/state contract breaks.
 ***************************************************************************/
#pragma once

#include <QString>

class QgisDesktopWindow;

namespace sicnu::app
{

/**
 * Runs the headless shell lifecycle checks against an already constructed,
 * shown window:
 *  - CommandRegistry is live and populated immediately after construction;
 *  - setupMenu projected real registry actions (Project/View menus) and the
 *    canonical shortcut was installed (#1037 F-1031-P0-registry);
 *  - window-hosted actions are re-hosted on the window;
 *  - a focused editor keeps printable letters while the map path keeps the
 *    binding (#1037 F-1031-P1-letterkey);
 *  - registry map-tool commands switch tools without a crash;
 *  - a fresh georeferencer window is born clean (#1052) and closes cleanly.
 *
 * @return true when every check passed; @p failure receives the first failed
 * contract description (empty on success).
 */
bool runShellSelfTest( QgisDesktopWindow *window, QString *failure );

} // namespace sicnu::app
