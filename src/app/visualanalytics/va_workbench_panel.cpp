/***************************************************************************
 * va_workbench_panel.cpp — Visual Analytics workbench surface
 ***************************************************************************/
#include "va_workbench_panel.h"

#include "va_chart_widget.h"
#include "va_selection_hub.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <QUuid>

#include <algorithm>
#include <cmath>
#include <atomic>

#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgsexception.h>
#include <qgsmapcanvas.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvertexmarker.h>

#include "geospatial/raster/raster_reader.h"

namespace sicnu::app::va
{

namespace
{
/// Bounded sampling contract for every job in this panel.
constexpr int kThumbSize = 512;         // histogram/profile source window
constexpr int kScatterThumb = 256;      // scatter source window
constexpr int kMaxScatterPoints = 4096; // payload cap
constexpr int kHistogramBins = 64;

/// This surface's hub origin token PREFIX, made unique per instance: two
/// panels must never consume (or be echo-suppressed against) each other.
constexpr const char *kOrigin = "va.panel#";
std::atomic<int> s_instanceCounter { 0 };

/// Runs on the pool thread: opens through the geospatial contract and reads
/// a Nearest-overview thumbnail of @p bands sized to fit @p size (the SAME
/// selectOverview + readWindowResampled idiom AssetPreviewService uses).
/// Throws typed GeoError on open/read failure — VaDataSource turns that
/// into `failed`.
std::vector<double> readThumbnail( sicnu::geo::RasterReader &reader,
                                   const std::vector<int> &bands,
                                   int size, int *outWidth, int *outHeight )
{
    const auto &meta = reader.metadata();
    const double scale = std::min(
        1.0, std::min( static_cast<double>( size ) / std::max( 1, meta.width ),
                       static_cast<double>( size ) / std::max( 1, meta.height ) ) );
    const int w = std::max( 1, static_cast<int>( meta.width * scale ) );
    const int h = std::max( 1, static_cast<int>( meta.height * scale ) );
    if ( w <= 0 || h <= 0 )
        throw std::runtime_error( "raster has a degenerate shape" );
    const sicnu::geo::RasterWindow window{ 0, 0, meta.width, meta.height };
    const int level = reader.selectOverview( 1, w, h, sicnu::geo::OverviewPolicy::Nearest );
    *outWidth = w;
    *outHeight = h;
    return reader.readWindowResampled( bands, window, w, h, level,
                                       sicnu::geo::OverviewPolicy::Nearest, QStringLiteral( "nearest" ).toStdString() );
}

bool isNoData( double v, const sicnu::geo::BandInfo &band )
{
    if ( !std::isfinite( v ) )
        return !band.noDataIsNaN; // NaN payload without the NaN nodata flag is still invalid
    if ( band.noDataIsNaN )
        return false;
    return band.hasNoData && std::abs( v - band.noDataValue ) <= 1e-9;
}

} // namespace

VaWorkbenchPanel::VaWorkbenchPanel( RasterPathProvider provider,
                                    VaSelectionHub *hub,
                                    CanvasProvider canvasProvider,
                                    RasterLayerProvider rasterLayerProvider,
                                    QWidget *parent )
    : QgsDockWidget( parent )
    , m_provider( std::move( provider ) )
    , m_canvasProvider( std::move( canvasProvider ) )
    , m_rasterLayerProvider( std::move( rasterLayerProvider ) )
    , m_hub( hub )
    , m_origin( QString::fromUtf8( kOrigin )
                + QString::number( s_instanceCounter.fetch_add( 1 ) + 1 ) )
    , m_probe( [this]() -> QgsRasterLayer * {
        return m_rasterLayerProvider ? m_rasterLayerProvider() : nullptr;
    } )
{
    setObjectName( QStringLiteral( "rsVaWorkbenchDock" ) );
    setWindowTitle( tr( "可视化分析" ) );
    buildUi();
}

void VaWorkbenchPanel::buildUi()
{
    auto *central = new QWidget( this );
    auto *layout = new QVBoxLayout( central );
    layout->setContentsMargins( 8, 8, 8, 8 );

    auto *controls = new QHBoxLayout;
    m_bandA = new QComboBox( central );
    m_bandA->setObjectName( QStringLiteral( "rsVaBandA" ) );
    m_bandA->setAccessibleName( tr( "波段 A" ) );
    m_bandB = new QComboBox( central );
    m_bandB->setObjectName( QStringLiteral( "rsVaBandB" ) );
    m_bandB->setAccessibleName( tr( "波段 B" ) );
    m_refreshBtn = new QPushButton( tr( "刷新图表" ), central );
    m_refreshBtn->setObjectName( QStringLiteral( "rsVaRefresh" ) );
    m_refreshBtn->setAccessibleName( tr( "刷新图表" ) );
    controls->addWidget( new QLabel( tr( "波段 A：" ), central ) );
    controls->addWidget( m_bandA );
    controls->addWidget( new QLabel( tr( "波段 B：" ), central ) );
    controls->addWidget( m_bandB );
    controls->addWidget( m_refreshBtn );
    controls->addStretch( 1 );
    layout->addLayout( controls );

    m_statusLabel = new QLabel( tr( "选择一个栅格图层后刷新。" ), central );
    m_statusLabel->setObjectName( QStringLiteral( "rsVaStatus" ) );
    m_statusLabel->setWordWrap( true );
    layout->addWidget( m_statusLabel );

    m_cursorLabel = new QLabel( tr( "光标：—" ), central );
    m_cursorLabel->setObjectName( QStringLiteral( "rsVaCursor" ) );
    m_cursorLabel->setWordWrap( true );
    layout->addWidget( m_cursorLabel );

    m_histogramChart = new VaChartWidget( central );
    m_histogramChart->setObjectName( QStringLiteral( "rsVaHistogram" ) );
    m_scatterChart = new VaChartWidget( central );
    m_scatterChart->setObjectName( QStringLiteral( "rsVaScatter" ) );
    m_profileChart = new VaChartWidget( central );
    m_profileChart->setObjectName( QStringLiteral( "rsVaProfile" ) );
    layout->addWidget( m_histogramChart, 3 );
    layout->addWidget( m_profileChart, 2 );
    layout->addWidget( m_scatterChart, 3 );

    connect( m_refreshBtn, &QPushButton::clicked, this, &VaWorkbenchPanel::refreshCharts );
    connect( m_bandA, &QComboBox::currentIndexChanged, this,
             [this]( int ) { requestHistogram(); requestScatter(); } );
    connect( m_bandB, &QComboBox::currentIndexChanged, this,
             [this]( int ) { requestScatter(); } );

    // ── Linked brushing (11.0): charts publish typed events; the panel
    // applies them locally AND routes them through the hub for everyone else.
    connect( m_histogramChart, &VaChartWidget::rangeSelected, this,
             [this]( double x0, double x1 ) {
                 applyScatterRangeFilter( x0, x1, m_histogramChart->objectName() );
                 VaSelectionSubject subject;
                 subject.kind = VaSelectionKind::ChartRange;
                 subject.chartId = m_histogramChart->objectName();
                 subject.x0 = x0;
                 subject.x1 = x1;
                 publishToHub( subject );
             } );
    connect( m_scatterChart, &VaChartWidget::pointSelected, this,
             [this]( int index ) {
                 VaSelectionSubject subject;
                 subject.kind = VaSelectionKind::ChartPoint;
                 subject.chartId = m_scatterChart->objectName();
                 subject.index = index;
                 publishToHub( subject );
                 showPickMarker( index );
             } );
    connect( m_profileChart, &VaChartWidget::categorySelected, this,
             [this]( int index ) {
                 VaSelectionSubject subject;
                 subject.kind = VaSelectionKind::ChartCategory;
                 subject.chartId = m_profileChart->objectName();
                 subject.index = index;
                 publishToHub( subject );
                 m_statusLabel->setText(
                   tr( "联动：选中波段 %1。" ).arg( index + 1 ) );
             } );

    // ── Hub consumption: other surfaces' brush events drive this panel.
    if ( m_hub )
        connect( m_hub, &VaSelectionHub::selectionPublished, this,
                 [this]( const VaSelectionEvent &event ) { consumeHubEvent( event ); } );

    // ── Cursor probe readout (async, stale-generation-dropped).
    connect( &m_probe, &VaCursorProbe::sampled, this,
             [this]( bool ok, double value, int band, bool noData, const QString &message ) {
                 if ( ok )
                     m_cursorLabel->setText( tr( "光标采样：波段 %1 = %2" )
                                               .arg( band )
                                               .arg( value, 0, 'g', 6 ) );
                 else if ( noData )
                     m_cursorLabel->setText( tr( "光标采样：NoData（%1）" ).arg( message ) );
                 else
                     m_cursorLabel->setText( tr( "光标采样不可用：%1" ).arg( message ) );
             } );

    connect( &m_histogramSource, &VaDataSource::ready, m_histogramChart,
             [this]( const VaData &data ) {
                 m_histogramChart->setData( data );
                 m_histogramChart->setAccessibleName( tr( "波段直方图（抽样估计）" ) );
             } );
    connect( &m_histogramSource, &VaDataSource::failed, m_histogramChart,
             [this]( const QString &message ) { m_histogramChart->setError( message ); } );

    connect( &m_scatterSource, &VaDataSource::ready, this,
             [this]( const VaData &data ) {
                 m_lastScatter = data;
                 m_displayedScatter = data;
                 m_scatterChart->setData( data );
             } );
    connect( &m_scatterSource, &VaDataSource::failed, m_scatterChart,
             [this]( const QString &message ) { m_scatterChart->setError( message ); } );

    connect( &m_profileSource, &VaDataSource::ready, this,
             [this]( const VaData &data ) {
                 m_profileChart->setData( data );
                 // The profile carries one x per band — populate the band
                 // combos now that the band count is known (pooled metadata,
                 // never GUI-thread I/O).
                 const int bandCount = data.series.xs.size();
                 if ( bandCount > 0 && m_bandA->count() != bandCount )
                 {
                     QSignalBlocker blockA( m_bandA );
                     QSignalBlocker blockB( m_bandB );
                     m_bandA->clear();
                     m_bandB->clear();
                     for ( int b = 1; b <= bandCount; ++b )
                     {
                         m_bandA->addItem( QString::number( b ), b );
                         m_bandB->addItem( QString::number( b ), b );
                     }
                     m_bandA->setCurrentIndex( std::min( 0, bandCount - 1 ) );
                     m_bandB->setCurrentIndex( std::min( 1, bandCount - 1 ) );
                 }
             } );
    connect( &m_profileSource, &VaDataSource::failed, m_profileChart,
             [this]( const QString &message ) { m_profileChart->setError( message ); } );

    setWidget( central );
}

void VaWorkbenchPanel::publishToHub( const VaSelectionSubject &subject )
{
    if ( m_hub )
        m_hub->publish( subject, m_origin );
}

void VaWorkbenchPanel::consumeHubEvent( const VaSelectionEvent &event )
{
    // Own broadcasts were already applied locally — never re-consumed.
    if ( event.origin == m_origin )
        return;
    switch ( event.subject.kind )
    {
        case VaSelectionKind::ChartRange:
            applyScatterRangeFilter( event.subject.x0, event.subject.x1,
                                     event.subject.chartId );
            break;
        case VaSelectionKind::ChartCategory:
            m_statusLabel->setText(
              tr( "联动（%1）：选中类别 %2。" )
                .arg( event.origin, event.subject.chartId ) );
            break;
        case VaSelectionKind::Pixel:
            // External pixel picks cannot be resolved to OUR raster
            // honestly (the subject carries no path) — readout only.
            m_cursorLabel->setText(
              tr( "联动像素：row %1, col %2" )
                .arg( event.subject.row )
                .arg( event.subject.column ) );
            break;
        default:
            break;
    }
}

void VaWorkbenchPanel::applyScatterRangeFilter( double x0, double x1,
                                                const QString &originChartId )
{
    Q_UNUSED( originChartId );
    m_filterMin = x0;
    m_filterMax = x1;
    m_hasFilter = true;
    if ( m_lastScatter.kind != VaChartKind::Scatter )
        return;
    const VaData filtered = filterScatterByXRange( m_lastScatter, m_filterMin, m_filterMax );
    m_displayedScatter = filtered;
    m_scatterChart->setData( filtered );
    m_statusLabel->setText(
      tr( "联动过滤：散点限制在直方图范围 [%1, %2]，共 %3 点。" )
        .arg( x0, 0, 'g', 4 )
        .arg( x1, 0, 'g', 4 )
        .arg( filtered.scatter.xs.size() ) );
}

void VaWorkbenchPanel::showPickMarker( int index )
{
    // Resolve the pick to a map location from the payload the widget is
    // CURRENTLY SHOWING (post-brush-filter) — geotransform arithmetic, no
    // re-scan, no I/O. Resolving against the raw snapshot would place the
    // marker at the wrong point after any filter.
    double rx = 0, ry = 0;
    if ( m_displayedScatter.kind != VaChartKind::Scatter
         || !scatterPickToMapPoint( m_displayedScatter.scatter, index, &rx, &ry ) )
    {
        return;
    }
    QgsMapCanvas *canvas = m_canvasProvider ? m_canvasProvider() : nullptr;
    if ( !canvas )
        return;

    QgsPointXY canvasPoint( rx, ry );
    const QgsCoordinateReferenceSystem rasterCrs( m_lastScatter.scatter.crsWkt );
    const QgsCoordinateReferenceSystem canvasCrs =
      canvas->mapSettings().destinationCrs();
    if ( rasterCrs.isValid() && canvasCrs.isValid() && rasterCrs != canvasCrs )
    {
        try
        {
            QgsCoordinateTransform ct( rasterCrs, canvasCrs, QgsProject::instance() );
            canvasPoint = ct.transform( canvasPoint );
        }
        catch ( const QgsCsException & )
        {
            // Fail closed: no marker instead of a wrong location.
            return;
        }
    }
    if ( m_markerCanvas != canvas )
    {
        m_pickMarker = nullptr; // old marker died with its old canvas
        m_markerCanvas = canvas;
    }
    if ( !m_pickMarker )
    {
        auto *created = new QgsVertexMarker( canvas );
        created->setIconType( QgsVertexMarker::ICON_BOX );
        m_pickMarker = created;
    }
    m_pickMarker->setCenter( canvasPoint );
    m_pickMarker->show();
}

void VaWorkbenchPanel::onViewCursorMoved( const QString &viewId, double x, double y,
                                          const QString &crsWkt )
{
    Q_UNUSED( viewId );
    m_cursorLabel->setText( tr( "光标：%1, %2" ).arg( x, 0, 'g', 6 ).arg( y, 0, 'g', 6 ) );
    const int band = m_bandA->currentData().isValid() ? m_bandA->currentData().toInt() : 1;
    m_probe.request( QgsPointXY( x, y ), crsWkt, band );
}

void VaWorkbenchPanel::onViewCursorLeft( const QString &viewId )
{
    Q_UNUSED( viewId );
    m_cursorLabel->setText( tr( "光标：—" ) );
}

void VaWorkbenchPanel::refreshCharts()
{
    const QString path = m_provider ? m_provider() : QString();
    if ( path.isEmpty() )
    {
        m_statusLabel->setText( tr( "当前选择没有栅格图层。" ) );
        m_histogramChart->clear();
        m_scatterChart->clear();
        m_profileChart->clear();
        return;
    }
    m_statusLabel->setText( tr( "正在从 %1 抽样计算（有界、可取消）。" ).arg( path ) );
    requestProfile();
    requestHistogram();
    requestScatter();
}

void VaWorkbenchPanel::requestProfile()
{
    const QString path = m_provider ? m_provider() : QString();
    if ( path.isEmpty() )
        return;
    m_profileChart->setLoading();
    m_profileSource.request(
        [pathUtf8 = path.toUtf8()]( const std::function<bool()> &stale ) {
            auto reader = sicnu::geo::RasterReader::open( pathUtf8.constData() );
            const auto &meta = reader.metadata();
            const int bandCount = meta.bandCount;
            VaSeries series;
            series.name = QObject::tr( "各波段均值（抽样估计 %1×%2）" ).arg( kThumbSize ).arg( kThumbSize );
            series.xLabel = QObject::tr( "波段" );
            series.yLabel = QObject::tr( "均值（估计）" );
            // Bands stream one at a time so cancellation polls between them.
            for ( int band = 1; band <= bandCount; ++band )
            {
                if ( stale() )
                    throw std::runtime_error( "canceled" );
                int tw = 0, th = 0;
                const std::vector<double> values =
                    readThumbnail( reader, { band }, kThumbSize, &tw, &th );
                const sicnu::geo::BandInfo &bandInfo =
                    meta.bands.at( static_cast<size_t>( band - 1 ) );
                double sum = 0;
                qint64 valid = 0;
                for ( double v : values )
                {
                    if ( isNoData( v, bandInfo ) )
                        continue;
                    sum += v;
                    ++valid;
                }
                series.xs.append( band );
                series.ys.append( valid > 0 ? sum / valid : 0.0 );
            }
            VaData data;
            data.kind = VaChartKind::Series;
            data.series = series;
            return data;
        } );
}

void VaWorkbenchPanel::requestHistogram()
{
    const QString path = m_provider ? m_provider() : QString();
    if ( path.isEmpty() )
        return;
    const int band = m_bandA->currentData().isValid() ? m_bandA->currentData().toInt() : 1;
    m_histogramChart->setLoading();
    m_histogramSource.request(
        [pathUtf8 = path.toUtf8(), band]( const std::function<bool()> &stale ) {
            int tw = 0, th = 0;
            auto reader = sicnu::geo::RasterReader::open( pathUtf8.constData() );
            const auto &meta = reader.metadata();
            if ( band < 1 || band > meta.bandCount )
                throw std::runtime_error( "band out of range" );
            const sicnu::geo::BandInfo &bandInfo =
                meta.bands.at( static_cast<size_t>( band - 1 ) );
            const std::vector<double> values =
                readThumbnail( reader, { band }, kThumbSize, &tw, &th );

            double min = std::numeric_limits<double>::infinity();
            double max = -std::numeric_limits<double>::infinity();
            qint64 valid = 0;
            qint64 noData = 0;
            double sum = 0;
            double sumSq = 0;
            for ( double v : values )
            {
                if ( isNoData( v, bandInfo ) )
                {
                    ++noData;
                    continue;
                }
                min = std::min( min, v );
                max = std::max( max, v );
                sum += v;
                sumSq += v * v;
                ++valid;
            }
            if ( stale() )
                throw std::runtime_error( "canceled" );
            if ( valid == 0 )
                throw std::runtime_error( "no valid pixels (all NoData or empty)" );

            VaHistogram histogram;
            const double span = max - min > 0 ? max - min : 1.0;
            for ( int b = 0; b <= kHistogramBins; ++b )
                histogram.binEdges.append( min + span * b / kHistogramBins );
            histogram.counts = QVector<qint64>( kHistogramBins, 0 );
            for ( double v : values )
            {
                if ( isNoData( v, bandInfo ) )
                    continue;
                int bin = static_cast<int>( ( v - min ) / span * kHistogramBins );
                bin = std::clamp( bin, 0, kHistogramBins - 1 );
                histogram.counts[bin] += 1;
            }
            histogram.validCount = valid;
            histogram.noDataCount = noData;
            histogram.min = min;
            histogram.max = max;
            histogram.mean = sum / valid;
            histogram.stddev = std::sqrt( std::max( 0.0, sumSq / valid - histogram.mean * histogram.mean ) );
            histogram.truncated = static_cast<qint64>( tw ) * th <
                                  static_cast<qint64>( meta.width ) * meta.height;

            VaData data;
            data.kind = VaChartKind::Histogram;
            data.histogram = histogram;
            return data;
        } );
}

void VaWorkbenchPanel::requestScatter()
{
    const QString path = m_provider ? m_provider() : QString();
    if ( path.isEmpty() )
        return;
    const int bandA = m_bandA->currentData().isValid() ? m_bandA->currentData().toInt() : 1;
    const int bandB = m_bandB->currentData().isValid() ? m_bandB->currentData().toInt() : 1;
    m_scatterChart->setLoading();
    m_scatterSource.request(
        [pathUtf8 = path.toUtf8(), bandA, bandB]( const std::function<bool()> &stale ) {
            auto reader = sicnu::geo::RasterReader::open( pathUtf8.constData() );
            const auto &meta = reader.metadata();
            if ( bandA < 1 || bandA > meta.bandCount || bandB < 1 || bandB > meta.bandCount )
                throw std::runtime_error( "band out of range" );
            const sicnu::geo::BandInfo &infoA = meta.bands.at( static_cast<size_t>( bandA - 1 ) );
            const sicnu::geo::BandInfo &infoB = meta.bands.at( static_cast<size_t>( bandB - 1 ) );
            int tw = 0, th = 0;
            const std::vector<double> values =
                readThumbnail( reader, { bandA, bandB }, kScatterThumb, &tw, &th );

            VaScatter scatter;
            scatter.xLabel = QObject::tr( "波段 %1" ).arg( bandA );
            scatter.yLabel = QObject::tr( "波段 %1" ).arg( bandB );
            const qint64 total = static_cast<qint64>( tw ) * th;
            const int stride = std::max( 1, static_cast<int>( total / kMaxScatterPoints ) );
            for ( qint64 i = 0; i < total; i += stride )
            {
                const double a = values.at( static_cast<size_t>( i ) );
                const double b = values.at( static_cast<size_t>( total + i ) );
                if ( isNoData( a, infoA ) || isNoData( b, infoB ) )
                    continue;
                // Full-resolution pixel geometry for the picked point (11.0):
                // thumbnail index → nearest full-res pixel. Pure arithmetic;
                // the map resolution happens on pick via the geotransform.
                const qint64 tx = i % tw;
                const qint64 ty = i / tw;
                scatter.cols.append( tx * meta.width / std::max( 1, tw ) );
                scatter.rows.append( ty * meta.height / std::max( 1, th ) );
                scatter.xs.append( a );
                scatter.ys.append( b );
                scatter.groups.append( -1 );
            }
            scatter.truncated = total > static_cast<qint64>( kMaxScatterPoints );
            if ( scatter.xs.isEmpty() )
                throw std::runtime_error( "no valid pixel pairs (all NoData or empty)" );
            if ( meta.hasGeotransform )
            {
                scatter.hasGeometry = true;
                for ( double g : meta.geotransform )
                    scatter.geotransform.append( g );
                scatter.crsWkt = QString::fromStdString( meta.crs.wkt );
                scatter.sourcePath = QString::fromUtf8( pathUtf8 );
            }

            VaData data;
            data.kind = VaChartKind::Scatter;
            data.scatter = scatter;
            return data;
        } );
}

} // namespace sicnu::app::va
