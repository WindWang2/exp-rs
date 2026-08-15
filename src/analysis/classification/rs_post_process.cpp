// rs_post_process.cpp — Classification post-process pure operators.
#include "rs_post_process.h"

#include <gdal_alg.h>
#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogrsf_frmts.h>

#include <opencv2/imgproc.hpp>

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
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

void setErr( QString *err, const QString &msg )
{
  if ( err )
    *err = msg;
}

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

// Majority among values that border the component (outside pixels).
int borderNeighborMajority( const cv::Mat &labels, const cv::Mat &cc, int compId,
                            int connectedness )
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

  for ( int r = 0; r < H; ++r )
  {
    const int *ccRow = cc.ptr<int>( r );
    for ( int c = 0; c < W; ++c )
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
        ++freq[pixelAt( labels, nr, nc )];
      }
    }
  }

  if ( freq.empty() )
    return 0;

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

  // Stack-allocated frequency table to eliminate heap allocations per pixel
  struct FreqEntry
  {
    int val;
    int count;
  };
  std::array<FreqEntry, 64> freq;
  int numEntries = 0;

  for ( int rr = r0; rr <= r1; ++rr )
  {
    const int *row = labels.ptr<int>( rr );
    for ( int cc = c0; cc <= c1; ++cc )
    {
      const int v = row[cc];
      bool found = false;
      for ( int i = 0; i < numEntries; ++i )
      {
        if ( freq[i].val == v )
        {
          ++freq[i].count;
          found = true;
          break;
        }
      }
      if ( !found && numEntries < 64 )
      {
        freq[numEntries++] = { v, 1 };
      }
    }
  }

  const int centerVal = pixelAt( labels, r, c );
  int centerCnt = 0;
  for ( int i = 0; i < numEntries; ++i )
  {
    if ( freq[i].val == centerVal )
    {
      centerCnt = freq[i].count;
      break;
    }
  }

  int bestVal = centerVal;
  int bestCnt = centerCnt;
  for ( int i = 0; i < numEntries; ++i )
  {
    if ( freq[i].count > bestCnt )
    {
      bestCnt = freq[i].count;
      bestVal = freq[i].val;
    }
    else if ( freq[i].count == bestCnt && bestVal != centerVal && freq[i].val < bestVal )
    {
      bestVal = freq[i].val;
    }
  }
  return bestVal;
}

} // namespace

bool RsPostProcess::sieve( const cv::Mat &src, cv::Mat &dst, int threshold,
                           int connectedness, QString *err )
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

  // Process each distinct class independently: OpenCV CC is binary.
  // Collect unique class values via a small scan (class counts are small).
  std::vector<int> classIds;
  {
    std::unordered_map<int, char> seen;
    for ( int r = 0; r < work.rows; ++r )
    {
      const int *row = work.ptr<int>( r );
      for ( int c = 0; c < work.cols; ++c )
      {
        if ( seen.emplace( row[c], 1 ).second )
          classIds.push_back( row[c] );
      }
    }
  }

  // Record replacements against the original work image (single pass).
  // Build a destination copy and apply after computing all replacements so
  // neighbor majority is based on pre-sieve labels.
  cv::Mat out = work.clone();
  const cv::Mat orig = work;

  for ( int classId : classIds )
  {
    cv::Mat mask;
    cv::compare( orig, classId, mask, cv::CMP_EQ ); // CV_8U 0/255

    cv::Mat cc, stats, centroids;
    const int nComp = cv::connectedComponentsWithStats(
      mask, cc, stats, centroids, connectedness, CV_32S );
    // Component 0 is background (not this class).
    for ( int comp = 1; comp < nComp; ++comp )
    {
      const int area = stats.at<int>( comp, cv::CC_STAT_AREA );
      if ( area >= threshold )
        continue;
      const int replacement = borderNeighborMajority( orig, cc, comp, connectedness );
      for ( int r = 0; r < out.rows; ++r )
      {
        const int *ccRow = cc.ptr<int>( r );
        int *outRow = out.ptr<int>( r );
        for ( int c = 0; c < out.cols; ++c )
        {
          if ( ccRow[c] == comp )
            outRow[c] = replacement;
        }
      }
    }
  }

  dst = out;
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
                           QString *err )
{
  cv::Mat labels;
  if ( !toLabels32S( src, labels, err ) )
    return false;
  if ( !checkConnectedness( connectedness, err ) )
    return false;

  static constexpr int dr8[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  static constexpr int dc8[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  static constexpr int dr4[4] = { -1, 1, 0, 0 };
  static constexpr int dc4[4] = { 0, 0, -1, 1 };
  const int nDir = ( connectedness == 8 ) ? 8 : 4;
  const int *dr = ( connectedness == 8 ) ? dr8 : dr4;
  const int *dc = ( connectedness == 8 ) ? dc8 : dc4;

  const int H = labels.rows;
  const int W = labels.cols;
  cv::Mat out = cv::Mat::zeros( H, W, CV_32S );
  cv::Mat visited = cv::Mat::zeros( H, W, CV_8U );

  int nextId = 1;
  std::queue<std::pair<int, int>> q;

  for ( int r = 0; r < H; ++r )
  {
    for ( int c = 0; c < W; ++c )
    {
      if ( visited.at<uchar>( r, c ) )
        continue;
      const int classVal = labels.at<int>( r, c );
      const int cid = nextId++;
      visited.at<uchar>( r, c ) = 1;
      out.at<int>( r, c ) = cid;
      q.emplace( r, c );
      while ( !q.empty() )
      {
        const auto [cr, cc] = q.front();
        q.pop();
        for ( int d = 0; d < nDir; ++d )
        {
          const int nr = cr + dr[d];
          const int nc = cc + dc[d];
          if ( nr < 0 || nr >= H || nc < 0 || nc >= W )
            continue;
          if ( visited.at<uchar>( nr, nc ) )
            continue;
          if ( labels.at<int>( nr, nc ) != classVal )
            continue;
          visited.at<uchar>( nr, nc ) = 1;
          out.at<int>( nr, nc ) = cid;
          q.emplace( nr, nc );
        }
      }
    }
  }

  dst = out;
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

bool RsPostProcess::loadLabelRaster( const QString &path, cv::Mat &labels, double gt[6],
                                     QString &wkt, QString *err )
{
  if ( path.isEmpty() )
  {
    setErr( err, QStringLiteral( "Empty path" ) );
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

  // Remove existing file so Create succeeds.
  GDALDriver *existing = GetGDALDriverManager()->GetDriverByName(
    driverName.toUtf8().constData() );
  if ( QFileInfo::exists( path ) )
  {
    GDALDataset *old = static_cast<GDALDataset *>(
      GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
    if ( old )
    {
      GDALClose( old );
      if ( existing )
        existing->Delete( path.toUtf8().constData() );
    }
  }

  GDALDataset *ds = drv->Create( path.toUtf8().constData(), lab.cols, lab.rows, 1, gdt, papsz );
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
  return true;
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

  // Remove existing output if present.
  {
    GDALDataset *old = static_cast<GDALDataset *>(
      GDALOpenEx( vectorPath.toUtf8().constData(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr ) );
    if ( old )
    {
      GDALClose( old );
      drv->Delete( vectorPath.toUtf8().constData() );
    }
    else if ( QFileInfo::exists( vectorPath ) )
    {
      drv->Delete( vectorPath.toUtf8().constData() );
    }
  }

  GDALDataset *dst = drv->Create( vectorPath.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
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
    setErr( err, QStringLiteral( "Cannot create layer in %1" ).arg( vectorPath ) );
    return false;
  }

  OGRFieldDefn field( fieldName.toUtf8().constData(), OFTInteger );
  if ( layer->CreateField( &field ) != OGRERR_NONE )
  {
    GDALClose( dst );
    GDALClose( src );
    setErr( err, QStringLiteral( "Cannot create class field" ) );
    return false;
  }

  const int fieldIndex = layer->FindFieldIndex( fieldName.toUtf8().constData(), TRUE );
  const CPLErr perr = GDALPolygonize(
    band,
    /*mask*/ nullptr,
    OGRLayer::ToHandle( layer ),
    fieldIndex,
    /*options*/ nullptr,
    /*progress*/ nullptr,
    /*progressArg*/ nullptr );

  GDALClose( dst );
  GDALClose( src );

  if ( perr != CE_None )
  {
    setErr( err, QStringLiteral( "GDALPolygonize failed for %1" ).arg( labelRasterPath ) );
    return false;
  }
  return true;
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
  rootObj[QStringLiteral( "version" )] = 1;
  rootObj[QStringLiteral( "classes" )] = classesArray;

  QFile file( sidecarPath );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
  {
    setErr( err, QStringLiteral( "Failed to open sidecar metadata file for writing: %1" ).arg( sidecarPath ) );
    return false;
  }

  file.write( QJsonDocument( rootObj ).toJson( QJsonDocument::Indented ) );
  file.close();
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
  if ( !QFileInfo::exists( sidecarPath ) )
  {
    setErr( err, QStringLiteral( "Sidecar metadata file does not exist: %1" ).arg( sidecarPath ) );
    return false;
  }

  QFile file( sidecarPath );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    setErr( err, QStringLiteral( "Failed to open sidecar metadata file: %1" ).arg( sidecarPath ) );
    return false;
  }

  const QJsonDocument doc = QJsonDocument::fromJson( file.readAll() );
  file.close();

  if ( !doc.isObject() )
  {
    setErr( err, QStringLiteral( "Malformed JSON in sidecar metadata: %1" ).arg( sidecarPath ) );
    return false;
  }

  const QJsonObject rootObj = doc.object();
  if ( !rootObj.contains( QStringLiteral( "classes" ) ) || !rootObj[QStringLiteral( "classes" )].isArray() )
  {
    setErr( err, QStringLiteral( "Missing classes array in sidecar metadata: %1" ).arg( sidecarPath ) );
    return false;
  }

  outDefs.clear();
  const QJsonArray classesArray = rootObj[QStringLiteral( "classes" )].toArray();
  for ( const QJsonValue &val : classesArray )
  {
    if ( !val.isObject() )
      continue;
    const QJsonObject obj = val.toObject();
    const int id = obj[QStringLiteral( "id" )].toInt();
    const QString name = obj[QStringLiteral( "name" )].toString();
    const QColor color( obj[QStringLiteral( "color" )].toString( QStringLiteral( "#808080" ) ) );
    if ( id > 0 )
    {
      outDefs.insert( id, RsClassDef( id, name, color ) );
    }
  }
  return true;
}
