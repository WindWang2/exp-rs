/***************************************************************************
 * temporal_workbench_panel.cpp — see temporal_workbench_panel.h
 ***************************************************************************/
#include "temporal_workbench_panel.h"

#include "temporal_scene_model.h"

#include "data/data_manager.h"
#include "data/temporal_workspace_types.h"

#include <QComboBox>
#include <limits>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

namespace sicnu::app
{
namespace
{

/// Parses the stored descriptor document through the authoritative typed
/// layer. Returns false (and surfaces the parser error) on a malformed
/// descriptor — nothing is guessed.
bool parseDescriptor( const QString &descriptor, sicnu::temporal::TemporalCollection *out,
                      QString *error )
{
    Json::Value root;
    Json::Reader reader;
    if ( !reader.parse( descriptor.toStdString(), root ) )
    {
        if ( error )
            *error = QString::fromStdString( reader.getFormattedErrorMessages() );
        return false;
    }
    return sicnu::temporal::TemporalCollection::fromJson( root, out, error );
}

} // namespace

// ── TimelineBar ──────────────────────────────────────────────────────────────

TemporalTimelineBar::TemporalTimelineBar( QWidget *parent )
    : QWidget( parent )
{
    setFocusPolicy( Qt::StrongFocus ); // keyboard navigation (goal §G)
}

void TemporalTimelineBar::setScenes( const QVector<qint64> &epochMillis,
                                     const QVector<bool> &valid )
{
    m_times = epochMillis;
    m_valid = valid;
    if ( m_valid.size() != m_times.size() )
        m_valid = QVector<bool>( m_times.size(), true );
    m_selected = m_times.isEmpty() ? -1 : 0;
    update();
}

void TemporalTimelineBar::setSelectedIndex( int index )
{
    m_selected = index;
    update();
}

void TemporalTimelineBar::setWindow( qint64 fromMs, qint64 toMs )
{
    m_fromMs = fromMs;
    m_toMs = toMs;
    update();
}

void TemporalTimelineBar::paintEvent( QPaintEvent * )
{
    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing );
    painter.fillRect( rect(), palette().window() );
    const int axisY = height() / 2;
    painter.setPen( palette().color( QPalette::WindowText ) );
    painter.drawLine( 8, axisY, width() - 8, axisY );

    if ( m_times.isEmpty() )
    {
        painter.drawText( rect(), Qt::AlignCenter, tr( "Collection Has No Scenes" ) );
        return;
    }

    qint64 min = std::numeric_limits<qint64>::max();
    qint64 max = std::numeric_limits<qint64>::min();
    for ( qint64 t : m_times )
    {
        min = qMin( min, t );
        max = qMax( max, t );
    }
    if ( max == min )
        max = min + 1;

    // Filter window band (when a filter is active).
    if ( m_toMs > m_fromMs )
    {
        const int x0 = 8 + static_cast<int>( ( double )( m_fromMs - min ) / ( max - min ) *
                                            ( width() - 16 ) );
        const int x1 = 8 + static_cast<int>( ( double )( m_toMs - min ) / ( max - min ) *
                                             ( width() - 16 ) );
        QColor band = palette().color( QPalette::Highlight );
        band.setAlpha( 40 );
        painter.fillRect( x0, axisY - 10, qMax( 1, x1 - x0 ), 20, band );
    }

    for ( int i = 0; i < m_times.size(); ++i )
    {
        const int x = 8 + static_cast<int>( ( double )( m_times[i] - min ) / ( max - min ) *
                                            ( width() - 16 ) );
        const bool known = i < m_valid.size() ? m_valid[i] : true;
        if ( i == m_selected )
        {
            painter.setBrush( palette().color( QPalette::Highlight ) );
            painter.drawEllipse( QPointF( x, axisY ), 5, 5 );
            painter.setBrush( Qt::NoBrush );
        }
        else if ( known )
        {
            painter.drawEllipse( QPointF( x, axisY ), 2, 2 );
        }
        else
        {
            // Unknown acquisition date: hollow ring, never a fabricated
            // position among real dates.
            painter.drawEllipse( QPointF( x, axisY ), 3, 3 );
        }
    }
}

int TemporalTimelineBar::nearestIndex( int x ) const
{
    if ( m_times.isEmpty() )
        return -1;
    qint64 min = std::numeric_limits<qint64>::max();
    qint64 max = std::numeric_limits<qint64>::min();
    for ( qint64 t : m_times )
    {
        min = qMin( min, t );
        max = qMax( max, t );
    }
    if ( max == min )
        max = min + 1;
    const qint64 target =
        min + static_cast<qint64>( ( double )( x - 8 ) / ( width() - 16 ) * ( max - min ) );
    int best = 0;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    for ( int i = 0; i < m_times.size(); ++i )
    {
        const qint64 distance = qAbs( m_times[i] - target );
        if ( distance < bestDistance )
        {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

void TemporalTimelineBar::mousePressEvent( QMouseEvent *event )
{
    const int index = nearestIndex( static_cast<int>( event->position().x() ) );
    if ( index >= 0 )
    {
        setSelectedIndex( index );
        emit sceneClicked( index );
    }
}

void TemporalTimelineBar::keyPressEvent( QKeyEvent *event )
{
    if ( m_times.isEmpty() )
        return;
    if ( event->key() == Qt::Key_Left && m_selected > 0 )
    {
        setSelectedIndex( m_selected - 1 );
        emit sceneClicked( m_selected );
        return;
    }
    if ( event->key() == Qt::Key_Right && m_selected + 1 < m_times.size() )
    {
        setSelectedIndex( m_selected + 1 );
        emit sceneClicked( m_selected );
        return;
    }
    QWidget::keyPressEvent( event );
}

// ── Panel ────────────────────────────────────────────────────────────────────

TemporalWorkbenchPanel::TemporalWorkbenchPanel( DataManagerProvider provider, QWidget *parent )
    : QgsDockWidget( parent )
    , m_dataManager( std::move( provider ) )
{
    auto *central = new QWidget( this );
    auto *layout = new QVBoxLayout( central );

    // Collection picker + summary
    auto *pickerRow = new QHBoxLayout;
    m_collectionCombo = new QComboBox( central );
    m_collectionCombo->setObjectName( QStringLiteral( "rsTemporalCollectionCombo" ) );
    m_collectionSummary = new QLineEdit( central );
    m_collectionSummary->setReadOnly( true );
    m_collectionSummary->setPlaceholderText( tr( "Collection Summary" ) );
    pickerRow->addWidget( new QLabel( tr( "Time Series Collection" ), central ) );
    pickerRow->addWidget( m_collectionCombo, 1 );
    layout->addLayout( pickerRow );

    // Date filter row
    auto *filterRow = new QHBoxLayout;
    m_fromEdit = new QDateEdit( central );
    m_toEdit = new QDateEdit( central );
    for ( QDateEdit *edit : { m_fromEdit, m_toEdit } )
    {
        edit->setCalendarPopup( true );
        edit->setDisplayFormat( QStringLiteral( "yyyy-MM-dd" ) );
        edit->setSpecialValueText( tr( "Unlimited" ) );
        edit->setDateRange( QDate( 1970, 1, 1 ), QDate( 2100, 1, 1 ) );
        edit->setDate( QDate( 1970, 1, 1 ) );
    }
    auto *clearFilter = new QPushButton( tr( "Clear Filter" ), central );
    filterRow->addWidget( new QLabel( tr( "Start" ), central ) );
    filterRow->addWidget( m_fromEdit );
    filterRow->addWidget( new QLabel( tr( "Deadline" ), central ) );
    filterRow->addWidget( m_toEdit );
    filterRow->addWidget( clearFilter );
    filterRow->addStretch( 1 );
    layout->addLayout( filterRow );

    // Timeline strip
    m_timeline = new TemporalTimelineBar( central );
    m_timeline->setObjectName( QStringLiteral( "rsTemporalTimeline" ) );
    layout->addWidget( m_timeline );

    // Scene table (paged)
    m_model = new TemporalSceneModel( this );
    m_view = new QTableView( central );
    m_view->setObjectName( QStringLiteral( "rsTemporalSceneView" ) );
    m_view->setModel( m_model );
    m_view->setSelectionBehavior( QAbstractItemView::SelectRows );
    m_view->setSelectionMode( QAbstractItemView::ExtendedSelection );
    m_view->horizontalHeader()->setStretchLastSection( true );
    m_view->verticalHeader()->setDefaultSectionSize( 22 );
    layout->addWidget( m_view, 1 );

    // Paging + actions
    auto *pageRow = new QHBoxLayout;
    m_prevPage = new QPushButton( tr( "Previous Page" ), central );
    m_nextPage = new QPushButton( tr( "Next Page" ), central );
    m_pageLabel = new QLabel( central );
    m_previewBtn = new QPushButton( tr( "Preview Current Epoch" ), central );
    m_compareBtn = new QPushButton( tr( "Compare Two Epochs" ), central );
    pageRow->addWidget( m_prevPage );
    pageRow->addWidget( m_pageLabel );
    pageRow->addWidget( m_nextPage );
    pageRow->addStretch( 1 );
    pageRow->addWidget( m_previewBtn );
    pageRow->addWidget( m_compareBtn );
    layout->addLayout( pageRow );

    m_qaLabel = new QLabel( central );
    m_qaLabel->setWordWrap( true );
    layout->addWidget( m_qaLabel );

    setWidget( central );

    // ── Wiring ──────────────────────────────────────────────────────────
    connect( m_collectionCombo, &QComboBox::currentIndexChanged, this,
             &TemporalWorkbenchPanel::onCollectionSelected );
    connect( m_fromEdit, &QDateEdit::dateChanged, this,
             &TemporalWorkbenchPanel::onFilterChanged );
    connect( m_toEdit, &QDateEdit::dateChanged, this,
             &TemporalWorkbenchPanel::onFilterChanged );
    connect( clearFilter, &QPushButton::clicked, this, [this] {
        m_fromEdit->setDate( QDate( 1970, 1, 1 ) );
        m_toEdit->setDate( QDate( 1970, 1, 1 ) );
    } );
    connect( m_model, &TemporalSceneModel::pageChanged, this, [this]( int page, int pages ) {
        m_pageLabel->setText( tr( "Page %1 / %2" ).arg( page + 1 ).arg( pages ) );
    } );
    connect( m_prevPage, &QPushButton::clicked, this, &TemporalWorkbenchPanel::onPrevPage );
    connect( m_nextPage, &QPushButton::clicked, this, &TemporalWorkbenchPanel::onNextPage );
    connect( m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
             &TemporalWorkbenchPanel::onSelectionChanged );
    connect( m_view, &QTableView::doubleClicked, this,
             [this]( const QModelIndex & ) {
                 const QString path = selectedScenePath();
                 if ( !path.isEmpty() )
                     emit previewRequested( path );
             } );
    connect( m_previewBtn, &QPushButton::clicked, this, [this] {
        const QString path = selectedScenePath();
        if ( !path.isEmpty() )
            emit previewRequested( path );
    } );
    connect( m_compareBtn, &QPushButton::clicked, this, [this] {
        const QModelIndexList rows = m_view->selectionModel()->selectedRows();
        if ( rows.size() != 2 )
            return;
        const sicnu::temporal::TemporalSceneRef *a = m_model->sceneAtRow( rows[0].row() );
        const sicnu::temporal::TemporalSceneRef *b = m_model->sceneAtRow( rows[1].row() );
        if ( a && b )
            emit compareRequested( a->path, b->path );
    } );
    connect( m_timeline, &TemporalTimelineBar::sceneClicked, this, [this]( int index ) {
        // The timeline carries the FULL collection; the table is the FILTERED,
        // paged projection. Map through the model's index space — with a date
        // filter active the two indices would otherwise select wrong scenes.
        const int row = m_model->rowForSceneIndex( index );
        if ( row < 0 )
        {
            m_qaLabel->setText( tr( "This scene is hidden by the current date filter." ) );
            return;
        }
        const int page = row / TemporalSceneModel::kPageSize;
        if ( page != m_model->page() )
        {
            m_model->setPage( page );
            m_pageLabel->setText( tr( "Page %1 / %2" ).arg( page + 1 ).arg( m_model->pageCount() ) );
        }
        const int inPageRow = row - page * TemporalSceneModel::kPageSize;
        if ( inPageRow < m_model->rowCount() )
            m_view->selectRow( inPageRow );
        onSelectionChanged();
    } );

    refreshCollections();
}

void TemporalWorkbenchPanel::refreshCollections()
{
    QSignalBlocker blocker( m_collectionCombo );
    m_collectionCombo->clear();
    sicnu::data::DataManager *dataManager = m_dataManager ? m_dataManager() : nullptr;
    if ( !dataManager )
    {
        m_qaLabel->setText( tr( "The data catalog is unavailable." ) );
        return;
    }
    const QVector<sicnu::data::TemporalCollectionRecord> records = dataManager->temporalCollections();
    for ( const sicnu::data::TemporalCollectionRecord &record : records )
        m_collectionCombo->addItem( record.displayName,
                                    QVariant( record.id.toString() ) );
    m_qaLabel->setText(
        records.isEmpty() ? tr( "No time series collections in the project yet — create one via the Agent temporal tools or a STAC import." )
                          : QString() );
    blocker.unblock();
    if ( !records.isEmpty() )
        onCollectionSelected( 0 );
    else
        rebuildSceneTable();
}

void TemporalWorkbenchPanel::onCollectionSelected( int index )
{
    Q_UNUSED( index );
    rebuildSceneTable();
}

void TemporalWorkbenchPanel::onFilterChanged()
{
    const QDate sentinel( 1970, 1, 1 );
    const QDate from = m_fromEdit->date() > sentinel ? m_fromEdit->date() : QDate();
    const QDate to =
        m_toEdit->date() > sentinel ? m_toEdit->date() : QDate();
    m_model->setDateFilter( from, to );
    m_pageLabel->setText( tr( "Page 1 / %1" ).arg( m_model->pageCount() ) );
    // Highlight the active window on the timeline (sentinel = no filter).
    if ( from.isValid() || to.isValid() )
        m_timeline->setWindow( from.isValid()
                                   ? QDateTime( from, QTime( 0, 0 ), Qt::UTC ).toMSecsSinceEpoch()
                                   : 0,
                               to.isValid() ? QDateTime( to, QTime( 23, 59, 59 ), Qt::UTC )
                                                  .toMSecsSinceEpoch()
                                            : std::numeric_limits<qint64>::max() );
    else
        m_timeline->setWindow( 0, 0 );
}

void TemporalWorkbenchPanel::rebuildSceneTable()
{
    m_model->setScenes( {} );
    m_timeline->setScenes( {}, {} );
    m_collectionSummary->clear();
    m_qaLabel->clear();
    m_previewBtn->setEnabled( false );
    m_compareBtn->setEnabled( false );

    sicnu::data::DataManager *dataManager = m_dataManager ? m_dataManager() : nullptr;
    const QString collectionId = m_collectionCombo->currentData().toString();
    if ( !dataManager || collectionId.isEmpty() )
    {
        m_pageLabel->setText( tr( "Page 1 / 1" ) );
        return;
    }
    const auto id = sicnu::data::CollectionId::fromString( collectionId );
    if ( !id )
        return;
    const std::optional<sicnu::data::TemporalCollectionRecord> record =
        dataManager->temporalCollection( *id );
    if ( !record )
        return;

    sicnu::temporal::TemporalCollection collection;
    QString error;
    if ( !parseDescriptor( record->descriptor, &collection, &error ) )
    {
        // Malformed descriptors are surfaced, never silently ignored.
        m_qaLabel->setText( tr( "Failed to parse the description document: %1" ).arg( error ) );
        return;
    }

    const QVector<sicnu::temporal::TemporalSceneRef> scenes = collection.scenes();
    m_model->setScenes( scenes );

    QVector<qint64> times;
    QVector<bool> valid;
    times.reserve( scenes.size() );
    valid.reserve( scenes.size() );
    qint64 unknownDates = 0;
    qint64 clouds = 0;
    double cloudSum = 0.0;
    for ( const sicnu::temporal::TemporalSceneRef &scene : scenes )
    {
        times.append( scene.time.valid ? scene.time.epochMillis : 0 );
        valid.append( scene.time.valid );
        if ( !scene.time.valid )
            ++unknownDates;
        if ( scene.cloudCoverPercent >= 0 )
        {
            ++clouds;
            cloudSum += scene.cloudCoverPercent;
        }
    }
    m_timeline->setScenes( times, valid );

    const QString range =
        tr( "%1 to %2" )
            .arg( collection.timeRangeStartIso().isEmpty()
                      ? tr( "Unknown" )
                      : collection.timeRangeStartIso(),
                  collection.timeRangeEndIso().isEmpty() ? tr( "Unknown" )
                                                         : collection.timeRangeEndIso() );
    m_collectionSummary->setText(
        tr( "%1 — %2 scenes, %3" ).arg( record->displayName ).arg( scenes.size() ).arg( range ) );

    QStringList qa;
    if ( unknownDates > 0 )
        qa << tr( "%1 scenes have no acquisition time (the precheck will reject them)" ).arg( unknownDates );
    if ( clouds > 0 )
        qa << tr( "Mean cloud cover %1% (%2 scenes reported)" )
                   .arg( cloudSum / clouds, 0, 'f', 1 )
                   .arg( clouds );
    else
        qa << tr( "No cloud cover report" );
    m_qaLabel->setText( tr( "QA：%1" ).arg( qa.join( QStringLiteral( "；" ) ) ) );

    m_pageLabel->setText( tr( "Page 1 / %1" ).arg( m_model->pageCount() ) );
}

void TemporalWorkbenchPanel::onSelectionChanged()
{
    const QModelIndexList rows =
        m_view->selectionModel() ? m_view->selectionModel()->selectedRows() : QModelIndexList();
    m_previewBtn->setEnabled( rows.size() == 1 );
    m_compareBtn->setEnabled( rows.size() == 2 );
    if ( rows.size() == 1 )
    {
        const int global = m_model->sceneIndexAtRow( rows.first().row() );
        m_timeline->setSelectedIndex( global );
    }
}

void TemporalWorkbenchPanel::onPrevPage()
{
    m_model->setPage( m_model->page() - 1 );
}

void TemporalWorkbenchPanel::onNextPage()
{
    m_model->setPage( m_model->page() + 1 );
}

QString TemporalWorkbenchPanel::selectedScenePath( int *sceneIndexOut ) const
{
    const QModelIndex current =
        m_view->selectionModel() ? m_view->currentIndex() : QModelIndex();
    const sicnu::temporal::TemporalSceneRef *scene =
        current.isValid() ? m_model->sceneAtRow( current.row() ) : nullptr;
    if ( sceneIndexOut )
        *sceneIndexOut = current.isValid() ? m_model->sceneIndexAtRow( current.row() ) : -1;
    return scene ? scene->path : QString();
}

} // namespace sicnu::app
