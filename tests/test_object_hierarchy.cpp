// test_object_hierarchy.cpp — Hierarchical OBIA V1 unit tests (#16–#21).
// Pure analysis: no OTB binary required, no QWidget.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "analysis/segmentation/rs_parent_link.h"
#include "analysis/segmentation/rs_object_hierarchy.h"
#include "analysis/segmentation/rs_segmenter_port.h"
#include "analysis/segmentation/rs_hierarchy_features.h"
#include "analysis/segmentation/rs_class_raster.h"
#include "analysis/segmentation/rs_object_classify.h"
#include "analysis/segmentation/rs_otb_segmenter.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gdal.h>
#include <gdal_priv.h>

using Catch::Approx;

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// 4x4 fine:
//  1 1 2 2
//  1 1 2 2
//  3 3 4 4
//  3 3 4 4
static RsSegmentMap makeFineMap()
{
    QVector<quint32> labels = {
        1, 1, 2, 2,
        1, 1, 2, 2,
        3, 3, 4, 4,
        3, 3, 4, 4
    };
    return RsSegmentMap( labels, 4, 4 );
}

// 4x4 coarse majority parents:
//  10 10 10 10
//  10 10 10 10
//  20 20 20 20
//  20 20 20 20
// fine 1,2 → 10; fine 3,4 → 20
static RsSegmentMap makeCoarseMap()
{
    QVector<quint32> labels = {
        10, 10, 10, 10,
        10, 10, 10, 10,
        20, 20, 20, 20,
        20, 20, 20, 20
    };
    return RsSegmentMap( labels, 4, 4 );
}

// ---------------------------------------------------------------------------
// #16 Parent-link
// ---------------------------------------------------------------------------

TEST_CASE( "ParentLink majority maps fine to coarse", "[hierarchy][parentlink]" )
{
    RsPixelMajorityParentLink linker;
    const auto fine = makeFineMap();
    const auto coarse = makeCoarseMap();

    RsParentTable t = linker.link( fine, coarse );
    REQUIRE( t.ok );
    REQUIRE( t.fineToParent.value( 1 ) == 10 );
    REQUIRE( t.fineToParent.value( 2 ) == 10 );
    REQUIRE( t.fineToParent.value( 3 ) == 20 );
    REQUIRE( t.fineToParent.value( 4 ) == 20 );
    REQUIRE( t.fineToParent.size() == 4 );
}

TEST_CASE( "ParentLink tie breaks to smaller coarse id", "[hierarchy][parentlink]" )
{
    // 2x2 fine all id 1; coarse half 5 half 7 (equal votes) → parent 5
    QVector<quint32> fineL = { 1, 1, 1, 1 };
    QVector<quint32> coarseL = { 7, 5, 7, 5 };
    RsSegmentMap fine( fineL, 2, 2 );
    RsSegmentMap coarse( coarseL, 2, 2 );

    RsPixelMajorityParentLink linker;
    RsParentTable t = linker.link( fine, coarse );
    REQUIRE( t.ok );
    REQUIRE( t.fineToParent.value( 1 ) == 5 );
}

TEST_CASE( "ParentLink orphan when no valid coarse votes", "[hierarchy][parentlink]" )
{
    // fine 1 over coarse 0 only → parent 0
    QVector<quint32> fineL = { 1, 1, 1, 1 };
    QVector<quint32> coarseL = { 0, 0, 0, 0 };
    RsSegmentMap fine( fineL, 2, 2 );
    RsSegmentMap coarse( coarseL, 2, 2 );

    RsPixelMajorityParentLink linker;
    RsParentTable t = linker.link( fine, coarse );
    REQUIRE( t.ok );
    REQUIRE( t.fineToParent.value( 1 ) == 0 );
}

TEST_CASE( "ParentLink skips label 0 and size mismatch fails", "[hierarchy][parentlink]" )
{
    QVector<quint32> fineL = {
        0, 1, 1, 0,
        0, 1, 1, 0
    };
    QVector<quint32> coarseL = {
        0, 9, 9, 0,
        0, 9, 9, 0
    };
    RsSegmentMap fine( fineL, 4, 2 );
    RsSegmentMap coarse( coarseL, 4, 2 );

    RsPixelMajorityParentLink linker;
    RsParentTable t = linker.link( fine, coarse );
    REQUIRE( t.ok );
    REQUIRE( t.fineToParent.contains( 1 ) );
    REQUIRE( !t.fineToParent.contains( 0 ) );
    REQUIRE( t.fineToParent.value( 1 ) == 9 );

    // Size mismatch
    RsSegmentMap wrong( QVector<quint32>( 9, 1 ), 3, 3 );
    RsParentTable bad = linker.link( fine, wrong );
    REQUIRE( !bad.ok );
    REQUIRE( bad.errorMessage.contains( QStringLiteral( "mismatch" ), Qt::CaseInsensitive ) );
}

// ---------------------------------------------------------------------------
// #16 Hierarchy store
// ---------------------------------------------------------------------------

TEST_CASE( "Hierarchy stores levels and parent/children queries", "[hierarchy]" )
{
    auto fine = makeFineMap();
    auto coarse = makeCoarseMap();
    RsPixelMajorityParentLink linker;
    RsParentTable edge = linker.link( fine, coarse );
    REQUIRE( edge.ok );

    RsObjectHierarchy h;
    QString err;
    REQUIRE( h.setLevels( { fine, coarse }, { edge }, &err ) );
    REQUIRE( h.levelCount() == 2 );
    REQUIRE( h.level( 0 ).segmentCount() == 4 );
    REQUIRE( h.level( 1 ).segmentCount() == 2 );

    REQUIRE( h.parentOf( 0, 1 ) == 10 );
    REQUIRE( h.parentOf( 0, 3 ) == 20 );
    REQUIRE( h.parentOf( 0, 99 ) == 0 );

    auto kids10 = h.childrenOf( 1, 10 );
    REQUIRE( kids10.size() == 2 );
    REQUIRE( kids10.contains( 1 ) );
    REQUIRE( kids10.contains( 2 ) );

    REQUIRE( h.childCount( 1, 10 ) == 2 );
    REQUIRE( h.childCount( 0, 1 ) == 0 ); // finest has no children

    // area fine1 = 4, parent10 = 8 → 0.5
    REQUIRE( h.areaRatioToParent( 0, 1 ) == Approx( 0.5 ) );
    REQUIRE( h.areaRatioToParent( 1, 10 ) == Approx( 0.0 ) ); // coarsest: no parent
}

TEST_CASE( "Hierarchy setLevels rejects grid size mismatch", "[hierarchy]" )
{
    RsSegmentMap a( QVector<quint32>( 4, 1 ), 2, 2 );
    RsSegmentMap b( QVector<quint32>( 9, 1 ), 3, 3 );
    RsParentTable emptyTable;
    emptyTable.ok = true;

    RsObjectHierarchy h;
    QString err;
    REQUIRE_FALSE( h.setLevels( { a, b }, { emptyTable }, &err ) );
    REQUIRE( h.isEmpty() );
    REQUIRE( err.contains( QStringLiteral( "mismatch" ), Qt::CaseInsensitive ) );
}

// ---------------------------------------------------------------------------
// #17 buildLevels with fake segmenter
// ---------------------------------------------------------------------------

class FakeSegmenter : public RsSegmenterPort
{
  public:
    QVector<RsSegmentMap> maps;
    QVector<RsLevelSpec> seenSpecs;
    int callCount = 0;
    int failAt = -1;

    RsSegmenterResult segment( const QString &rasterPath,
                               const RsLevelSpec &spec,
                               const std::function<bool()> & ) override
    {
        Q_UNUSED( rasterPath );
        seenSpecs.append( spec );
        RsSegmenterResult r;
        if ( failAt >= 0 && callCount == failAt )
        {
            r.errorMessage = QStringLiteral( "fake fail at %1" ).arg( callCount );
            ++callCount;
            return r;
        }
        if ( callCount >= maps.size() )
        {
            r.errorMessage = QStringLiteral( "no more maps" );
            ++callCount;
            return r;
        }
        r.segMap = maps[callCount];
        r.ok = true;
        ++callCount;
        return r;
    }
};

TEST_CASE( "buildLevels segments then links with injected fake", "[hierarchy][buildLevels]" )
{
    FakeSegmenter fake;
    fake.maps = { makeFineMap(), makeCoarseMap() };

    RsLevelSpec fineSpec;
    fineSpec.filter = RsLevelSpec::Filter::MeanShift;
    fineSpec.spatialRadius = 3;
    fineSpec.name = QStringLiteral( "fine" );

    RsLevelSpec coarseSpec;
    coarseSpec.filter = RsLevelSpec::Filter::Watershed;
    coarseSpec.watershedThreshold = 0.05;
    coarseSpec.name = QStringLiteral( "coarse" );

    RsObjectHierarchy h;
    RsPixelMajorityParentLink linker;
    QString err;
    const bool ok = h.buildLevels( QStringLiteral( "/tmp/dummy.tif" ),
                                   { fineSpec, coarseSpec }, fake, linker, &err );
    INFO( "buildLevels err=" << err.toStdString()
                             << " callCount=" << fake.callCount );
    REQUIRE( ok );
    REQUIRE( fake.callCount == 2 );
    REQUIRE( fake.seenSpecs.size() == 2 );
    REQUIRE( fake.seenSpecs[0].filter == RsLevelSpec::Filter::MeanShift );
    REQUIRE( fake.seenSpecs[1].filter == RsLevelSpec::Filter::Watershed );

    REQUIRE( h.levelCount() == 2 );
    REQUIRE( h.parentOf( 0, 1 ) == 10 );
    REQUIRE( h.childrenOf( 1, 20 ).contains( 4 ) );
}

TEST_CASE( "buildLevels fails clearly when a level fails", "[hierarchy][buildLevels]" )
{
    FakeSegmenter fake;
    fake.maps = { makeFineMap(), makeCoarseMap() };
    fake.failAt = 1;

    RsObjectHierarchy h;
    RsPixelMajorityParentLink linker;
    QString err;
    REQUIRE_FALSE( h.buildLevels( QStringLiteral( "/tmp/dummy.tif" ),
                                   { RsLevelSpec{}, RsLevelSpec{} },
                                   fake, linker, &err ) );
    REQUIRE( h.isEmpty() );
    REQUIRE( err.contains( QStringLiteral( "Level 1" ) ) );
}

TEST_CASE( "relinkEdgesTouching only refreshes adjacent edges", "[hierarchy]" )
{
    auto fine = makeFineMap();
    auto coarse = makeCoarseMap();
    RsPixelMajorityParentLink linker;
    auto edge = linker.link( fine, coarse );

    RsObjectHierarchy h;
    REQUIRE( h.setLevels( { fine, coarse }, { edge } ) );

    // Replace coarse with inverted parents for re-link demo:
    // top half 20, bottom half 10 → fine 1,2→20; 3,4→10
    QVector<quint32> newCoarse = {
        20, 20, 20, 20,
        20, 20, 20, 20,
        10, 10, 10, 10,
        10, 10, 10, 10
    };
    // Directly poke level via rebuild: setLevels again with same fine
    RsSegmentMap coarse2( newCoarse, 4, 4 );
    auto edge2 = linker.link( fine, coarse2 );
    REQUIRE( h.setLevels( { fine, coarse2 }, { edge2 } ) );
    REQUIRE( h.parentOf( 0, 1 ) == 20 );

    // relink after set should be idempotent
    QString err;
    REQUIRE( h.relinkEdgesTouching( 1, linker, &err ) );
    REQUIRE( h.parentOf( 0, 1 ) == 20 );
}

// ---------------------------------------------------------------------------
// #18 OTB availability (no live OTB required)
// ---------------------------------------------------------------------------

TEST_CASE( "OtbSegmenter reports clear error when OTB missing or bad path", "[hierarchy][otb]" )
{
    RsOtbSegmenter seg;
    RsLevelSpec spec;
    spec.filter = RsLevelSpec::Filter::MeanShift;

    // Nonexistent raster always fails clearly
    auto r = seg.segment( QStringLiteral( "/nonexistent/raster.tif" ), spec );
    REQUIRE_FALSE( r.ok );
    REQUIRE( !r.errorMessage.isEmpty() );

    // isAvailable is a pure probe (true or false depending on environment)
    ( void ) RsOtbSegmenter::isAvailable();
}

// ---------------------------------------------------------------------------
// #20 F2a inter-level columns (fixture hierarchy, no raster extract)
// ---------------------------------------------------------------------------

#ifdef SICNU_HAS_OPENCV
TEST_CASE( "F2a appendInterLevelFeatures childCount and areaRatio", "[hierarchy][features]" )
{
    auto fine = makeFineMap();
    auto coarse = makeCoarseMap();
    RsPixelMajorityParentLink linker;
    auto edge = linker.link( fine, coarse );
    RsObjectHierarchy h;
    REQUIRE( h.setLevels( { fine, coarse }, { edge } ) );

    // Synthetic flat matrix: 4 fine segments × 3 base features
    QVector<quint32> ids = { 1, 2, 3, 4 };
    cv::Mat flat( 4, 3, CV_32F );
    for ( int r = 0; r < 4; ++r )
        for ( int c = 0; c < 3; ++c )
            flat.at<float>( r, c ) = static_cast<float>( r * 10 + c );

    auto mat = RsHierarchyFeatures::appendInterLevelFeatures( flat, ids, h, /*level*/ 0 );
    REQUIRE( mat.ok );
    REQUIRE( mat.X.cols == 5 ); // 3 + childCount + areaRatio
    REQUIRE( mat.spectralShapeCols == 3 );
    REQUIRE( mat.meta.segmentIds.size() == 4 );
    REQUIRE( mat.meta.parentIds[0] == 10 );
    REQUIRE( mat.meta.parentIds[2] == 20 );

    // Level 0: childCount always 0
    REQUIRE( mat.X.at<float>( 0, 3 ) == Approx( 0.0f ) );
    // areaRatio 4/8 = 0.5
    REQUIRE( mat.X.at<float>( 0, 4 ) == Approx( 0.5f ) );

    // Coarse level: childCount for 10 is 2, areaRatio 0 (no parent)
    QVector<quint32> coarseIds = { 10, 20 };
    cv::Mat flatC( 2, 2, CV_32F, cv::Scalar( 1.0f ) );
    auto matC = RsHierarchyFeatures::appendInterLevelFeatures( flatC, coarseIds, h, 1 );
    REQUIRE( matC.ok );
    REQUIRE( matC.X.at<float>( 0, 2 ) == Approx( 2.0f ) ); // childCount
    REQUIRE( matC.X.at<float>( 0, 3 ) == Approx( 0.0f ) ); // areaRatio
    REQUIRE( matC.meta.parentIds[0] == 0 );
}

TEST_CASE( "F2a orphan zeros areaRatio and keeps row", "[hierarchy][features]" )
{
    // Fine with orphan (coarse all 0 under segment 2 region only) —
    // Use mixed coarse: segment 1 has parent, segment 2 is over nodata.
    QVector<quint32> fineL = {
        1, 1, 2, 2,
        1, 1, 2, 2
    };
    QVector<quint32> coarseL = {
        9, 9, 0, 0,
        9, 9, 0, 0
    };
    RsSegmentMap fine( fineL, 4, 2 );
    RsSegmentMap coarse( coarseL, 4, 2 );
    RsPixelMajorityParentLink linker;
    auto edge = linker.link( fine, coarse );
    REQUIRE( edge.fineToParent.value( 1 ) == 9 );
    REQUIRE( edge.fineToParent.value( 2 ) == 0 );

    RsObjectHierarchy h;
    REQUIRE( h.setLevels( { fine, coarse }, { edge } ) );

    QVector<quint32> ids = { 1, 2 };
    cv::Mat flat( 2, 1, CV_32F );
    flat.at<float>( 0, 0 ) = 1.0f;
    flat.at<float>( 1, 0 ) = 2.0f;

    auto mat = RsHierarchyFeatures::appendInterLevelFeatures( flat, ids, h, 0 );
    REQUIRE( mat.ok );
    REQUIRE( mat.meta.parentIds[1] == 0 );
    REQUIRE( mat.X.at<float>( 1, 2 ) == Approx( 0.0f ) ); // orphan areaRatio
    REQUIRE( mat.X.rows == 2 ); // orphan row kept
}
#endif

// ---------------------------------------------------------------------------
// #21 paint class raster from known labels (no OTB)
// ---------------------------------------------------------------------------

static QString writeTinyRefRaster( const QString &dir )
{
    const QString path = dir + QStringLiteral( "/ref.tif" );
    GDALAllRegister();
    GDALDriverH drv = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( drv, path.toUtf8().constData(), 4, 4, 1, GDT_Byte, nullptr );
    REQUIRE( ds );
    double gt[6] = { 100.0, 1.0, 0.0, 200.0, 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    QVector<quint8> buf( 16, 42 );
    GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 4, 4,
                  buf.data(), 4, 4, GDT_Byte, 0, 0 );
    GDALClose( ds );
    return path;
}

TEST_CASE( "ClassRaster paint from known segment classes", "[hierarchy][writeback]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString ref = writeTinyRefRaster( tmp.path() );
    const QString out = tmp.path() + QStringLiteral( "/class.tif" );

    auto fine = makeFineMap();
    QMap<quint32, int> classes;
    classes[1] = 1;
    classes[2] = 1;
    classes[3] = 2;
    classes[4] = 2;

    QHash<int, QColor> colors;
    colors[1] = QColor( 0, 255, 0 );
    colors[2] = QColor( 0, 0, 255 );

    auto r = RsClassRaster::paint( fine, classes, ref, out, colors );
    REQUIRE( r.ok );
    REQUIRE( r.totalPixels == 16 );
    REQUIRE( QFile::exists( out ) );

    RsSegmentMap painted = RsSegmentMap::fromGeoTIFF( out );
    REQUIRE( painted.width() == 4 );
    REQUIRE( painted.labelAt( 0, 0 ) == 1 ); // seg 1 → class 1
    REQUIRE( painted.labelAt( 3, 3 ) == 2 ); // seg 4 → class 2

    // NoData must be set to 0
    GDALDatasetH ds = GDALOpen( out.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds );
    int success = 0;
    const double nd = GDALGetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), &success );
    GDALClose( ds );
    REQUIRE( success );
    REQUIRE( nd == Approx( 0.0 ) );
}

TEST_CASE( "ClassRaster paint partial classes leave unlabeled as 0", "[hierarchy][writeback]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString ref = writeTinyRefRaster( tmp.path() );
    const QString out = tmp.path() + QStringLiteral( "/partial.tif" );

    auto fine = makeFineMap();
    QMap<quint32, int> classes;
    classes[1] = 1; // only segment 1 labeled
    auto r = RsClassRaster::paint( fine, classes, ref, out );
    REQUIRE( r.ok );
    REQUIRE( r.totalPixels == 4 ); // only seg 1 (4 px)

    RsSegmentMap painted = RsSegmentMap::fromGeoTIFF( out );
    REQUIRE( painted.labelAt( 0, 0 ) == 1 );
    REQUIRE( painted.labelAt( 0, 2 ) == 0 ); // seg 2 unlabeled
    REQUIRE( painted.labelAt( 3, 3 ) == 0 ); // seg 4 unlabeled
}

TEST_CASE( "ClassRaster paint rejects class id 0 and size mismatch", "[hierarchy][writeback]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString ref = writeTinyRefRaster( tmp.path() );
    auto fine = makeFineMap();

    QMap<quint32, int> bad;
    bad[1] = 0;
    auto r0 = RsClassRaster::paint( fine, bad, ref, tmp.path() + QStringLiteral( "/bad0.tif" ) );
    REQUIRE_FALSE( r0.ok );
    REQUIRE( r0.errorMessage.contains( QStringLiteral( ">= 1" ) ) );

    // Wrong-size reference
    GDALAllRegister();
    const QString badRef = tmp.path() + QStringLiteral( "/badref.tif" );
    GDALDriverH drv = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( drv, badRef.toUtf8().constData(), 2, 2, 1, GDT_Byte, nullptr );
    REQUIRE( ds );
    GDALClose( ds );
    QMap<quint32, int> okCls;
    okCls[1] = 1;
    const QString mmOut = tmp.path() + QStringLiteral( "/mm.tif" );
    auto r1 = RsClassRaster::paint( fine, okCls, badRef, mmOut );
    REQUIRE_FALSE( r1.ok );
    REQUIRE( r1.errorMessage.contains( QStringLiteral( "size" ), Qt::CaseInsensitive ) );
    // Incomplete GeoTIFF from GDALCreate must be removed on size-mismatch fail.
    REQUIRE_FALSE( QFile::exists( mmOut ) );
}

TEST_CASE( "ClassRaster paint UInt16 path for class id > 255", "[hierarchy][writeback]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString ref = writeTinyRefRaster( tmp.path() );
    const QString out = tmp.path() + QStringLiteral( "/u16.tif" );

    auto fine = makeFineMap();
    QMap<quint32, int> classes;
    classes[1] = 300;
    classes[2] = 300;
    classes[3] = 400;
    classes[4] = 400;

    auto r = RsClassRaster::paint( fine, classes, ref, out );
    REQUIRE( r.ok );
    REQUIRE( r.totalPixels == 16 );

    GDALDatasetH ds = GDALOpen( out.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds );
    REQUIRE( GDALGetRasterDataType( GDALGetRasterBand( ds, 1 ) ) == GDT_UInt16 );
    quint16 v = 0;
    GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Read, 0, 0, 1, 1, &v, 1, 1, GDT_UInt16, 0, 0 );
    GDALClose( ds );
    REQUIRE( v == 300 );
}

TEST_CASE( "ClassRaster polygonize smoke masks background", "[hierarchy][writeback]" )
{
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString ref = writeTinyRefRaster( tmp.path() );
    const QString classPath = tmp.path() + QStringLiteral( "/class.tif" );
    const QString shpPath = tmp.path() + QStringLiteral( "/classes.shp" );

    auto fine = makeFineMap();
    QMap<quint32, int> classes;
    classes[1] = 1;
    classes[2] = 1;
    // leave 3,4 unlabeled → 0
    REQUIRE( RsClassRaster::paint( fine, classes, ref, classPath ).ok );

    auto poly = RsClassRaster::polygonize( classPath, shpPath );
    REQUIRE( poly.ok );
    REQUIRE( QFile::exists( shpPath ) );
}

TEST_CASE( "buildLevels parent-link failure clears hierarchy", "[hierarchy][buildLevels]" )
{
    class FailingLinker : public RsParentLinkStrategy
    {
      public:
        RsParentTable link( const RsSegmentMap &, const RsSegmentMap & ) const override
        {
            RsParentTable t;
            t.ok = false;
            t.errorMessage = QStringLiteral( "forced parent-link failure" );
            return t;
        }
    };

    FakeSegmenter fake;
    fake.maps = { makeFineMap(), makeCoarseMap() };
    RsObjectHierarchy h;
    // Pre-fill to prove clear on fail
    {
        auto fine = makeFineMap();
        auto coarse = makeCoarseMap();
        RsPixelMajorityParentLink okLinker;
        auto edge = okLinker.link( fine, coarse );
        REQUIRE( h.setLevels( { fine, coarse }, { edge } ) );
        REQUIRE( !h.isEmpty() );
    }

    FailingLinker linker;
    QString err;
    REQUIRE_FALSE( h.buildLevels( QStringLiteral( "/tmp/dummy.tif" ),
                                   { RsLevelSpec{}, RsLevelSpec{} },
                                   fake, linker, &err ) );
    REQUIRE( h.isEmpty() );
    REQUIRE( err.contains( QStringLiteral( "forced parent-link" ) ) );
}

TEST_CASE( "buildLevels rejects all-zero label map", "[hierarchy][buildLevels]" )
{
    FakeSegmenter fake;
    fake.maps = {
        RsSegmentMap( QVector<quint32>( 16, 0 ), 4, 4 ), // all nodata
        makeCoarseMap()
    };
    RsObjectHierarchy h;
    RsPixelMajorityParentLink linker;
    QString err;
    REQUIRE_FALSE( h.buildLevels( QStringLiteral( "/tmp/dummy.tif" ),
                                   { RsLevelSpec{}, RsLevelSpec{} },
                                   fake, linker, &err ) );
    REQUIRE( h.isEmpty() );
    REQUIRE( err.contains( QStringLiteral( "no objects" ), Qt::CaseInsensitive ) );
}

TEST_CASE( "setLevels rejects parentTables size mismatch", "[hierarchy]" )
{
    auto fine = makeFineMap();
    auto coarse = makeCoarseMap();
    RsObjectHierarchy h;
    QString err;
    REQUIRE_FALSE( h.setLevels( { fine, coarse }, {}, &err ) ); // need 1 table
    REQUIRE( err.contains( QStringLiteral( "parentTables" ), Qt::CaseInsensitive ) );
    REQUIRE( h.isEmpty() );
}

TEST_CASE( "buildLevels cancel path clears hierarchy", "[hierarchy][buildLevels]" )
{
    FakeSegmenter fake;
    fake.maps = { makeFineMap(), makeCoarseMap() };
    RsObjectHierarchy h;
    RsPixelMajorityParentLink linker;
    QString err;
    int calls = 0;
    auto cancelAfterFirst = [&]() {
        ++calls;
        return calls > 1; // cancel before second level / during second
    };
    // Cancel on second segmenter call: isCanceled checked at start of each level
    // After first segment, callCount=1, next loop iteration cancels.
    REQUIRE_FALSE( h.buildLevels( QStringLiteral( "/tmp/dummy.tif" ),
                                   { RsLevelSpec{}, RsLevelSpec{} },
                                   fake, linker, &err, cancelAfterFirst ) );
    REQUIRE( h.isEmpty() );
    REQUIRE( err.contains( QStringLiteral( "cancel" ), Qt::CaseInsensitive ) );
}

TEST_CASE( "ROI majority tie prefers smaller class id", "[hierarchy][roi]" )
{
    // Pure unit of the tie rule used by GUI/operator (inlined here for seam test).
    QHash<int, int> votes;
    votes[7] = 3;
    votes[5] = 3; // equal votes
    votes[9] = 1;

    int bestClass = 0;
    int bestCount = 0;
    for ( auto cit = votes.constBegin(); cit != votes.constEnd(); ++cit )
    {
        if ( cit.value() > bestCount
             || ( cit.value() == bestCount && ( bestClass == 0 || cit.key() < bestClass ) ) )
        {
            bestCount = cit.value();
            bestClass = cit.key();
        }
    }
    REQUIRE( bestClass == 5 );
    REQUIRE( bestCount == 3 );
}

#ifdef SICNU_HAS_OPENCV
#include "analysis/classification/rs_classifier_svm.h"

TEST_CASE( "ObjectClassify trains and predicts on synthetic matrix", "[hierarchy][classify]" )
{
    // Two clusters in 2D feature space
    cv::Mat X( 4, 2, CV_32F );
    X.at<float>( 0, 0 ) = 0; X.at<float>( 0, 1 ) = 0;
    X.at<float>( 1, 0 ) = 0.1f; X.at<float>( 1, 1 ) = 0.1f;
    X.at<float>( 2, 0 ) = 10; X.at<float>( 2, 1 ) = 10;
    X.at<float>( 3, 0 ) = 10.1f; X.at<float>( 3, 1 ) = 9.9f;
    QVector<quint32> ids = { 1, 2, 3, 4 };
    QMap<quint32, int> train;
    train[1] = 1;
    train[3] = 2;

    RsClassifierSvm backend;
    auto r = RsObjectClassify::classify( X, ids, train, backend );
    REQUIRE( r.ok );
    REQUIRE( r.labeledCount == 2 );
    REQUIRE( r.segmentClasses.value( 1 ) == 1 );
    REQUIRE( r.segmentClasses.value( 3 ) == 2 );
    // Neighbors should follow labels
    REQUIRE( r.segmentClasses.value( 2 ) == 1 );
    REQUIRE( r.segmentClasses.value( 4 ) == 2 );
}
#endif
