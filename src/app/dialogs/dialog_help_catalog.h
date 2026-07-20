// dialog_help_catalog.h — Shared help text for app dialogs and major windows.
#pragma once

#include <QString>

class QAction;
class QDialog;
class QWidget;

/**
 * Help utilities: tooltips / WhatsThis / status tips, plus HTML help pages
 * for dialogs keyed by tool id (see RasterProcessingDialogBase::toolName()).
 */
namespace SicnuDialogHelp
{

/// Apply ToolTip + StatusTip + WhatsThis to a widget.
void tip( QWidget *w, const QString &text );

/// Apply ToolTip + StatusTip + WhatsThis to an action.
void tip( QAction *a, const QString &text );

/**
 * Full HTML help body for a dialog tool id.
 * Falls back to a generic page if id is unknown.
 */
QString htmlForTool( const QString &toolId, const QString &titleFallback = QString() );

/**
 * One-line summary for window/status (short).
 */
QString shortForTool( const QString &toolId, const QString &titleFallback = QString() );

/**
 * Configure a QDialog: window tooltip, WhatsThis, and optional title-bar help.
 * Call after setWindowTitle().
 */
void applyDialogChrome( QDialog *dlg, const QString &toolId );

/**
 * Show modal help box (标题 + HTML).
 */
void showHelpBox( QWidget *parent, const QString &title, const QString &html );

/**
 * Show help for tool id.
 */
void showToolHelp( QWidget *parent, const QString &toolId, const QString &title );

} // namespace SicnuDialogHelp
