/***************************************************************************
 * map_tool_canvas_item.h — canvas-destruction-safe item ownership for tools
 *
 * A QgsRubberBand / QgsVertexMarker created with a canvas is a scene-owned
 * QGraphicsItem. QgsMapCanvas::~QgsMapCanvas deletes every child QgsMapTool
 * while the scene still exists (see the fix for issue #1048), so a tool
 * destructor may free its items normally. If a tool is instead destroyed
 * AFTER its canvas — a reparented tool, a deleteLater() callback that fires
 * late, or a tool torn down by a parent widget after the canvas child died —
 * the scene teardown already destroyed the item and the raw pointer dangles.
 *
 * QgsMapCanvas nulls every child tool's canvas pointer right before deleting
 * the scene items, so `tool->canvas() == nullptr` is the exact, documented
 * signal that the scene items are gone. Every tool that owns canvas items
 * must funnel its deletion through deleteToolCanvasItem().
 *
 * CONSTRAINT: the signal only exists for tools that are still QObject children
 * of the canvas. A tool reparented away from its canvas (e.g. the georef tools
 * call setParent(shell)) is not visited by ~QgsMapCanvas, so its canvas()
 * becomes a dangling non-null pointer once the canvas dies. Such tools must
 * not own raw canvas items — the snap-indicator style weak tracking
 * (QObjectParentUniquePtr on the canvas) is the supported pattern for them.
 * No current guard call site is reparented.
 ***************************************************************************/
#pragma once

#include <qgsmaptool.h>

namespace sicnu::app
{

/// Delete a map-tool-owned canvas item without ever touching scene memory that
/// the canvas destructor already reclaimed. @p item is nulled unconditionally.
/// Returns true when the item was deleted by this call.
template <typename T>
inline bool deleteToolCanvasItem( QgsMapTool *tool, T *&item )
{
    if ( !item )
        return false;
    T *victim = item;
    item = nullptr;
    if ( !tool || !tool->canvas() )
        return false; // canvas gone: its scene teardown deleted the item
    delete victim;
    return true;
}

} // namespace sicnu::app
