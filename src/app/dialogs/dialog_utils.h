// dialog_utils.h — Shared utilities for dialog UI chrome
#pragma once

#include "dialog_help_catalog.h"

#include <QString>
#include <QGroupBox>

class QCheckBox;
class QComboBox;
class QDialog;
class QDialogButtonBox;
class QFormLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

/**
 * Populate a combo box with all valid raster layers from the current project.
 * Each item stores the layer id (QString) as QVariant data — never a raw
 * QgsRasterLayer* — so a layer removed from the project cannot leave a
 * dangling pointer behind. Resolve entries through
 * rasterLayerFromComboData() / currentRasterLayerFromCombo().
 */
void populateRasterLayerCombo( QComboBox *combo, bool clearFirst = true );

/**
 * Resolve a raster layer from the layer id stored in a combo item's QVariant
 * data (see populateRasterLayerCombo). Returns nullptr when the data is not a
 * layer id or the layer no longer exists in the project.
 */
class QgsRasterLayer;
QgsRasterLayer *rasterLayerFromComboData( const QVariant &data );

/// Resolve the currently selected raster layer of a combo (see above).
QgsRasterLayer *currentRasterLayerFromCombo( const QComboBox *combo );

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

/// A plain-text field label that can wrap and focus its editor.
QLabel *makeToolFieldLabel( const QString &text, QWidget *editor );

/// A responsive parameter row, with one wrapping caption for checkboxes.
QWidget *makeToolParameterRow( const QString &text, QWidget *editor );

/// Move a checkbox caption into a wrapping label that also toggles the checkbox.
QHBoxLayout *makeToolCheckRow( QCheckBox *check );

/// Scroll a tool form and keep the focused editor in view.
QScrollArea *makeToolFormScroll( QWidget *body );

/// Wrap a completed tool form in a scroll area and return the outer layout for its footer.
QVBoxLayout *makeScrollableToolBody( QDialog *dialog, QVBoxLayout *form );

/// Keep native button roles and switch the box to a column when its actions do not fit.
void adaptToolButtonBox( QDialogButtonBox *box );

/// Muted helper / hint label under a field.
QLabel *makeHintLabel( QWidget *parent, const QString &text );

/// Primary action button (绿色「运行」).
void markPrimary( QPushButton *btn );

/// Ghost / secondary button chrome.
void markSecondary( QPushButton *btn );

/// Horizontal row: stretch + primary + secondary (optional).
QHBoxLayout *makeActionRow( QWidget *parent );

} // namespace SicnuUi
