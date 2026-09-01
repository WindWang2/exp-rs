// dialog_utils.h — Shared utilities for dialog UI chrome
#pragma once

#include "dialog_help_catalog.h"

#include <QString>
#include <QGroupBox>

class QComboBox;
class QDialog;
class QDialogButtonBox;
class QFormLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

/**
 * Populate a combo box with all valid raster layers from the current project.
 * Each item stores the QgsRasterLayer* as QVariant data.
 */
void populateRasterLayerCombo( QComboBox *combo, bool clearFirst = true );

/**
 * Preflights two raster files against the shared pixel-grid compatibility
 * service (compareGrids). Returns an empty string when the pair can proceed
 * (no blocking issue); otherwise a user-facing, actionable message describing
 * the primary blocking incompatibility, with any non-blocking guidance (e.g.
 * NoData mismatch) appended. A file that cannot be opened yields a generic
 * message. \p allowPixelSizeMismatch ignores resolution differences — the
 * fusion case, where the pan grid is deliberately finer than the MS grid.
 * Dialogs call this before submitting multi-raster operators.
 */
QString rasterGridCompatibilityMessage( const QString &rasterA,
                                        const QString &rasterB,
                                        bool allowPixelSizeMismatch = false );

namespace SicnuUi
{

/// Apply standard dialog shell: min size, margins-friendly object names.
void polishDialog( QDialog *dlg, int minWidth = 520 );

/// Root vertical layout with consistent margins (12~14px) and spacing (10~12px) for dialogs.
QVBoxLayout *makeDialogRootLayout( QWidget *host );

/// Card-style section frame with bold title + optional tip (margins: 10~12px, spacing: 8px).
QFrame *makeSection( QWidget *parent, const QString &title,
                     const QString &tip = QString() );

/// Standardized GroupBox with unified styling, bold title, optional tip (margins: 10~14px, spacing: 8px).
QGroupBox *makeGroup( QWidget *parent, const QString &title,
                      const QString &tip = QString() );

/// Standardized QFormLayout with right-aligned labels, 10px h-spacing, 8px v-spacing, and standard margins.
QFormLayout *makeFormLayout( QWidget *parent = nullptr );

/// Muted helper / hint label under a field.
QLabel *makeHintLabel( QWidget *parent, const QString &text );

/// Primary action button (绿色「运行」).
void markPrimary( QPushButton *btn );

/// Ghost / secondary button chrome.
void markSecondary( QPushButton *btn );

/// Horizontal row: stretch + primary + secondary (optional).
QHBoxLayout *makeActionRow( QWidget *parent );

} // namespace SicnuUi
