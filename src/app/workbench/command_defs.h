/***************************************************************************
 * command_defs.h — shell command definitions (Workbench 5.0, Milestone C)
 *
 * Wires the existing QgisDesktopWindow slots into CommandDefinition entries:
 * one handler, one availability contract and one canonical shortcut per
 * capability. Surfaces (ribbon / hidden menu host / context menus / palette)
 * project these through CommandRegistry::action().
 *
 * Batch 1 covers the primary capability set; follow-up milestones migrate
 * the remaining dialogs and digitizing tools and make the menu host consume
 * these definitions directly.
 ***************************************************************************/
#pragma once

#include <QtGlobal>

class QgisDesktopWindow;

namespace sicnu::app
{
class CommandRegistry;
}

/// Registers the shell's built-in commands on @a registry. @a window is the
/// handler target (raw pointer, must outlive the registry).
void registerShellCommands( sicnu::app::CommandRegistry *registry, QgisDesktopWindow *window );
