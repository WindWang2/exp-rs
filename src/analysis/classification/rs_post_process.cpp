// rs_post_process.cpp — Classification post-process pure operators.
#include "rs_post_process.h"

#include "platform/durable_sidecar.h"

#include <gdal_alg.h>
#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogrsf_frmts.h>


#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "geospatial/util/atomic_fs.h"

namespace
{

void setErr( QString *err, const QString &msg )
{
  if ( err )
    *err = msg;
}

// Single source of the <raster>.class.json schema version (R6: the reader
// now enforces what the writer stamps).
constexpr int kClassMetaDataSidecarVersion = 1;

bool toLabels32S( const cv::Mat &src, cv::Mat &labels, QString *err )
{
  if ( src.empty() )
  {
    setErr( err, QStringLiteral( "Empty label raster" ) );
    return false;
  }
  if ( src.channels() != 1 )
  {
    setErr( err, QStringLiteral( "Label raster must be single-channel" ) );
    return false;
  }
  if ( src.type() == CV_32S )
  {
    labels = src;
    return true;
  }
  if ( src.type() == CV_8U )
  {
    src.convertTo( labels, CV_32S );
    return true;
  }
  setErr( err, QStringLiteral( "Label raster must be CV_32S or CV_8U" ) );
  return false;
}

bool checkConnectedness( int connectedness, QString *err )
{
  if ( connectedness != 4 && connectedness != 8 )
  {
    setErr( err, QStringLiteral( "connectedness must be 4 or 8" ) );
    return false;
  }
  return true;
}

int pixelAt( const cv::Mat &m, int r, int c )
{
  return m.at<int>( r, c );
}

/// R30-T01 (WP08/WP09): one component of a same-label global labeling.
struct ComponentRecord
{
  int label = 0;        ///< the class value the component is made of
  int top = 0;
  int left = 0;
  int height = 0;
  int width = 0;
  qint64 area = 0;
  qint64 firstPixel = 0; ///< flat raster index of the component's first pixel
};

/// R30-T01 (WP08/WP09): global same-label connected-component labeling in
/// ONE pass. Every maximal set of same-value pixels connected under
/// `connectedness` gets an id (1..N) in raster-scan order of its first
/// pixel — deterministic across runs. `cc` (CV_32S, 0 = unassigned) is
/// filled with those ids; `records` describes each component.
///
/// Equivalence note (WP09): a PER-CLASS CC of the binary mask (labels==k)
/// yields exactly the restriction of this global labeling to class k, which
/// is what makes the sieve rewrite bit-identical to the per-class original.
/// Cancellation: polled every 16 seed rows and every 65536 flood steps.
bool labelSameValueComponents( const cv::Mat &labels,
                               int connectedness,
                               cv::Mat &cc,
                               std::vector<ComponentRecord> &records,
                               const std::function<bool()> &isCanceled,
                               QString *err )
{
  static constexpr int dr8[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  static constexpr int dc8[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  static constexpr int dr4[4] = { -1, 1, 0, 0 };
  static constexpr int dc4[4] = { 0, 0, -1, 1 };
  const int nDir = ( connectedness == 8 ) ? 8 : 4;
  const int *dr = ( connectedness == 8 ) ? dr8 : dr4;
  const int *dc = ( connectedness == 8 ) ? dc8 : dc4;

  const int H = labels.rows;
  const int W = labels.cols;
  cc = cv::Mat::zeros( H, W, CV_32S );
  records.clear();

  std::vector<qint64> stack; // flood frontier (flat indices); reused
  for ( int r = 0; r < H; ++r )
  {
    if ( isCanceled && ( r % 16 ) == 0 && isCanceled() )
    {
      setErr( err, QStringLiteral( "Cancelled" ) );
      return false;
    }
    const int *row = labels.ptr<int>( r );
    int *ccRow = cc.ptr<int>( r );
    for ( int c = 0; c < W; ++c )
    {
      if ( ccRow[c] != 0 )
        continue;
      const int classVal = row[c];
      const int cid = static_cast<int>( records.size() ) + 1;
      ComponentRecord record;
      record.label = classVal;
      record.firstPixel = static_cast<qint64>( r ) * W + c;
      record.area = 1;
      // Track the extremes independently; width from partial minima would
      // miss the case where a later pixel widens the bbox to the LEFT after
      // an earlier update already fixed the width.
      int minRow = r, maxRow = r, minCol = c, maxCol = c;
      ccRow[c] = cid;
      stack.clear();
      stack.push_back( record.firstPixel );
      qint64 steps = 0;
      while ( !stack.empty() )
      {
        const qint64 flat = stack.back();
        stack.pop_back();
        const int cr = static_cast<int>( flat / W );
        const int cx = static_cast<int>( flat % W );
        for ( int d = 0; d < nDir; ++d )
        {
          const int nr = cr + dr[d];
          const int nc = cx + dc[d];
          if ( nr < 0 || nr >= H || nc < 0 || nc >= W )
            continue;
          if ( cc.at<int>( nr, nc ) != 0 || labels.at<int>( nr, nc ) != classVal )
            continue;
          cc.at<int>( nr, nc ) = cid;
          ++record.area;
          minRow = std::min( minRow, nr );
          maxRow = std::max( maxRow, nr );
          minCol = std::min( minCol, nc );
          maxCol = std::max( maxCol, nc );
          stack.push_back( static_cast<qint64>( nr ) * W + nc );
        }
        if ( isCanceled && ( ++steps & 0xFFFF ) == 0 && isCanceled() )
        {
          setErr( err, QStringLiteral( "Cancelled" ) );
          return false;
        }
      }
      record.top = minRow;
      record.left = minCol;
      record.height = maxRow - minRow + 1;
      record.width = maxCol - minCol + 1;
      records.push_back( record );
    }
  }
  return true;
}

// Majority among values that border the component (outside pixels).
// R30-T01 (WP09): bbox passed explicitly — the caller holds a ComponentRecord
// (no per-class OpenCV stats matrix anymore).
int borderNeighborMajority( const cv::Mat &labels, const cv::Mat &cc, int compId,
                            int top, int left, int height, int width,
                            int connectedness, int fallbackVal = 0 )
{
  static constexpr int dr8[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  static constexpr int dc8[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  static constexpr int dr4[4] = { -1, 1, 0, 0 };
  static constexpr int dc4[4] = { 0, 0, -1, 1 };

  const int nDir = ( connectedness == 8 ) ? 8 : 4;
  const int *dr = ( connectedness == 8 ) ? dr8 : dr4;
  const int *dc = ( connectedness == 8 ) ? dc8 : dc4;

  const int H = labels.rows;
  const int W = labels.cols;

  std::unordered_map<int, int> freq;
  freq.reserve( 16 );

  for ( int r = top; r < top + height; ++r )
  {
    const int *ccRow = cc.ptr<int>( r );
    for ( int c = left; c < left + width; ++c )
    {
      if ( ccRow[c] != compId )
        continue;
      for ( int d = 0; d < nDir; ++d )
      {
        const int nr = r + dr[d];
        const int nc = c + dc[d];
        if ( nr < 0 || nr >= H || nc < 0 || nc >= W )
          continue;
        if ( cc.at<int>( nr, nc ) == compId )
          continue;
        // Label 0 is the segmenters' NoData and does not vote (modeOfWindow
        // discipline, #612/#700): letting it participate lets NoData eat
        // valid isolated patches surrounded mostly by background.
        const int vote = pixelAt( labels, nr, nc );
        if ( vote == 0 )
          continue;
        ++freq[vote];
      }
    }
  }

  if ( freq.empty() )
    return fallbackVal;

  int bestVal = 0;
  int bestCnt = -1;
  for ( const auto &kv : freq )
  {
    if ( kv.second > bestCnt || ( kv.second == bestCnt && kv.first < bestVal ) )
    {
      bestCnt = kv.second;
      bestVal = kv.first;
    }
  }
  return bestVal;
}

int modeOfWindow( const cv::Mat &labels, int r, int c, int k )
{
  const int H = labels.rows;
  const int W = labels.cols;
  const int half = k / 2;

  const int r0 = std::max( 0, r - half );
  const int r1 = std::min( H - 1, r + half );
  const int c0 = std::max( 0, c - half );
  const int c1 = std::min( W - 1, c + half );

  // Frequency table sized to the window (k*k cells can hold at most k*k
  // DISTINCT labels). The previous fixed cap of 64 silently dropped votes
  // for >=9x9 windows on many-class maps (#612), biasing the mode.
  // Label 0 is the segmenters' NoData and does not vote: letting it
  // participate could grow NoData into valid areas.
  struct FreqEntry
  {
    int val;
    int count;
  };
  const int maxEntries = k * k;
  std::vector<FreqEntry> freq;
  freq.reserve( static_cast<size_t>( maxEntries ) );

  for ( int rr = r0; rr <= r1; ++rr )
  {
    const int *row = labels.ptr<int>( rr );
    for ( int cc = c0; cc <= c1; ++cc )
    {
      const int v = row[cc];
      if ( v == 0 )
        continue;
      bool found = false;
      for ( FreqEntry &e : freq )
      {
        if ( e.val == v )
        {
          ++e.count;
          found = true;
          break;
        }
      }
      if ( !found )
      {
        freq.push_back( { v, 1 } );
      }
    }
  }

  if ( freq.empty() )
    return pixelAt( labels, r, c );  // only NoData around: keep the center

  // A NoData (label 0) CENTER stays NoData (#700): the filter must not grow
  // classes into NoData areas, it only relabels valid centers. Label 0 also
  // does not vote (see above), so this only leaves untouched pixels 0.
  const int center = pixelAt( labels, r, c );
  if ( center == 0 )
    return 0;

  int bestVal = center;
  int bestCnt = -1;
  for ( const FreqEntry &e : freq )
  {
    if ( e.count > bestCnt || ( e.count == bestCnt && e.val < bestVal ) )
    {
      bestCnt = e.count;
      bestVal = e.val;
    }
  }
  return bestVal;
}

} // namespace

bool RsPostProcess::sieve( const cv::Mat &src, cv::Mat &dst, int threshold,
                           int connectedness, QString *err,
                           const std::function<bool()> &isCanceled )
{
  cv::Mat labels;
  if ( !toLabels32S( src, labels, err ) )
    return false;
  if ( !checkConnectedness( connectedness, err ) )
    return false;
  if ( threshold < 1 )
  {
    dst = labels.clone();
    return true;
  }

  // Work on a contiguous CV_32S copy so we can rewrite small components.
  cv::Mat work = labels.clone();
  if ( !work.isContinuous() )
    work = work.clone();

  // R30-T01 (WP09): ONE global same-label labeling replaces the per-class
  // binary CC pass (K full-grid masks + K connectedComponentsWithStats runs
  // + a frozen full-image copy). Equivalence: the per-class CC of the mask
  // (orig==class) is exactly the restriction of this global labeling to
  // that class (both are the maximal same-label connected regions), so
  // component membership is identical — and membership, read from the
  // labeling done on the INPUT (frozen identity), is all the rewrite below
  // consumes. Sequential semantics preserved: components are processed in
  // the LEGACY order (class by first raster occurrence, components within a
  // class in raster order of their first pixel) and the majority still
  // reads the CURRENT work image (R10-A/AN-2).
  cv::Mat cc;
  std::vector<ComponentRecord> records;
  if ( !labelSameValueComponents( work, connectedness, cc, records, isCanceled, err ) )
    return false;

  // Legacy order: rank classes by first raster occurrence, components of a
  // class by their first pixel.
  std::unordered_map<int, qint64> classFirstPixel;
  for ( const ComponentRecord &record : records )
  {
    auto it = classFirstPixel.find( record.label );
    if ( it == classFirstPixel.end() )
      classFirstPixel.emplace( record.label, record.firstPixel );
    else
      it->second = std::min( it->second, record.firstPixel );
  }
  std::vector<int> order;
  order.reserve( records.size() );
  for ( qint64 i = 0; i < static_cast<qint64>( records.size() ); ++i )
    order.push_back( static_cast<int>( i ) );
  std::sort( order.begin(), order.end(), [&records, &classFirstPixel]( int a, int b ) {
    const qint64 rankA = classFirstPixel.at( records[static_cast<size_t>( a )].label );
    const qint64 rankB = classFirstPixel.at( records[static_cast<size_t>( b )].label );
    if ( rankA != rankB )
      return rankA < rankB;
    return records[static_cast<size_t>( a )].firstPixel
           < records[static_cast<size_t>( b )].firstPixel;
  } );

  int processed = 0;
  for ( const int recordIndex : order )
  {
    if ( isCanceled && ( ++processed % 32 == 0 ) && isCanceled() )
    {
      setErr( err, QStringLiteral( "Cancelled" ) );
      return false;
    }
    const ComponentRecord &record = records[static_cast<size_t>( recordIndex )];
    if ( record.area >= threshold )
      continue;
    const int compId = recordIndex + 1; // global ids are 1-based in seed order
    const int replacement = borderNeighborMajority(
      work, cc, compId, record.top, record.left, record.height, record.width,
      connectedness, record.label );
    for ( int r = record.top; r < record.top + record.height; ++r )
    {
      const int *ccRow = cc.ptr<int>( r );
      int *outRow = work.ptr<int>( r );
      for ( int c = record.left; c < record.left + record.width; ++c )
      {
        if ( ccRow[c] == compId )
          outRow[c] = replacement;
      }
    }
  }

  dst = work;
  return true;
}

bool RsPostProcess::majorityFilter( const cv::Mat &src, cv::Mat &dst, int kernelOdd,
                                    QString *err,
                                    const std::function<bool()> &isCanceled )
{
  cv::Mat labels;
  if ( !toLabels32S( src, labels, err ) )
    return false;
  if ( kernelOdd < 3 || ( kernelOdd % 2 ) == 0 )
  {
    setErr( err, QStringLiteral( "majority kernel must be odd and >= 3" ) );
    return false;
  }

  // Full-image path: O(H*W*k^2). Poll cancel every 16 rows so UI tasks can abort.
  cv::Mat out( labels.rows, labels.cols, CV_32S );
  for ( int r = 0; r < labels.rows; ++r )
  {
    if ( isCanceled && ( r % 16 ) == 0 && isCanceled() )
    {
      setErr( err, QStringLiteral( "Cancelled" ) );
      return false;
    }
    int *outRow = out.ptr<int>( r );
    for ( int c = 0; c < labels.cols; ++c )
      outRow[c] = modeOfWindow( labels, r, c, kernelOdd );
  }
  dst = out;
  return true;
}

bool RsPostProcess::clump( const cv::Mat &src, cv::Mat &dst, int connectedness,
                           QString *err, const std::function<bool()> &isCanceled )
{
  // R30-T01 (WP08): worst case (checkerboard) every second pixel starts its
  // own component. When that bound cannot fit the int32 id range, refuse
  // typed — a wrapped id would silently MERGE components on the map. The
  // check reads only the header (runs before any pixel access/conversion).
  const qint64 worstCaseComponents =
    ( static_cast<qint64>( src.rows ) * src.cols + 1 ) / 2;
  if ( worstCaseComponents > static_cast<qint64>( std::numeric_limits<int>::max() ) - 1 )
  {
    setErr( err, QStringLiteral( "clump refuses a %1x%2 raster: its worst-case "
                                 "component count (%3) exceeds the int32 id range %4" )
                      .arg( src.rows )
                      .arg( src.cols )
                      .arg( worstCaseComponents )
                      .arg( std::numeric_limits<int>::max() - 1 ) );
    return false;
  }

  cv::Mat labels;
  if ( !toLabels32S( src, labels, err ) )
    return false;
  if ( !checkConnectedness( connectedness, err ) )
    return false;

  cv::Mat cc;
  std::vector<ComponentRecord> records;
  if ( !labelSameValueComponents( labels, connectedness, cc, records, isCanceled, err ) )
    return false;

  // Component ids are exactly the cc values (1-based, raster seed order) —
  // the deterministic clump contract.
  dst = cc;
  return true;
}

bool RsPostProcess::recode( const cv::Mat &src, cv::Mat &dst, const QMap<int, int> &map,
                            QString *err )
{
  cv::Mat labels;
  if ( !toLabels32S( src, labels, err ) )
    return false;

  cv::Mat out( labels.rows, labels.cols, CV_32S );
  for ( int r = 0; r < labels.rows; ++r )
  {
    const int *inRow = labels.ptr<int>( r );
    int *outRow = out.ptr<int>( r );
    for ( int c = 0; c < labels.cols; ++c )
    {
      const int v = inRow[c];
      outRow[c] = map.value( v, v );
    }
  }
  dst = out;
  return true;
}

bool RsPostProcess::loadLabelRaster( const QString &path, cv::Mat &labels,
                                     double gt[6], QString &wkt, QString *err )
{
  return loadLabelRaster( path, labels, gt, wkt, nullptr, err );
}

bool RsPostProcess::loadLabelRaster( const QString &path, cv::Mat &labels,
                                     double gt[6], QString &wkt,
                                     double *nodataValue, QString *err )
{
  if ( path.isEmpty() )
  {
    setErr( err, QStringLiteral( "Empty label raster path" ) );
    return false;
  }

  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
  {
    setErr( err, QStringLiteral( "Cannot open label raster: %1" ).arg( path ) );
    return false;
  }
  if ( ds->GetRasterCount() < 1 )
  {
    GDALClose( ds );
    setErr( err, QStringLiteral( "Raster has no bands: %1" ).arg( path ) );
    return false;
  }

  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  if ( W <= 0 || H <= 0 )
  {
    GDALClose( ds );
    setErr( err, QStringLiteral( "Invalid raster size" ) );
    return false;
  }

  if ( ds->GetGeoTransform( gt ) != CE_None )
  {
    // Identity geotransform fallback
    gt[0] = 0;
    gt[1] = 1;
    gt[2] = 0;
    gt[3] = 0;
    gt[4] = 0;
    gt[5] = 1;
  }
  const char *proj = ds->GetProjectionRef();
  wkt = proj ? QString::fromUtf8( proj ) : QString();

  if ( nodataValue )
  {
    int hasNodata = 0;
    const double nd = ds->GetRasterBand( 1 )->GetNoDataValue( &hasNodata );
    *nodataValue = hasNodata ? nd : std::numeric_limits<double>::quiet_NaN();
  }

  labels.create( H, W, CV_32S );
  const CPLErr rio = ds->GetRasterBand( 1 )->RasterIO(
    GF_Read, 0, 0, W, H, labels.ptr<int>( 0 ), W, H, GDT_Int32,
    0, static_cast<GSpacing>( labels.step[0] ) );
  GDALClose( ds );
  if ( rio != CE_None )
  {
    setErr( err, QStringLiteral( "Failed to read label band: %1" ).arg( path ) );
    labels.release();
    return false;
  }
  return true;
}

bool RsPostProcess::saveLabelRaster( const QString &path, const cv::Mat &labels,
                                     const double gt[6], const QString &wkt,
                                     const QVector<QRgb> &colorTable,
                                     const QStringList &creationOptions,
                                     double nodataValue,
                                     QString *err )
{
  cv::Mat lab;
  if ( !toLabels32S( labels, lab, err ) )
    return false;
  if ( path.isEmpty() )
  {
    setErr( err, QStringLiteral( "Empty output path" ) );
    return false;
  }

  GDALAllRegister();

  // Prefer GTiff; allow driver inference from extension for GPKG raster etc.
  QString driverName = QStringLiteral( "GTiff" );
  const QString ext = QFileInfo( path ).suffix().toLower();
  if ( ext == QLatin1String( "img" ) )
    driverName = QStringLiteral( "HFA" );
  else if ( ext == QLatin1String( "gpkg" ) )
    driverName = QStringLiteral( "GPKG" );

  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName(
    driverName.toUtf8().constData() );
  if ( !drv )
  {
    setErr( err, QStringLiteral( "GDAL driver not available: %1" ).arg( driverName ) );
    return false;
  }

  // Dtype policy (ADR 0019 S4): Byte when all labels fit 0..255, UInt16 up
  // to 65535, Int32 beyond — labels are never silently clamped. Negative
  // labels (e.g. unset values) force Int32.
  double minV = 0, maxV = 0;
  cv::minMaxLoc( lab, &minV, &maxV );
  GDALDataType gdt = GDT_Byte;
  if ( minV < 0.0 || maxV > 65535.0 )
    gdt = GDT_Int32;
  else if ( maxV > 255.0 )
    gdt = GDT_UInt16;

  char **papsz = nullptr;
  for ( const QString &opt : creationOptions )
    papsz = CSLAddString( papsz, opt.toUtf8().constData() );

  // Write to a sibling temp file first and rename over the target on success,
  // so a failed run (disk full, permissions, bad creation options) never
  // destroys the previous output (#285). GDAL closes the dataset before the
  // rename, so the target is only replaced by a fully written file.
  const QString tmpPath = path + QLatin1String( ".tmp~" );
  GDALDataset *ds = drv->Create( tmpPath.toUtf8().constData(), lab.cols, lab.rows, 1, gdt, papsz );
  CSLDestroy( papsz );
  if ( !ds )
  {
    setErr( err, QStringLiteral( "Cannot create output: %1" ).arg( path ) );
    return false;
  }

  ds->SetGeoTransform( const_cast<double *>( gt ) );
  if ( !wkt.isEmpty() )
    ds->SetProjection( wkt.toUtf8().constData() );

  CPLErr rio = CE_Failure;
  if ( gdt == GDT_Byte )
  {
    cv::Mat u8;
    lab.convertTo( u8, CV_8U );
    rio = ds->GetRasterBand( 1 )->RasterIO(
      GF_Write, 0, 0, u8.cols, u8.rows, u8.ptr<uchar>( 0 ), u8.cols, u8.rows, GDT_Byte,
      0, static_cast<GSpacing>( u8.step[0] ) );
  }
  else if ( gdt == GDT_UInt16 )
  {
    cv::Mat u16;
    lab.convertTo( u16, CV_16U );
    rio = ds->GetRasterBand( 1 )->RasterIO(
      GF_Write, 0, 0, u16.cols, u16.rows, u16.ptr<ushort>( 0 ),
      u16.cols, u16.rows, GDT_UInt16, 0, static_cast<GSpacing>( u16.step[0] ) );
  }
  else
  {
    rio = ds->GetRasterBand( 1 )->RasterIO(
      GF_Write, 0, 0, lab.cols, lab.rows, lab.ptr<int>( 0 ), lab.cols, lab.rows, GDT_Int32,
      0, static_cast<GSpacing>( lab.step[0] ) );
  }

  if ( rio != CE_None )
  {
    GDALClose( ds );
    QFile::remove( tmpPath );
    setErr( err, QStringLiteral( "Failed to write label band: %1" ).arg( path ) );
    return false;
  }

  if ( gdt == GDT_Byte && !colorTable.isEmpty() )
  {
    GDALColorTable ct( GPI_RGB );
    for ( int i = 0; i < colorTable.size(); ++i )
    {
      const QRgb rgb = colorTable.at( i );
      GDALColorEntry e;
      e.c1 = static_cast<short>( qRed( rgb ) );
      e.c2 = static_cast<short>( qGreen( rgb ) );
      e.c3 = static_cast<short>( qBlue( rgb ) );
      e.c4 = static_cast<short>( qAlpha( rgb ) );
      ct.SetColorEntry( i, &e );
    }
    ds->GetRasterBand( 1 )->SetColorTable( &ct );
    ds->GetRasterBand( 1 )->SetColorInterpretation( GCI_PaletteIndex );
  }

  if ( !std::isnan( nodataValue ) )
    ds->GetRasterBand( 1 )->SetNoDataValue( nodataValue );

  GDALClose( ds );

  // Durability gate (atomic_fs.h contract): flush the staged bytes before
  // either rename mechanism commits the directory entry.
  try
  {
    sicnu::geo::atomic_fs::fsyncFile( tmpPath.toStdString() );
  }
  catch ( const sicnu::geo::GeoError &ex )
  {
    // Same cleanup discipline as the publish-failure path below: a failed
    // gate must not strand the staged raster next to the output.
    drv->Delete( tmpPath.toUtf8().constData() );
    setErr( err, QStringLiteral( "Failed to flush the staged output: %1 (%2)" )
                   .arg( path, QString::fromUtf8( ex.what() ) ) );
    return false;
  }

  // Rename the fully written temp file over the target. GTiff/HFA/GPKG all
  // support GDALDriver::Rename (POSIX rename when driverCreateCopyFileSet is
  // unset); fall back to atomic_fs publish (ReplaceFileW / MoveFileExW) so a
  // Windows sharing-violation cannot delete-then-fail (#1178 / #285).
  if ( drv->Rename( path.toUtf8().constData(), tmpPath.toUtf8().constData() ) == CE_None )
    return true;
  try
  {
    sicnu::geo::atomic_fs::publishStagedFile( tmpPath.toStdString(), path.toStdString() );
    return true;
  }
  catch ( const sicnu::geo::GeoError &ex )
  {
    // Same cleanup discipline as the group publish below: a failed publish
    // must not strand the staged raster.
    drv->Delete( tmpPath.toUtf8().constData() );
    setErr( err, QStringLiteral( "Failed to move output into place: %1 (%2)" )
                   .arg( path, QString::fromUtf8( ex.what() ) ) );
    return false;
  }
}

bool RsPostProcess::polygonize( const QString &labelRasterPath, const QString &vectorPath,
                                const QString &classField, QString *err )
{
  if ( labelRasterPath.isEmpty() || vectorPath.isEmpty() )
  {
    setErr( err, QStringLiteral( "Empty polygonize path" ) );
    return false;
  }

  GDALAllRegister();
  OGRRegisterAll();

  GDALDataset *src = static_cast<GDALDataset *>(
    GDALOpen( labelRasterPath.toUtf8().constData(), GA_ReadOnly ) );
  if ( !src )
  {
    setErr( err, QStringLiteral( "Cannot open label raster: %1" ).arg( labelRasterPath ) );
    return false;
  }
  GDALRasterBand *band = src->GetRasterBand( 1 );
  if ( !band )
  {
    GDALClose( src );
    setErr( err, QStringLiteral( "No band in label raster" ) );
    return false;
  }

  const QString ext = QFileInfo( vectorPath ).suffix().toLower();
  const char *driverName = ( ext == QLatin1String( "gpkg" ) ) ? "GPKG" : "ESRI Shapefile";
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( driverName );
  if ( !drv )
  {
    GDALClose( src );
    setErr( err, QStringLiteral( "Vector driver not available: %1" )
                   .arg( QString::fromUtf8( driverName ) ) );
    return false;
  }

  // Write to a sibling temp dataset first and rename over the target on
  // success, so a failed polygonize never destroys the previous output (#285).
  // Preserve extension (e.g. .tmp~.shp / .tmp~.gpkg) so OGR drivers treat
  // the path as a standard dataset file rather than creating a directory (#404).
  const QFileInfo fi( vectorPath );
  const QString tmpPath = fi.absolutePath() + QLatin1Char( '/' ) + fi.completeBaseName()
                          + QStringLiteral( ".tmp~." ) + fi.suffix();
  GDALDataset *dst = drv->Create( tmpPath.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
  if ( !dst )
  {
    GDALClose( src );
    setErr( err, QStringLiteral( "Cannot create vector: %1" ).arg( vectorPath ) );
    return false;
  }

  OGRSpatialReference srs;
  const char *proj = src->GetProjectionRef();
  if ( proj && *proj )
    srs.importFromWkt( proj );

  const QString fieldName = classField.isEmpty() ? QStringLiteral( "class_id" ) : classField;
  OGRLayer *layer = dst->CreateLayer(
    "polygons",
    ( proj && *proj ) ? &srs : nullptr,
    wkbPolygon,
    nullptr );
  if ( !layer )
  {
    GDALClose( dst );
    GDALClose( src );
    QFile::remove( tmpPath );
    setErr( err, QStringLiteral( "Cannot create layer in %1" ).arg( vectorPath ) );
    return false;
  }

  OGRFieldDefn field( fieldName.toUtf8().constData(), OFTInteger );
  if ( layer->CreateField( &field ) != OGRERR_NONE )
  {
    GDALClose( dst );
    GDALClose( src );
    QFile::remove( tmpPath );
    setErr( err, QStringLiteral( "Cannot create class field" ) );
    return false;
  }

  const int fieldIndex = layer->FindFieldIndex( fieldName.toUtf8().constData(), TRUE );
  // 8-connected polygonization, matching RsClassRaster::polygonize and the
  // segmenter's 8-connected object definition: a label whose pixels touch
  // only diagonally is one object and must export as one polygon. GDAL's
  // default 4-connectivity split such objects in two (R26-S8).
  char *polygonizeOptions[2] = { const_cast<char *>( "8CONNECTED=8" ), nullptr };
  const CPLErr perr = GDALPolygonize(
    band,
    /*mask*/ nullptr,
    OGRLayer::ToHandle( layer ),
    fieldIndex,
    polygonizeOptions,
    /*progress*/ nullptr,
    /*progressArg*/ nullptr );

  GDALClose( dst );
  GDALClose( src );

  if ( perr != CE_None )
  {
    QFile::remove( tmpPath );
    setErr( err, QStringLiteral( "GDALPolygonize failed for %1" ).arg( labelRasterPath ) );
    return false;
  }

  // #1174: atomic_fs group publish (sidecars FIRST, main LAST, .bak rollback).
  // Never drv->Delete the previous output before publish — that destroyed the
  // last good group when the subsequent rename failed.
  try
  {
    sicnu::geo::atomic_fs::publishStagedGroup( tmpPath.toStdString(), vectorPath.toStdString() );
    return true;
  }
  catch ( const sicnu::geo::GeoError &ex )
  {
    drv->Delete( tmpPath.toUtf8().constData() );
    setErr( err, QStringLiteral( "Failed to move vector output into place: %1 (%2)" )
                   .arg( vectorPath, QString::fromUtf8( ex.what() ) ) );
    return false;
  }
}

bool RsPostProcess::saveClassMetaData( const QString &rasterPath, const QHash<int, RsClassDef> &defs, QString *err )
{
  if ( rasterPath.isEmpty() )
  {
    setErr( err, QStringLiteral( "Empty raster path" ) );
    return false;
  }

  const QString sidecarPath = rasterPath.endsWith( QStringLiteral( ".class.json" ) ) ? rasterPath : rasterPath + QStringLiteral( ".class.json" );
  QJsonArray classesArray;
  QList<int> ids = defs.keys();
  std::sort( ids.begin(), ids.end() );

  for ( int id : ids )
  {
    const RsClassDef d = defs.value( id );
    QJsonObject obj;
    obj[QStringLiteral( "id" )] = d.id();
    obj[QStringLiteral( "name" )] = d.name();
    obj[QStringLiteral( "color" )] = d.color().name();
    classesArray.append( obj );
  }

  QJsonObject rootObj;
  rootObj[QStringLiteral( "version" )] = kClassMetaDataSidecarVersion;
  rootObj[QStringLiteral( "classes" )] = classesArray;

  // R6: the sidecar write authority (temp + fsync + atomic publish +
  // .last-good rotation) — the old plain open+truncate write destroyed the
  // metadata for a finished classification result on any kill mid-write.
  const QByteArray bytes = QJsonDocument( rootObj ).toJson( QJsonDocument::Indented );

  const sicnu::platform::sidecar::WriteResult result = sicnu::platform::sidecar::write(
    { sidecarPath.toUtf8().constData(),
      std::string( bytes.constData(), static_cast<std::size_t>( bytes.size() ) ),
      sicnu::platform::sidecar::kDefaultLastGoodSuffix } );
  if ( !result )
  {
    setErr( err, QStringLiteral( "Failed to persist sidecar metadata %1: %2 (%3)" )
                   .arg( sidecarPath,
                         QString::fromLatin1(
                           sicnu::platform::sidecar::writeStatusName( result.status ) ),
                         QString::fromUtf8( result.error.c_str() ) ) );
    return false;
  }
  return true;
}

bool RsPostProcess::loadClassMetaData( const QString &rasterPath, QHash<int, RsClassDef> &outDefs, QString *err )
{
  if ( rasterPath.isEmpty() )
  {
    setErr( err, QStringLiteral( "Empty raster path" ) );
    return false;
  }

  const QString sidecarPath = rasterPath.endsWith( QStringLiteral( ".class.json" ) ) ? rasterPath : rasterPath + QStringLiteral( ".class.json" );

  // R6: resolve main -> .last-good through the sidecar read authority, so a
  // torn/lost main (previously a silent skip) recovers from the snapshot the
  // authority rotated on the last successful save.
  const sicnu::platform::sidecar::ReadResult read = sicnu::platform::sidecar::read(
    sidecarPath.toUtf8().constData(), sicnu::platform::sidecar::kDefaultLastGoodSuffix );
  if ( read.source == sicnu::platform::sidecar::ReadSource::Missing )
  {
    setErr( err, QStringLiteral( "Sidecar metadata file does not exist: %1" ).arg( sidecarPath ) );
    return false;
  }
  if ( read.source == sicnu::platform::sidecar::ReadSource::Corrupt )
  {
    setErr( err, QStringLiteral( "Sidecar metadata unreadable at %1 (%2)" )
                   .arg( sidecarPath, QString::fromUtf8( read.error.c_str() ) ) );
    return false;
  }

  QJsonDocument doc = QJsonDocument::fromJson(
    QByteArray( read.bytes.data(), static_cast<int>( read.bytes.size() ) ) );

  if ( !doc.isObject()
       && read.source == sicnu::platform::sidecar::ReadSource::Main )
  {
    // The main READ fine (I/O level) but does not decode — e.g. a torn main
    // that stayed JSON-prefixed. The last-good copy the authority rotated
    // after the previous successful save is the recovery channel; consult
    // it before giving up.
    const std::string lastGoodPath = std::string( sidecarPath.toUtf8().constData() ) +
                                    sicnu::platform::sidecar::kDefaultLastGoodSuffix;
    const sicnu::platform::sidecar::ReadResult lastGood =
      sicnu::platform::sidecar::read( lastGoodPath, "" );
    if ( lastGood.source == sicnu::platform::sidecar::ReadSource::Main )
      doc = QJsonDocument::fromJson(
        QByteArray( lastGood.bytes.data(),
                    static_cast<int>( lastGood.bytes.size() ) ) );
  }

  if ( !doc.isObject() )
  {
    setErr( err, QStringLiteral( "Malformed JSON in sidecar metadata: %1" ).arg( sidecarPath ) );
    return false;
  }

  const QJsonObject rootObj = doc.object();

  // Version gate (R6): the writer always stamped version:1; the reader never
  // checked. Pre-versioning legacy files (field absent) load as version 1;
  // anything else is a newer/foreign schema and is refused fail-closed.
  const QJsonValue versionValue = rootObj.value( QStringLiteral( "version" ) );
  if ( !versionValue.isUndefined() &&
       ( !versionValue.isDouble() || versionValue.toInt() != kClassMetaDataSidecarVersion ) )
  {
    setErr( err, QStringLiteral( "Unsupported sidecar metadata version %1 at %2 (supported: %3)" )
                   .arg( versionValue.toVariant().toString() )
                   .arg( sidecarPath )
                   .arg( kClassMetaDataSidecarVersion ) );
    return false;
  }
  if ( !rootObj.contains( QStringLiteral( "classes" ) ) || !rootObj[QStringLiteral( "classes" )].isArray() )
  {
    setErr( err, QStringLiteral( "Missing classes array in sidecar metadata: %1" ).arg( sidecarPath ) );
    return false;
  }

  outDefs.clear();
  const QJsonArray classesArray = rootObj[QStringLiteral( "classes" )].toArray();
  for ( const auto &val : classesArray )
  {
    if ( !val.isObject() )
      continue;
    const QJsonObject obj = val.toObject();
    const int id = obj[QStringLiteral( "id" )].toInt();
    const QString name = obj[QStringLiteral( "name" )].toString();
    const QColor color( obj[QStringLiteral( "color" )].toString( QStringLiteral( "#808080" ) ) );
    if ( id >= 0 )
    {
      outDefs.insert( id, RsClassDef( id, name, color ) );
    }
  }
  return true;
}
