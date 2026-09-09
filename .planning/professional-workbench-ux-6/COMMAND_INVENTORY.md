# Command inventory @ baseline

## Registry definitions (`workbench/command_defs.cpp`, 49 commands)

project.new (Ctrl+N) · project.open (Ctrl+O) · project.save (Ctrl+S) · project.saveAs ·
project.exit (Ctrl+Q) · project.importLayer · project.newLayout · project.stacBrowse ·
layer.addRaster · layer.addVector · layer.importLayer · layer.newVector · layer.properties ·
layer.remove · layer.zoomTo · layer.attributeTable · layer.toggleEditing · layer.saveEdits ·
map.pan · map.zoomIn · map.zoomOut · map.zoomFull · map.refresh (F5) · map.identify ·
map.measureDistance · map.measureArea · map.swipe · map.compareLayers ·
rs.bandMath · rs.bandRatio · rs.spectralIndex · rs.contrastStretch · rs.applyMask · rs.qaMask ·
rs.radiometric · rs.atmospheric · rs.ortho · rs.extractBands · rs.fusion · rs.mosaic ·
rs.pca · rs.spatialFilter · rs.speckle · rs.changeDetection · rs.terrain · rs.temporal ·
workbench.classify · workbench.georefI2I · workbench.georefI2M · workbench.obia

## Duplicate / competing execution paths found

1. **Menu host**: `main_window_menus.cpp` creates QActions with `QKeySequence`
   literals + handlers that duplicate registry titles/shortcuts (conflict test
   scans only this file — #795). Registry shortcut uniqueness cannot see these
   (registry m_shortcuts vs menu literals).
2. **Workbench switcher** duplicated: `populateWorkbenchMenu` builds manual
   checkable actions instead of projecting `workbench.*` definitions.
3. **Layer context menu** (`layer_tree_menu.cpp`) — separate action set for
   properties/zoom/remove sharing slots with `layer.*` registry commands.
4. **Dialog accept paths** (#674 note in display manager): auto-load from
   TaskCenter signal, dialog accept, and job panel can each add the same
   result layer (deduped at the display seam since #674).
5. Shortcut ownership: only ONE registry projection may install the canonical
   shortcut; the assert aborts on a second distinct command (#792).

## Registry→surface projection state

- Ribbon: `shell/ribbon_controller.cpp` projects many commands via
  `addCommandButton(group, id)` (nav/inquiry/look groups visible in scan).
- Command palette: consumes `definitions()` directly (single path).
- Menus: partially migrated (menu host still literal-QAction based).
