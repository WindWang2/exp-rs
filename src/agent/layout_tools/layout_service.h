// src/agent/layout_tools/layout_service.h
#pragma once

//
// LayoutService — headless cartographic state for the Agent/MCP surface
// (Cartographic Layout Studio).
//
// Design contract: this service is the *programmatic* client of the exact
// same property layer the GUI Property Inspector uses. Every mutation goes
// through QgsLayoutItem setters wrapped in QgsLayoutUndoStack
// beginCommand/endCommand pairs with the same command ids the QGIS item
// widgets emit (UndoIncrementalMove / UndoIncrementalResize / UndoRotation /
// UndoOpacity / ...), so:
//
//   GUI edit  →  QgsLayoutItem setters + QgsLayoutUndoStack
//   MCP edit  →  QgsLayoutItem setters + QgsLayoutUndoStack   (identical)
//
// The QgsLayoutItem itself remains the single source of truth; nothing here
// caches item state.
//

#include <json/json.h>

#include <QString>
#include <QStringList>

class QgsLayout;
class QgsLayoutItem;
class QgsPrintLayout;

namespace sicnu::agent::layout_tools {

/**
 * Headless layout/cartography operations over QgsProject::instance().
 *
 * All geometry values exchanged through this service use millimeters (the
 * layout native unit) unless stated otherwise. Item identification uses the
 * item uuid, falling back to the item id() string when no uuid matches.
 */
class LayoutService
{
  public:
    static LayoutService &instance();

    // --- Project lifecycle -------------------------------------------------

    /// Reads a .qgs/.qgz project (clears the current one). Layouts travel with
    /// the project through QgsLayoutManager.
    bool loadProject( const QString &path, QString *error = nullptr );

    /// Writes the current project (including all layouts) to a .qgs file.
    bool saveProject( const QString &path, QString *error = nullptr );

    // --- Layout management ---------------------------------------------------

    QStringList layoutNames() const;

    /// Creates a print layout with one page of the given preset ("A4", "A3",
    /// "A2", "A1", "A0", "A5", "Letter", "Custom") and orientation.
    QgsPrintLayout *createLayout( const QString &name, const QString &pageSize = QStringLiteral( "A4" ),
                                  bool landscape = false, QString *error = nullptr );

    QgsPrintLayout *findLayout( const QString &name ) const;

    bool deleteLayout( const QString &name, QString *error = nullptr );

    // --- Items ---------------------------------------------------------------

    /// Compact item listing for agents: id/type/name/page/x/y/width/height.
    Json::Value listItemInfos( QgsLayout *layout ) const;

    /// Resolves an item by uuid or id() string.
    QgsLayoutItem *findItem( QgsLayout *layout, const QString &idOrUuid ) const;

    /// Full property map (common + type specific) for a single item.
    Json::Value itemProperties( QgsLayoutItem *item ) const;

    /// Adds an item of "map"|"label"|"legend"|"scalebar"|"northarrow"|"picture"|
    /// "shape"|"ellipse"|"triangle"|"chart" with initial properties applied.
    QgsLayoutItem *addItem( QgsLayout *layout, const QString &type, const Json::Value &props,
                            QString *error = nullptr );

    bool removeItem( QgsLayout *layout, const QString &idOrUuid, QString *error = nullptr );

    /// Applies a property map to an item. Each property is applied as one
    /// mergeable undo command inside a single "Set Item Properties" macro.
    /// Unknown/invalid keys are reported through *applied/*ignored.
    bool applyItemProperties( QgsLayoutItem *item, const Json::Value &props,
                              QStringList *applied = nullptr, QStringList *ignored = nullptr,
                              QString *error = nullptr );

    // --- Multi-item operations ------------------------------------------------

    /// Aligns items (ids resolved via findItem) using QgsLayoutAligner.
    bool alignItems( QgsLayout *layout, const QStringList &ids, const QString &alignment,
                     QString *error = nullptr );

    bool distributeItems( QgsLayout *layout, const QStringList &ids, const QString &distribution,
                          QString *error = nullptr );

    // --- Templates --------------------------------------------------------------

    /// Saves a layout as a QGIS .qpt template.
    bool saveTemplate( QgsLayout *layout, const QString &path, QString *error = nullptr );

    /// Creates (or replaces) a layout from a .qpt template.
    QgsPrintLayout *loadTemplate( const QString &name, const QString &path, QString *error = nullptr );

    // --- Export -------------------------------------------------------------------

    /// Exports a layout to png/jpg/pdf/svg with a raster memory preflight
    /// (rejects buffers above maxBytes or per-edge pixel limits).
    bool exportLayout( QgsLayout *layout, const QString &path, const QString &format, double dpi,
                       qint64 maxBytes, QString *error = nullptr );

    // --- Auto layout ----------------------------------------------------------

    /// Suggests/applies a classic thematic composition (§ Cartographic Layout
    /// Studio): title top, map primary region, legend right, scale bar bottom,
    /// north arrow top-right, source note bottom. Missing components are
    /// created (marked with "auto:" item ids); pre-existing items whose ids
    /// are not auto-managed are never moved. Returns compact info about the
    /// arranged components.
    Json::Value autoArrange( QgsLayout *layout, bool apply, QString *error = nullptr );

    // --- Helpers ---------------------------------------------------------------------

    static QString itemTypeToString( QgsLayoutItem *item );
};

} // namespace sicnu::agent::layout_tools
