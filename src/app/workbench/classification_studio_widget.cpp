// src/app/workbench/classification_studio_widget.cpp — D15 Package G.
#include "classification_studio_widget.h"

#include "qgsrasterlayer.h"
#include "qgsrasterdataprovider.h"
#include "qgsrasterblock.h"

#include <QComboBox>
#include <QHeaderView>
#include <QImage>
#include <QPainter>
#include <QSlider>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <queue>
#include <vector>

namespace rs::app
{

// ---------------------------------------------------------------------------
// RsRoiMagicWandTool
// ---------------------------------------------------------------------------

RsRoiMagicWandTool::RsRoiMagicWandTool( QObject *parent )
    : QObject( parent )
{
}

namespace
{
  struct RasterPlane
  {
      int width = 0;
      int height = 0;
      std::vector<float> samples; // band-sequential planes
  };

  RasterPlane readLayerPlanes( const QgsRasterLayer *layer )
  {
    RasterPlane plane;
    if ( !layer || !layer->isValid() || !layer->dataProvider() )
      return plane;
    plane.width = layer->width();
    plane.height = layer->height();
    if ( plane.width <= 0 || plane.height <= 0 )
      return plane;
    const int bands = std::max( 1, layer->bandCount() );
    const QgsRectangle extent = layer->extent();
    plane.samples.resize( static_cast<size_t>( plane.width ) * plane.height * bands );
    // The seam takes a const layer (read-only contract); QGIS's provider
    // accessor is not const-marked, so the read path const_casts locally.
    QgsRasterDataProvider *provider = const_cast<QgsRasterLayer *>( layer )->dataProvider();
    if ( !provider )
      return plane;
    for ( int b = 0; b < bands; ++b )
    {
      std::unique_ptr<QgsRasterBlock> block( provider->block( b + 1, extent, plane.width, plane.height ) );
      if ( !block )
        return RasterPlane{};
      const size_t offset = static_cast<size_t>( b ) * plane.width * plane.height;
      for ( int y = 0; y < plane.height; ++y )
        for ( int x = 0; x < plane.width; ++x )
          plane.samples[offset + static_cast<size_t>( y ) * plane.width + x] = static_cast<float>( block->value( y, x ) );
    }
    return plane;
  }

  // Directed crack edges around the mask union; interior stays on the
  // left.  Vertices at integer lattice corners.  A saddle vertex carries
  // two outgoing edges — both are kept.
  struct Edge
  {
      int x1, y1, x2, y2;
  };

  std::vector<QPolygonF> maskOutlines( const std::vector<uint8_t> &mask, int width, int height )
  {
    const auto inside = [&]( int x, int y )
    {
      return x >= 0 && x < width && y >= 0 && y < height && mask[static_cast<size_t>( y ) * width + x] != 0;
    };
    std::map<std::pair<int64_t, int64_t>, std::vector<Edge>> edgeMap;
    const auto key = []( int x, int y )
    { return std::make_pair( static_cast<int64_t>( x ), static_cast<int64_t>( y ) ); };
    const auto addEdge = [&]( const Edge &e )
    { edgeMap[key( e.x1, e.y1 )].push_back( e ); };

    for ( int y = 0; y < height; ++y )
      for ( int x = 0; x < width; ++x )
      {
        if ( !inside( x, y ) )
          continue;
        // Screen coordinates: +x right, +y down.  Keeping the mask cell on
        // the LEFT of travel direction:
        if ( !inside( x, y - 1 ) )
          addEdge( { x, y, x + 1, y } );               // top: left->right
        if ( !inside( x + 1, y ) )
          addEdge( { x + 1, y, x + 1, y + 1 } );       // right: up->down
        if ( !inside( x, y + 1 ) )
          addEdge( { x + 1, y + 1, x, y + 1 } );       // bottom: right->left
        if ( !inside( x - 1, y ) )
          addEdge( { x, y + 1, x, y } );               // left: down->up
      }

    std::vector<QPolygonF> loops;
    while ( !edgeMap.empty() )
    {
      // Pick the first vertex that still owns an unconsumed edge; buckets
      // drained by the walk below are erased on the spot, so no empty
      // bucket can ever be dereferenced here.
      Edge first { 0, 0, 0, 0 };
      bool foundStart = false;
      for ( auto it = edgeMap.begin(); it != edgeMap.end() && !foundStart; )
      {
        if ( it->second.empty() )
          it = edgeMap.erase( it );
        else
        {
          first = it->second.front();
          it->second.erase( it->second.begin() );
          foundStart = true;
        }
      }
      if ( !foundStart )
        break;
      QPolygonF loop;
      loop.append( QPointF( first.x1, first.y1 ) );
      int cx = first.x2, cy = first.y2;
      int px = first.x1, py = first.y1;
      while ( ( cx != first.x1 || cy != first.y1 ) && !edgeMap.empty() )
      {
        loop.append( QPointF( cx, cy ) );
        // Prefer the sharpest right turn when a saddle vertex offers two
        // outgoing edges (keeps loops consistent through checker corners).
        auto bucketIt = edgeMap.find( key( cx, cy ) );
        if ( bucketIt == edgeMap.end() || bucketIt->second.empty() )
          break; // broken chain (should not happen for closed masks)
        Edge chosen = bucketIt->second.front();
        size_t chosenIndex = 0;
        int bestTurn = -1;
        const int inX = cx - px, inY = cy - py;
        for ( size_t i = 0; i < bucketIt->second.size(); ++i )
        {
          const Edge &e = bucketIt->second[i];
          const int outX = e.x2 - e.x1, outY = e.y2 - e.y1;
          // cross > 0: right turn in screen coords (y down).
          const int cross = inX * outY - inY * outX;
          const int turn = cross > 0 ? 2 : ( cross < 0 ? 0 : 1 );
          if ( turn > bestTurn )
          {
            bestTurn = turn;
            chosen = e;
            chosenIndex = i;
          }
        }
        bucketIt->second.erase( bucketIt->second.begin() + static_cast<std::ptrdiff_t>( chosenIndex ) );
        if ( bucketIt->second.empty() )
          edgeMap.erase( bucketIt );
        px = cx;
        py = cy;
        cx = chosen.x2;
        cy = chosen.y2;
      }
      loop.append( QPointF( first.x1, first.y1 ) ); // close
      if ( loop.size() >= 4 )
        loops.push_back( loop );
    }
    // Longest loop first: the outer boundary dominates.
    std::sort( loops.begin(), loops.end(), []( const QPolygonF &a, const QPolygonF &b )
               { return a.boundingRect().width() * a.boundingRect().height() > b.boundingRect().width() * b.boundingRect().height(); } );
    return loops;
  }
} // namespace

QPolygonF RsRoiMagicWandTool::extractRegion( const QPoint &seedPixel,
                                             const QgsRasterLayer *rasterLayer,
                                             double spectralTolerance,
                                             int maxPixels,
                                             int connectivity )
{
  QPolygonF polygon;
  const RasterPlane plane = readLayerPlanes( rasterLayer );
  if ( plane.samples.empty() )
    return polygon;
  const int w = plane.width, h = plane.height;
  const int bands = std::max( 1, static_cast<int>( plane.samples.size() ) / ( w * h ) );
  if ( seedPixel.x() < 0 || seedPixel.x() >= w || seedPixel.y() < 0 || seedPixel.y() >= h )
    return polygon;
  if ( maxPixels <= 0 || ( connectivity != 4 && connectivity != 8 ) )
    return polygon;

  const size_t planeSize = static_cast<size_t>( w ) * h;
  std::vector<float> seed( bands );
  for ( int b = 0; b < bands; ++b )
    seed[b] = plane.samples[static_cast<size_t>( b ) * planeSize + static_cast<size_t>( seedPixel.y() ) * w + seedPixel.x()];

  const double tol2 = spectralTolerance * spectralTolerance;
  std::vector<uint8_t> mask( planeSize, 0 );
  std::vector<uint8_t> visited( planeSize, 0 );
  std::queue<std::pair<int, int>> queue;
  const auto seedIdx = static_cast<size_t>( seedPixel.y() ) * w + seedPixel.x();
  visited[seedIdx] = 1;
  queue.push( { seedPixel.x(), seedPixel.y() } );
  int accepted = 0;

  while ( !queue.empty() && accepted < maxPixels )
  {
    const auto [cx, cy] = queue.front();
    queue.pop();
    const auto idx = static_cast<size_t>( cy ) * w + cx;
    mask[idx] = 1;
    ++accepted;

    static constexpr int kDx8[] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static constexpr int kDy8[] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    const int neighbors = ( connectivity == 4 ) ? 4 : 8;
    for ( int n = 0; n < neighbors; ++n )
    {
      const int nx = cx + kDx8[n], ny = cy + kDy8[n];
      if ( nx < 0 || nx >= w || ny < 0 || ny >= h )
        continue;
      const size_t nIdx = static_cast<size_t>( ny ) * w + nx;
      if ( visited[nIdx] )
        continue;
      double dist2 = 0.0;
      for ( int b = 0; b < bands; ++b )
      {
        const double d = static_cast<double>( plane.samples[static_cast<size_t>( b ) * planeSize + nIdx] ) - seed[b];
        dist2 += d * d;
      }
      dist2 /= bands;
      if ( dist2 <= tol2 )
      {
        visited[nIdx] = 1;
        queue.push( { nx, ny } );
      }
    }
  }

  const std::vector<QPolygonF> loops = maskOutlines( mask, w, h );
  if ( !loops.empty() )
    polygon = loops.front();
  return polygon;
}

// ---------------------------------------------------------------------------
// FeatureScatterWidget
// ---------------------------------------------------------------------------

namespace
{
  QColor densityColor( double t )
  {
    // 5-stop viridis-like LUT: dark violet -> blue -> teal -> green -> yellow.
    static const int stops[5][3] = {
      { 68, 1, 84 }, { 59, 82, 139 }, { 33, 145, 140 }, { 94, 201, 98 }, { 253, 231, 37 } };
    t = std::clamp( t, 0.0, 1.0 ) * 4.0;
    const int i = std::min( 3, static_cast<int>( t ) );
    const double f = t - i;
    const auto mix = [&]( int c )
    { return static_cast<int>( std::lround( stops[i][c] * ( 1.0 - f ) + stops[i + 1][c] * f ) ); };
    return QColor( mix( 0 ), mix( 1 ), mix( 2 ) );
  }
} // namespace

FeatureScatterWidget::FeatureScatterWidget( QWidget *parent )
    : QWidget( parent )
{
  setMinimumSize( 120, 90 );
}

void FeatureScatterWidget::setGridBinning( int binsX, int binsY )
{
  mBinsX = std::clamp( binsX, 1, 200 );
  mBinsY = std::clamp( binsY, 1, 200 );
  mDirty = true;
  update();
}

void FeatureScatterWidget::setData( std::span<const float> xFeatures,
                                    std::span<const float> yFeatures,
                                    std::span<const int> labels,
                                    const QString &xLabel,
                                    const QString &yLabel )
{
  const size_t n = std::min( xFeatures.size(), yFeatures.size() );
  mX.assign( xFeatures.begin(), xFeatures.begin() + n );
  mY.assign( yFeatures.begin(), yFeatures.begin() + n );
  mLabels.assign( labels.begin(), labels.begin() + std::min<size_t>( labels.size(), n ) );

  if ( !mX.empty() )
  {
    const auto [minItX, maxItX] = std::minmax_element( mX.begin(), mX.end() );
    const auto [minItY, maxItY] = std::minmax_element( mY.begin(), mY.end() );
    mXMin = *minItX;
    mXMax = *maxItX;
    mYMin = *minItY;
    mYMax = *maxItY;
    if ( mXMax <= mXMin )
      mXMax = mXMin + 1.0f;
    if ( mYMax <= mYMin )
      mYMax = mYMin + 1.0f;
  }
  ( void ) xLabel; // axis labels are drawn by the host's chrome, not the thumbnail
  ( void ) yLabel;
  mDirty = true;
  update();
}

void FeatureScatterWidget::rebuildImage() const
{
  if ( !mDirty && !mCache.isNull() )
    return;
  if ( mX.empty() )
  {
    mCache = QImage();
    mDirty = false;
    return;
  }
  std::vector<uint32_t> bins( static_cast<size_t>( mBinsX ) * mBinsY, 0 );
  const double sx = static_cast<double>( mBinsX ) / ( mXMax - mXMin );
  const double sy = static_cast<double>( mBinsY ) / ( mYMax - mYMin );
  uint32_t maxCount = 1;
  for ( size_t i = 0; i < mX.size(); ++i )
  {
    if ( !std::isfinite( mX[i] ) || !std::isfinite( mY[i] ) )
      continue; // NaN/inf never reach the float->int bin cast (UB guard)
    const int bx = std::clamp( static_cast<int>( ( mX[i] - mXMin ) * sx ), 0, mBinsX - 1 );
    const int by = std::clamp( static_cast<int>( ( mYMax - mY[i] ) * sy ), 0, mBinsY - 1 ); // y flipped (screen)
    const uint32_t c = ++bins[static_cast<size_t>( by ) * mBinsX + bx];
    maxCount = std::max( maxCount, c );
  }
  mCache = QImage( mBinsX, mBinsY, QImage::Format_RGB32 );
  const double logMax = std::log1p( static_cast<double>( maxCount ) );
  for ( int y = 0; y < mBinsY; ++y )
    for ( int x = 0; x < mBinsX; ++x )
    {
      const uint32_t c = bins[static_cast<size_t>( y ) * mBinsX + x];
      const double t = c == 0 ? 0.0 : std::log1p( static_cast<double>( c ) ) / logMax;
      mCache.setPixel( x, y, c == 0 ? QColor( 12, 12, 16 ).rgb() : densityColor( t ).rgb() );
    }
  mDirty = false;
}

QImage FeatureScatterWidget::renderDensityThumbnail() const
{
  rebuildImage();
  return mCache;
}

void FeatureScatterWidget::paintEvent( QPaintEvent *event )
{
  ( void ) event;
  rebuildImage();
  QPainter p( this );
  p.fillRect( rect(), QColor( 12, 12, 16 ) );
  if ( !mCache.isNull() )
    p.drawImage( rect(), mCache );
}

// ---------------------------------------------------------------------------
// ClassificationStudioWidget
// ---------------------------------------------------------------------------

ClassificationStudioWidget::ClassificationStudioWidget( QWidget *parent )
    : QWidget( parent )
    , mWand( new RsRoiMagicWandTool( this ) )
{
  auto *layout = new QVBoxLayout( this );
  mAlgoCombo = new QComboBox( this );
  mAlgoCombo->addItem( QStringLiteral( "Random Forest" ), static_cast<int>( 0 ) );
  mAlgoCombo->addItem( QStringLiteral( "SVM" ), static_cast<int>( 1 ) );
  mAlgoCombo->addItem( QStringLiteral( "KMeans" ), static_cast<int>( 2 ) );
  mAlgoCombo->addItem( QStringLiteral( "ISODATA" ), static_cast<int>( 3 ) );
  mAlgoCombo->addItem( QStringLiteral( "Normal Bayes" ), static_cast<int>( 4 ) );
  layout->addWidget( mAlgoCombo );

  mClassTable = new QTableWidget( 0, 3, this );
  mClassTable->setHorizontalHeaderLabels( { tr( "ID" ), tr( "Class" ), tr( "Color" ) } );
  mClassTable->horizontalHeader()->setStretchLastSection( true );
  layout->addWidget( mClassTable );

  mSwipeSlider = new QSlider( Qt::Horizontal, this );
  mSwipeSlider->setRange( 0, 100 );
  layout->addWidget( mSwipeSlider );

  connect( mSwipeSlider, &QSlider::valueChanged, this, [this]( int value )
           { emit swipeOffsetChanged( value / 100.0f ); } );
  connect( mAlgoCombo, &QComboBox::currentIndexChanged, this, [this]( int index )
           {
             if ( index >= 0 )
               emit classificationRequested( mAlgoCombo->itemData( index ).toInt() );
           } );
}

void ClassificationStudioWidget::bindInputLayer( QgsRasterLayer *layer )
{
  mLayer = layer;
}

void ClassificationStudioWidget::setMissionInputRef( const sicnu::app::WorkbenchObjectRef &ref )
{
  mMissionInput = ref;
}

void ClassificationStudioWidget::setMissionResultRef( const sicnu::app::WorkbenchObjectRef &ref )
{
  mMissionResult = ref;
}

void ClassificationStudioWidget::acceptProductPath( const QString &path, int algoType )
{
  if ( path.isEmpty() )
    return;
  const int algo = algoType >= 0 ? algoType
                                 : ( mAlgoCombo ? mAlgoCombo->itemData( mAlgoCombo->currentIndex() ).toInt()
                                                : -1 );
  emit classificationProductReady( path, algo );
}

void ClassificationStudioWidget::setClassPalette( const std::vector<int> &classIds,
                                                  const std::vector<QString> &names,
                                                  const std::vector<uint32_t> &argbColors )
{
  const size_t n = std::min( { classIds.size(), names.size(), argbColors.size() } );
  mClassTable->setRowCount( static_cast<int>( n ) );
  for ( size_t i = 0; i < n; ++i )
  {
    const auto row = static_cast<int>( i );
    mClassTable->setItem( row, 0, new QTableWidgetItem( QString::number( classIds[i] ) ) );
    mClassTable->setItem( row, 1, new QTableWidgetItem( names[i] ) );
    QTableWidgetItem *colorItem = new QTableWidgetItem();
    colorItem->setBackground( QColor( QColor::fromRgba( argbColors[i] ) ) );
    mClassTable->setItem( row, 2, colorItem );
    if ( row == 0 )
      mCurrentClassId = classIds[i];
  }
  mClassTable->selectRow( 0 );
  if ( mClassTable->currentRow() >= 0 && mClassTable->item( mClassTable->currentRow(), 0 ) )
    mCurrentClassId = mClassTable->item( mClassTable->currentRow(), 0 )->text().toInt();
}

void ClassificationStudioWidget::runMagicWandAt( const QPoint &seedPixel, double spectralTolerance )
{
  if ( mLayer.isNull() )
    return;
  const QPolygonF polygon = mWand->extractRegion( seedPixel, mLayer, spectralTolerance );
  if ( !polygon.isEmpty() )
    emit roiExtracted( mCurrentClassId, polygon );
}

} // namespace rs::app
