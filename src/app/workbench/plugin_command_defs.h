/***************************************************************************
 * plugin_command_defs.h — Workbench 9.0 M8
 *
 * Registers a rendered plugin's menu contributions as first-class
 * CommandRegistry commands (id `plugin.<pluginId>.<n>`). Pure function over
 * (registry, actions) so the command lifecycle is unit-testable without the
 * main window. Availability follows each rendered action's lifetime — when
 * the shell releases the plugin (unload/crash), its commands disable
 * automatically and `unregisterCommandsMatching` clears them.
 ***************************************************************************/
#pragma once

#include <QList>
#include <QString>

class QAction;

namespace sicnu::app
{

class CommandRegistry;

/// Registers one command per action; returns the number registered.
/// Titles are taken from the actions; handlers trigger them through a
/// QPointer guard (a deleted action disables the command, never crashes).
int registerPluginMenuCommands( CommandRegistry *registry,
                                const QList<QAction *> &actions,
                                const QString &pluginId,
                                const QString &categoryLabel );

} // namespace sicnu::app
