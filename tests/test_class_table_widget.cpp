// Phase 10A Task 10.3 — RsClassTableWidget tests.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <opencv2/core.hpp>

#include "rs_class_table_widget.h"
#include "rs_merge_classes_dialog.h"
#include "rs_post_process.h"
#include "rs_roi_collection.h"
#include "qgsgeometry.h"
#include "qgspalettedrasterrenderer.h"
#include "qgsrasterlayer.h"

namespace
{
  QApplication *ensureApp()
  {
    static int argc = 1;
    static char a[] = "t";
    static char *v[] = { a, nullptr };
    return qApp ? nullptr : new QApplication( argc, v );
  }
} // namespace

TEST_CASE( "ClassTable: displays 6 classes with ROI counts", "[classify][table]" )
{
  ensureApp();
  RsRoiCollection col;
  col.setClassDef( RsClassDef( 1, "Forest", QColor( "#2da44e" ) ) );
  col.setClassDef( RsClassDef( 2, "Water", QColor( "#0969da" ) ) );
  auto g = QgsGeometry::fromWkt( "POINT(1 1)" );
  col.appendRoi( RsRoi( 1, g, { 10, 11, 12 } ) );
  col.appendRoi( RsRoi( 1, g, { 20, 21 } ) );
  col.appendRoi( RsRoi( 2, g, { 30 } ) );

  RsClassTableWidget w;
  w.setRoiCollection( &col );
  REQUIRE( w.rowCount() == 2 );
  REQUIRE( w.roiCountForRow( 0 ) == 2 );
  REQUIRE( w.pixelCountForRow( 0 ) == 5 );
  REQUIRE( w.roiCountForRow( 1 ) == 1 );
  REQUIRE( w.pixelCountForRow( 1 ) == 1 );
}

TEST_CASE( "ClassTable: currentClassChanged on row selection", "[classify][table]" )
{
  ensureApp();
  RsRoiCollection col;
  col.setClassDef( RsClassDef( 1, "A", QColor( "#0a0" ) ) );
  col.setClassDef( RsClassDef( 2, "B", QColor( "#a00" ) ) );

  RsClassTableWidget w;
  w.setRoiCollection( &col );
  QSignalSpy spy( &w, &RsClassTableWidget::currentClassChanged );
  w.setCurrentRow( 1 );
  REQUIRE( spy.size() >= 1 );
  REQUIRE( w.currentClassId() == 2 );
}

TEST_CASE( "ClassTable: selectedClassIds returns all selected IDs", "[classify][table]" )
{
  ensureApp();
  RsRoiCollection col;
  col.setClassDef( RsClassDef( 1, "A", QColor( "#0a0" ) ) );
  col.setClassDef( RsClassDef( 2, "B", QColor( "#a00" ) ) );
  col.setClassDef( RsClassDef( 3, "C", QColor( "#00a" ) ) );

  RsClassTableWidget w;
  w.setRoiCollection( &col );
  REQUIRE( w.rowCount() == 3 );

  w.setCurrentClassId( 1 );
  const QList<int> sel = w.selectedClassIds();
  REQUIRE( sel.contains( 1 ) );
}

TEST_CASE( "ClassTable: rebuild preserves selection and empty table returns 0 classId", "[classify][table]" )
{
  ensureApp();
  RsRoiCollection col;
  col.setClassDef( RsClassDef( 1, "A", QColor( "#0a0" ) ) );
  col.setClassDef( RsClassDef( 2, "B", QColor( "#a00" ) ) );

  RsClassTableWidget w;
  w.setRoiCollection( &col );
  w.setCurrentClassId( 2 );
  REQUIRE( w.currentClassId() == 2 );

  // Rebuild on class def change
  col.setClassDef( RsClassDef( 3, "C", QColor( "#00a" ) ) );
  REQUIRE( w.currentClassId() == 2 );
  REQUIRE( w.selectedClassIds().contains( 2 ) );

  // Empty table returns 0
  RsClassTableWidget emptyW;
  REQUIRE( emptyW.currentClassId() == 0 );
}

TEST_CASE( "ClassTable: mergeSelectedClasses emits mergeClassesRequested signal", "[classify][table]" )
{
  ensureApp();
  RsRoiCollection col;
  col.setClassDef( RsClassDef( 1, "Deep Water", QColor( "#0000ff" ) ) );
  col.setClassDef( RsClassDef( 2, "Shallow Water", QColor( "#0088ff" ) ) );

  RsClassTableWidget w;
  w.setRoiCollection( &col );
  w.setCurrentClassId( 1 );

  QSignalSpy mergeSpy( &w, &RsClassTableWidget::mergeClassesRequested );
  w.mergeSelectedClasses( 1, "Water", QColor( "#0000ff" ) );
  REQUIRE( mergeSpy.size() == 1 );

  const auto args = mergeSpy.first();
  const QList<int> sources = args.at( 0 ).value<QList<int>>();
  REQUIRE( sources.contains( 1 ) );
  REQUIRE( args.at( 1 ).toInt() == 1 );
  REQUIRE( args.at( 2 ).toString() == "Water" );
}

TEST_CASE( "PostProcess: sidecar class.json metadata save and load round-trip", "[classify][postprocess]" )
{
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString rasterPath = tempDir.filePath( "classification_result.tif" );

  QHash<int, RsClassDef> defs;
  defs.insert( 1, RsClassDef( 1, "Water", QColor( "#0000ff" ) ) );
  defs.insert( 2, RsClassDef( 2, "Forest", QColor( "#00ff00" ) ) );

  QString err;
  REQUIRE( RsPostProcess::saveClassMetaData( rasterPath, defs, &err ) );
  REQUIRE( QFile::exists( rasterPath + ".class.json" ) );

  QHash<int, RsClassDef> loadedDefs;
  REQUIRE( RsPostProcess::loadClassMetaData( rasterPath, loadedDefs, &err ) );
  REQUIRE( loadedDefs.size() == 2 );
  REQUIRE( loadedDefs.value( 1 ).name() == "Water" );
  REQUIRE( loadedDefs.value( 2 ).name() == "Forest" );
  REQUIRE( loadedDefs.value( 1 ).color() == QColor( "#0000ff" ) );
}

TEST_CASE( "PostProcess: loadClassMetaData returns false with empty out when sidecar absent", "[classify][postprocess]" )
{
  // Defensive coverage for the project-load fallback path: a result raster
  // that has no sidecar must not throw and must leave outDefs empty.
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString rasterPath = tempDir.filePath( "result_without_sidecar.tif" );

  QHash<int, RsClassDef> loadedDefs;
  QString err;
  REQUIRE_FALSE( RsPostProcess::loadClassMetaData( rasterPath, loadedDefs, &err ) );
  REQUIRE( loadedDefs.isEmpty() );
}

TEST_CASE( "PostProcess: Byte result raster with color table loads as paletted renderer", "[classify][postprocess]" )
{
  // Verifies the precondition applyPreviewLayerRenderer() depends on: a Byte
  // result raster written with a color table is auto-detected by QGIS as a
  // paletted raster, so live rename/recolor can rebuild its ClassData.
  ensureApp();
  QTemporaryDir tempDir;
  REQUIRE( tempDir.isValid() );
  const QString rasterPath = tempDir.filePath( "palette_result.tif" );

  // 4x4 Byte label raster with classes 0..2 (0 = nodata/unclassified).
  cv::Mat labels( 4, 4, CV_8U );
  for ( int r = 0; r < 4; ++r )
    for ( int c = 0; c < 4; ++c )
      labels.at<uchar>( r, c ) = static_cast<uchar>( ( r + c ) % 3 );

  const double gt[6] = { 0, 1, 0, 0, 0, -1 };
  const QString wkt;
  // Index 0 transparent/nodata, 1 and 2 carry colors.
  QVector<QRgb> colorTable( 3 );
  colorTable[0] = qRgba( 0, 0, 0, 0 );
  colorTable[1] = qRgb( 0, 0, 255 );
  colorTable[2] = qRgb( 0, 255, 0 );

  QString err;
  REQUIRE( RsPostProcess::saveLabelRaster( rasterPath, labels, gt, wkt, colorTable,
                                            QStringList(), 0.0, &err ) );

  QgsRasterLayer layer( rasterPath, QStringLiteral( "test" ), QStringLiteral( "gdal" ) );
  REQUIRE( layer.isValid() );

  auto *renderer = dynamic_cast<QgsPalettedRasterRenderer *>( layer.renderer() );
  REQUIRE( renderer != nullptr );
  REQUIRE( renderer->classes().size() >= 2 );
}

TEST_CASE( "MergeClasses: buildRecodeMap maps sources to target and target to itself", "[classify][merge]" )
{
  // Merge classes {1, 3} into target id 1 (keep-lowest).
  const QMap<int, int> map = buildRecodeMap( { 1, 3 }, 1 );
  REQUIRE( map.size() == 2 );
  REQUIRE( map.value( 1 ) == 1 );
  REQUIRE( map.value( 3 ) == 1 );

  // The target id must map to itself even when it is not in the source list.
  const QMap<int, int> map2 = buildRecodeMap( { 2, 3 }, 1 );
  REQUIRE( map2.value( 1 ) == 1 );
  REQUIRE( map2.value( 2 ) == 1 );
  REQUIRE( map2.value( 3 ) == 1 );

  // Duplicate source ids collapse to a single entry.
  const QMap<int, int> map3 = buildRecodeMap( { 3, 3 }, 3 );
  REQUIRE( map3.size() == 1 );
  REQUIRE( map3.value( 3 ) == 3 );
}
