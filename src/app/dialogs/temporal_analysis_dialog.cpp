// src/app/dialogs/temporal_analysis_dialog.cpp
#include "temporal_analysis_dialog.h"

#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include "processing/algorithms/temporal/spatiotemporal_contracts.h"
#include "processing/algorithms/temporal/temporal_collection.h"
#include "processing/gdal/gdal_grid_compat.h"
#include "data/raster_grid_compat.h"
#include "processing/algorithms/temporal/temporal_preflight.h"
#include "processing/algorithms/temporal/temporal_workspace.h"
#include "data/data_manager.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDateTime>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace
{

struct AlgorithmEntry
{
    const char *id;
    const char *label;
};

const AlgorithmEntry kAlgorithms[] = {
    { "rs:temporal_summary", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "Time series statistics (mean / min-max / std dev / count)" ) },
    { "rs:temporal_composite", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "Temporal compositing (best pixel / mean / median)" ) },
    { "rs:temporal_index_series", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "Index time series (per-date NDVI/EVI/... stack)" ) },
    { "rs:temporal_trend", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "Linear trend (slope / intercept / R²)" ) },
    { "rs:temporal_anomaly", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "Time series anomalies (z-score / difference)" ) },
    { "rs:temporal_extract_series", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "Point/ROI time series extraction (CSV)" ) },
};

enum SceneColumns
{
    ColPath = 0,
    ColTime,
    ColPlatform,
    ColModality,
    ColStatus,
    ColCount
};

} // namespace

TemporalAnalysisDialog::TemporalAnalysisDialog( QWidget *parent )
  : RasterProcessingDialogBase( parent )
{
  setWindowTitle( dialogTitle() );
  setMinimumWidth( 680 );
  setupUi();
}

void TemporalAnalysisDialog::setupUi()
{
  auto *mainLayout = SicnuUi::makeDialogRootLayout( this );
  setupHelpBanner( mainLayout );

  // ---- input scenes ----
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "Epoch Scenes" ) );
  inputGroup->setToolTip( tr( "Multitemporal raster list. Times are parsed from product metadata / file names and can be edited; sorted by time before running." ) );
  auto *groupLayout = new QVBoxLayout( inputGroup );
  groupLayout->setContentsMargins( 10, 8, 10, 8 );
  groupLayout->setSpacing( 8 );

  m_sceneTable = new QTableWidget( 0, ColCount, inputGroup );
  m_sceneTable->setObjectName( QStringLiteral( "temporalSceneTable" ) );
  m_sceneTable->setHorizontalHeaderLabels( { tr( "Files" ), tr( "Time (ISO)" ), tr( "Platform" ), tr( "Modality" ), tr( "Status" ) } );
  m_sceneTable->horizontalHeader()->setSectionResizeMode( ColPath, QHeaderView::Stretch );
  m_sceneTable->horizontalHeader()->setSectionResizeMode( ColTime, QHeaderView::ResizeToContents );
  m_sceneTable->horizontalHeader()->setSectionResizeMode( ColPlatform, QHeaderView::ResizeToContents );
  m_sceneTable->horizontalHeader()->setSectionResizeMode( ColStatus, QHeaderView::ResizeToContents );
  m_sceneTable->setMinimumHeight( 160 );
  m_sceneTable->setAlternatingRowColors( true );
  SicnuDialogHelp::tip( m_sceneTable, tr( "Epoch list: the time column is editable (YYYY-MM-DD or a full timestamp); the status column shows grid consistency and QA bands." ) );
  groupLayout->addWidget( m_sceneTable );

  auto *btnRow = new QHBoxLayout();
  btnRow->setSpacing( 8 );
  auto *addBtn = new QPushButton( tr( "Add Epochs..." ), inputGroup );
  SicnuUi::markSecondary( addBtn );
  SicnuDialogHelp::tip( addBtn, tr( "Adds one or more epoch raster files (acquisition times parsed automatically)." ) );
  connect( addBtn, &QPushButton::clicked, this, &TemporalAnalysisDialog::addScenes );
  btnRow->addWidget( addBtn );

  auto *removeBtn = new QPushButton( tr( "Remove Selected" ), inputGroup );
  SicnuUi::markSecondary( removeBtn );
  SicnuDialogHelp::tip( removeBtn, tr( "Removes the selected rasters from the epoch list." ) );
  connect( removeBtn, &QPushButton::clicked, this, &TemporalAnalysisDialog::removeSelectedScenes );
  btnRow->addWidget( removeBtn );

  auto *preflightBtn = new QPushButton( tr( "Precheck" ), inputGroup );
  SicnuDialogHelp::tip( preflightBtn,
                        tr( "Runs time / grid / band-role / radiometric consistency checks without any computation." ) );
  connect( preflightBtn, &QPushButton::clicked, this, &TemporalAnalysisDialog::runPreflight );
  btnRow->addWidget( preflightBtn );
  btnRow->addStretch();
  groupLayout->addLayout( btnRow );

  auto *filterRow = new QHBoxLayout();
  filterRow->setSpacing( 8 );
  filterRow->addWidget( new QLabel( tr( "Date filter:" ), inputGroup ) );
  m_filterEdit = new QLineEdit( inputGroup );
  m_filterEdit->setObjectName( QStringLiteral( "temporalFilterEdit" ) );
  m_filterEdit->setPlaceholderText( tr( "e.g. 2025-04 (filters the display by the time column; does not affect computation)" ) );
  SicnuDialogHelp::tip( m_filterEdit, tr( "Filters the list display only; all non-removed epochs take part in the computation." ) );
  connect( m_filterEdit, &QLineEdit::textChanged, this, &TemporalAnalysisDialog::filterChanged );
  filterRow->addWidget( m_filterEdit, 1 );
  groupLayout->addLayout( filterRow );

  m_preflightLabel = SicnuUi::makeHintLabel( inputGroup, tr( "Not prechecked yet: press 'Precheck' to verify time / grid / radiometric consistency." ) );
  groupLayout->addWidget( m_preflightLabel );

  // ---- algorithm + parameters ----
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "Analysis and Parameters" ) );
  auto *paramLayout = new QVBoxLayout( paramGroup );
  paramLayout->setContentsMargins( 10, 8, 10, 8 );
  paramLayout->setSpacing( 8 );

  auto *algRow = new QHBoxLayout();
  algRow->addWidget( new QLabel( tr( "Analysis:" ), paramGroup ) );
  m_algorithmCombo = new QComboBox( paramGroup );
  m_algorithmCombo->setObjectName( QStringLiteral( "temporalAlgorithmCombo" ) );
  for ( const auto &entry : kAlgorithms )
    m_algorithmCombo->addItem( tr( entry.label ), QString::fromLatin1( entry.id ) );
  SicnuDialogHelp::tip( m_algorithmCombo, tr( "Time series analysis algorithms (executed via the processing registry; reusable in the toolbox / Agent)." ) );
  connect( m_algorithmCombo, &QComboBox::currentIndexChanged, this, &TemporalAnalysisDialog::algorithmChanged );
  algRow->addWidget( m_algorithmCombo, 1 );

  algRow->addWidget( new QLabel( tr( "Band roles:" ), paramGroup ) );
  m_bandRoleCombo = new QComboBox( paramGroup );
  m_bandRoleCombo->setObjectName( QStringLiteral( "temporalBandRoleCombo" ) );
  m_bandRoleCombo->addItem( tr( "Band 1" ), QString() );
  m_bandRoleCombo->addItem( QStringLiteral( "blue" ), QStringLiteral( "blue" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "green" ), QStringLiteral( "green" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "red" ), QStringLiteral( "red" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "red_edge" ), QStringLiteral( "red_edge" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "nir" ), QStringLiteral( "nir" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "swir1" ), QStringLiteral( "swir1" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "swir2" ), QStringLiteral( "swir2" ) );
  SicnuDialogHelp::tip( m_bandRoleCombo, tr( "Analysis bands are resolved by semantic role (metadata first; falls back to the conventional order with a warning when absent)." ) );
  algRow->addWidget( m_bandRoleCombo );
  paramLayout->addLayout( algRow );

  // per-algorithm parameter pages
  m_paramStack = new QStackedWidget( paramGroup );
  m_paramStack->setObjectName( QStringLiteral( "temporalParamStack" ) );

  // page: summary
  {
    auto *page = new QFrame( m_paramStack );
    auto *lay = new QHBoxLayout( page );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->addWidget( SicnuUi::makeHintLabel(
      page, tr( "Output bands: count / valid_count / mean / min / max / stddev. When median is ticked, tile size shrinks automatically to fit the memory budget (exact values, not approximate)." ) ) );
    m_medianCheck = new QCheckBox( tr( "Includes median" ), page );
    m_medianCheck->setObjectName( QStringLiteral( "temporalMedianCheck" ) );
    SicnuDialogHelp::tip( m_medianCheck, tr( "Exact per-pixel median; tile size shrinks automatically for long series to respect the memory budget." ) );
    lay->addWidget( m_medianCheck );
    m_paramStack->addWidget( page );
  }
  // page: composite
  {
    auto *page = new QFrame( m_paramStack );
    auto *lay = new QHBoxLayout( page );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->addWidget( new QLabel( tr( "Method:" ), page ) );
    m_compositeMethodCombo = new QComboBox( page );
    m_compositeMethodCombo->setObjectName( QStringLiteral( "temporalCompositeMethod" ) );
    m_compositeMethodCombo->addItem( tr( "Best Pixel" ), QStringLiteral( "best_pixel" ) );
    m_compositeMethodCombo->addItem( tr( "Mean" ), QStringLiteral( "mean" ) );
    m_compositeMethodCombo->addItem( tr( "Median" ), QStringLiteral( "median" ) );
    SicnuDialogHelp::tip( m_compositeMethodCombo,
                          tr( "Best pixel: highest quality score among valid observations (ties broken by closeness to the target date, then by the earlier epoch);"
                              tr("The output includes valid-observation count and quality score bands.") ) );
    lay->addWidget( m_compositeMethodCombo );
    lay->addWidget( new QLabel( tr( "Period:" ), page ) );
    m_periodCombo = new QComboBox( page );
    m_periodCombo->setObjectName( QStringLiteral( "temporalPeriodCombo" ) );
    m_periodCombo->addItem( tr( "All" ), QStringLiteral( "all" ) );
    m_periodCombo->addItem( tr( "Monthly" ), QStringLiteral( "month" ) );
    m_periodCombo->addItem( tr( "Seasonally" ), QStringLiteral( "quarter" ) );
    m_periodCombo->addItem( tr( "Season" ), QStringLiteral( "season" ) );
    m_periodCombo->addItem( tr( "Yearly" ), QStringLiteral( "year" ) );
    SicnuDialogHelp::tip( m_periodCombo, tr( "When grouped by period, one file is written per period (suffix = start date)." ) );
    lay->addWidget( m_periodCombo );
    lay->addStretch();
    m_paramStack->addWidget( page );
  }
  // page: index series
  {
    auto *page = new QFrame( m_paramStack );
    auto *lay = new QHBoxLayout( page );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->addWidget( new QLabel( tr( "Index:" ), page ) );
    m_indexCombo = new QComboBox( page );
    m_indexCombo->setObjectName( QStringLiteral( "temporalIndexCombo" ) );
    for ( const char *idx : { "NDVI", "EVI", "SAVI", "NDWI", "NDBI", "MNDWI", "NBR", "NDRE", "NDSI", "NDTI" } )
      m_indexCombo->addItem( QString::fromLatin1( idx ), QString::fromLatin1( idx ) );
    SicnuDialogHelp::tip( m_indexCombo,
                          tr( "Same kernel as the single-scene spectral index; output is a stack with one band per date (acquisition-time metadata preserved)." ) );
    lay->addWidget( m_indexCombo );
    lay->addStretch();
    m_paramStack->addWidget( page );
  }
  // page: trend
  {
    auto *page = new QFrame( m_paramStack );
    auto *lay = new QHBoxLayout( page );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->addWidget( SicnuUi::makeHintLabel(
      page, tr( "Outputs: slope (per day) / intercept / R² / n / RMSE. The regression uses real acquisition-time intervals; slope × 365.25 = annual change rate." ) ) );
    m_paramStack->addWidget( page );
  }
  // page: anomaly
  {
    auto *page = new QFrame( m_paramStack );
    auto *lay = new QHBoxLayout( page );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->addWidget( new QLabel( tr( "Method:" ), page ) );
    m_anomalyMethodCombo = new QComboBox( page );
    m_anomalyMethodCombo->setObjectName( QStringLiteral( "temporalAnomalyMethod" ) );
    m_anomalyMethodCombo->addItem( tr( "z-score" ), QStringLiteral( "zscore" ) );
    m_anomalyMethodCombo->addItem( tr( "Difference from baseline mean" ), QStringLiteral( "difference" ) );
    SicnuDialogHelp::tip( m_anomalyMethodCombo,
                          tr( "The baseline defaults to all epochs except the target; narrow it with the baseline_start/end parameters." ) );
    lay->addWidget( m_anomalyMethodCombo );
    lay->addStretch();
    m_paramStack->addWidget( page );
  }
  // page: extract series
  {
    auto *page = new QFrame( m_paramStack );
    auto *grid = new QGridLayout( page );
    grid->setContentsMargins( 0, 0, 0, 0 );
    grid->setSpacing( 8 );
    grid->addWidget( new QLabel( tr( "Point (x, y):" ), page ), 0, 0 );
    m_pointEdit = new QLineEdit( page );
    m_pointEdit->setObjectName( QStringLiteral( "temporalPointEdit" ) );
    m_pointEdit->setPlaceholderText( tr( "Map coordinates, e.g. 460000.5, 3390020.25 (alternative to point/polygon)" ) );
    SicnuDialogHelp::tip( m_pointEdit, tr( "The point coordinates must share the epoch collection's CRS." ) );
    grid->addWidget( m_pointEdit, 0, 1 );
    grid->addWidget( new QLabel( tr( "Polygon ROIs:" ), page ), 1, 0 );
    m_polygonEdit = new QLineEdit( page );
    m_polygonEdit->setObjectName( QStringLiteral( "temporalPolygonEdit" ) );
    m_polygonEdit->setPlaceholderText( tr( "Vertex list x1,y1;x2,y2;x3,y3;... (closed ring; the bounding box is scanned only)" ) );
    SicnuDialogHelp::tip( m_polygonEdit, tr( "ROI statistics: mean/median/min/max/stddev/valid_count, exported as CSV per date." ) );
    grid->addWidget( m_polygonEdit, 1, 1 );
    m_paramStack->addWidget( page );
  }
  paramLayout->addWidget( m_paramStack );

  setupOutputRow( mainLayout );
  setupButtonBar( mainLayout );
  mainLayout->addStretch( 1 );

  algorithmChanged();
}

void TemporalAnalysisDialog::addScenes()
{
  const QStringList paths = QFileDialog::getOpenFileNames(
    this, tr( "Add Epoch Rasters" ), QString(),
    tr( "Rasters (*.tif *.tiff *.img *.asc);;All Files (*)" ) );
  if ( paths.isEmpty() )
    return;

  for ( const QString &path : paths )
  {
    sicnu::temporal::TemporalSceneRef scene;
    QString err;
    sicnu::temporal::inspectScene( path, QString(), &scene, &err );

    const int row = m_sceneTable->rowCount();
    m_sceneTable->insertRow( row );
    auto *pathItem = new QTableWidgetItem( path );
    pathItem->setFlags( pathItem->flags() & ~Qt::ItemIsEditable );
    m_sceneTable->setItem( row, ColPath, pathItem );
    auto *timeItem = new QTableWidgetItem( scene.time.valid ? scene.time.iso : QString() );
    timeItem->setToolTip( tr( "Editable: YYYY-MM-DD or ISO timestamp; empty means unknown (the precheck will reject it)." ) );
    m_sceneTable->setItem( row, ColTime, timeItem );
    auto *platformItem = new QTableWidgetItem( scene.platform );
    platformItem->setFlags( platformItem->flags() & ~Qt::ItemIsEditable );
    m_sceneTable->setItem( row, ColPlatform, platformItem );
    // Platform 3.0: show the observation modality (declared or inferred) so
    // mixed-modality mistakes are visible before the preflight runs.
    const auto contract = sicnu::temporal::ObservationContract::fromSceneRef( scene );
    const QString modalityText = contract.modality == sicnu::temporal::Modality::Unknown
                                   ? QString()
                                   : sicnu::temporal::modalityToString( contract.modality );
    auto *modalityItem = new QTableWidgetItem( modalityText );
    modalityItem->setFlags( modalityItem->flags() & ~Qt::ItemIsEditable );
    m_sceneTable->setItem( row, ColModality, modalityItem );
    auto *statusItem = new QTableWidgetItem();
    statusItem->setFlags( statusItem->flags() & ~Qt::ItemIsEditable );
    m_sceneTable->setItem( row, ColStatus, statusItem );
    // remember the automatically resolved time source for status display
    statusItem->setData( Qt::UserRole, scene.timeSource );
  }
  // keep chronological order (deterministic: time, then insertion order)
  m_sceneTable->sortItems( ColTime, Qt::AscendingOrder );
  refreshStatusColumn();
}

void TemporalAnalysisDialog::removeSelectedScenes()
{
  const auto selected = m_sceneTable->selectionModel()->selectedRows();
  // Remove in DESCENDING row order: removeRow() shifts every row below it, so
  // a forward pass over the pre-removal indices deletes the wrong scenes (and
  // a tail index past the shrinking end is a silent no-op that leaves a
  // selected scene in the table feeding the run).
  QList<int> rows;
  rows.reserve( selected.size() );
  for ( const QModelIndex &idx : selected )
    rows.append( idx.row() );
  std::sort( rows.begin(), rows.end(), std::greater<int>() );
  for ( int row : rows )
    m_sceneTable->removeRow( row );
  refreshStatusColumn();
}

void TemporalAnalysisDialog::refreshStatusColumn()
{
  // Quick per-scene status vs the first scene: dimensions/CRS + QA band.
  int refW = 0;
  int refH = 0;
  GdalDatasetWrapper refDsHolder;
  const GdalDatasetWrapper *refDs = nullptr;
  for ( int row = 0; row < m_sceneTable->rowCount(); ++row )
  {
    const QString path = m_sceneTable->item( row, ColPath )->text();
    GdalDatasetWrapper ds;
    if ( !ds.open( path ) )
    {
      m_sceneTable->item( row, ColStatus )->setText( tr( "Cannot Open" ) );
      continue;
    }
    if ( row == 0 )
    {
      refW = ds.width();
      refH = ds.height();
      // keep the reference dataset open for the compareGrids-based status
      if ( refDsHolder.open( path ) )
        refDs = &refDsHolder;
    }
    if ( !refDs )
    {
      m_sceneTable->item( row, ColStatus )->setText( tr( "Grid unknown" ) );
      continue;
    }
    QStringList status;
    // Same gate as the operator preflight (data::compareGrids: CRS / pixel
    // size / sub-pixel origin / extent), so the dialog can never claim a
    // compatibility the scientific gate rejects.
    const sicnu::data::RasterGrid grid = sicnu::processing::gridFromDataset( ds );
    const sicnu::data::RasterGrid refGrid = sicnu::processing::gridFromDataset( *refDs );
    const bool gridOk = ds.width() == refW && ds.height() == refH &&
                        sicnu::data::compareGrids( refGrid, grid ).compatible();
    status << ( gridOk ? tr( "Grids consistent" ) : tr( "Grids inconsistent" ) );
    const QString timeText = m_sceneTable->item( row, ColTime )->text().trimmed();
    if ( timeText.isEmpty() )
      status << tr( "Missing time" );
    bool hasQa = false;
    for ( int b = 1; b <= ds.bandCount() && !hasQa; ++b )
    {
      const QString role = ds.bandMetadataItem( b, "SICNU_BAND_ROLE" );
      if ( role == QLatin1String( "qa" ) || role == QLatin1String( "scene_classification" ) )
        hasQa = true;
    }
    if ( hasQa )
      status << tr( "QA✓" );
    m_sceneTable->item( row, ColStatus )->setText( status.join( QStringLiteral( " · " ) ) );
  }
}

void TemporalAnalysisDialog::filterChanged( const QString &text )
{
  const QString needle = text.trimmed();
  for ( int row = 0; row < m_sceneTable->rowCount(); ++row )
  {
    const bool match = needle.isEmpty() ||
                       m_sceneTable->item( row, ColTime )->text().contains( needle, Qt::CaseInsensitive );
    m_sceneTable->setRowHidden( row, !match );
  }
}

void TemporalAnalysisDialog::algorithmChanged()
{
  const QString id = m_algorithmCombo->currentData().toString();
  int page = 0;
  if ( id == QLatin1String( "rs:temporal_composite" ) )
    page = 1;
  else if ( id == QLatin1String( "rs:temporal_index_series" ) )
    page = 2;
  else if ( id == QLatin1String( "rs:temporal_trend" ) )
    page = 3;
  else if ( id == QLatin1String( "rs:temporal_anomaly" ) )
    page = 4;
  else if ( id == QLatin1String( "rs:temporal_extract_series" ) )
    page = 5;
  m_paramStack->setCurrentIndex( page );
}

QStringList TemporalAnalysisDialog::scenePaths() const
{
  QStringList paths;
  for ( int row = 0; row < m_sceneTable->rowCount(); ++row )
    paths << m_sceneTable->item( row, ColPath )->text();
  return paths;
}

Json::Value TemporalAnalysisDialog::buildScenesJson() const
{
  Json::Value scenes( Json::arrayValue );
  for ( int row = 0; row < m_sceneTable->rowCount(); ++row )
  {
    Json::Value entry( Json::objectValue );
    entry["path"] = m_sceneTable->item( row, ColPath )->text().toStdString();
    const QString time = m_sceneTable->item( row, ColTime )->text().trimmed();
    if ( !time.isEmpty() )
      entry["time"] = time.toStdString();
    scenes.append( entry );
  }
  return scenes;
}

void TemporalAnalysisDialog::runPreflight()
{
  if ( m_sceneTable->rowCount() == 0 )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Add epoch scenes first." ) );
    return;
  }
  const QStringList paths = scenePaths();
  QStringList times;
  for ( int row = 0; row < m_sceneTable->rowCount(); ++row )
    times << m_sceneTable->item( row, ColTime )->text().trimmed();

  auto collection = sicnu::temporal::TemporalCollection::fromScenePaths( paths, times );
  sicnu::temporal::PreflightOptions options;
  const auto report = sicnu::temporal::runPreflight( collection, options );

  QStringList summary;
  summary << tr( "Epoch: %1" ).arg( report.sceneCount );
  if ( !collection.timeRangeStartIso().isEmpty() )
    summary << tr( "Range: %1 → %2" )
                   .arg( collection.timeRangeStartIso(), collection.timeRangeEndIso() );
  summary << ( report.gridCompatible ? tr( "Grid: consistent" ) : tr( "Grid: inconsistent" ) );
  summary << tr( "Radiometric state: %1" )
                 .arg( report.commonRadiometricState.isEmpty()
                           ? tr( "Unknown (warning)" )
                           : report.commonRadiometricState );
  int blocking = 0;
  for ( const auto &issue : report.issues )
    if ( issue.blocking )
      ++blocking;
  summary << ( blocking == 0 ? tr( "Precheck Passed" )
                             : tr( "Precheck failed: %1 blocking issues" ).arg( blocking ) );
  m_preflightLabel->setText( summary.join( QStringLiteral( "  |  " ) ) );

  if ( !report.ok() )
  {
    QStringList lines;
    for ( const auto &issue : report.issues )
      if ( issue.blocking )
        lines << QStringLiteral( "[%1] %2" ).arg( issue.code, issue.message );
    QMessageBox::warning( this, tr( "Time Series Precheck" ),
                          lines.join( QLatin1Char( '\n' ) ).left( 2000 ) );
  }
}

bool TemporalAnalysisDialog::validateInputs()
{
  if ( m_sceneTable->rowCount() < 2 )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Time series analysis needs at least 2 epochs." ) );
    return false;
  }
  for ( int row = 0; row < m_sceneTable->rowCount(); ++row )
  {
    if ( m_sceneTable->item( row, ColTime )->text().trimmed().isEmpty() )
    {
      QMessageBox::warning( this, dialogTitle(),
                            tr( "Row %1 is missing an acquisition time (not parsed from product metadata / file name; enter it manually)." ).arg( row + 1 ) );
      return false;
    }
  }
  if ( outputPath().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "Specify the output file path." ) );
    return false;
  }
  const QString id = m_algorithmCombo->currentData().toString();
  if ( id == QLatin1String( "rs:temporal_extract_series" ) )
  {
    const bool hasPoint = !m_pointEdit->text().trimmed().isEmpty();
    const bool hasPoly = !m_polygonEdit->text().trimmed().isEmpty();
    if ( hasPoint == hasPoly )
    {
      QMessageBox::warning( this, dialogTitle(), tr( "Series extraction requires exactly one of: a point or a polygon." ) );
      return false;
    }
  }
  return true;
}

sicnu::data::DataManager *TemporalAnalysisDialog::dataManager() const
{
  if ( m_dataManager )
    return m_dataManager;
  return sicnu::temporal::workspaceCatalog();
}

bool TemporalAnalysisDialog::loadCollection( const sicnu::data::CollectionId &id )
{
  auto *dm = dataManager();
  if ( !dm )
    return false;
  sicnu::temporal::TemporalCollection col;
  QString err;
  if ( !sicnu::temporal::loadCollectionFromWorkspace( *dm, id, &col, &err ) )
    return false;

  m_activeCollectionId = id;
  m_sceneTable->setRowCount( 0 );
  for ( const auto &scene : col.scenes() )
  {
    const int row = m_sceneTable->rowCount();
    m_sceneTable->insertRow( row );
    m_sceneTable->setItem( row, ColPath, new QTableWidgetItem( scene.path ) );
    m_sceneTable->setItem( row, ColTime, new QTableWidgetItem( scene.time.valid ? scene.time.iso : QString() ) );
    m_sceneTable->setItem( row, ColPlatform, new QTableWidgetItem( scene.platform ) );
    {
      const auto contract = sicnu::temporal::ObservationContract::fromSceneRef( scene );
      const QString modalityText = contract.modality == sicnu::temporal::Modality::Unknown
                                     ? QString()
                                     : sicnu::temporal::modalityToString( contract.modality );
      m_sceneTable->setItem( row, ColModality, new QTableWidgetItem( modalityText ) );
    }
    m_sceneTable->setItem( row, ColStatus, new QTableWidgetItem( tr( "Loaded" ) ) );
  }
  refreshStatusColumn();
  return true;
}

sicnu::temporal::TemporalCollection TemporalAnalysisDialog::buildCollectionFromUi() const
{
  sicnu::temporal::TemporalCollection col;
  for ( int row = 0; row < m_sceneTable->rowCount(); ++row )
  {
    const QString path = m_sceneTable->item( row, ColPath ) ? m_sceneTable->item( row, ColPath )->text().trimmed() : QString();
    if ( path.isEmpty() )
      continue;
    const QString timeStr = m_sceneTable->item( row, ColTime ) ? m_sceneTable->item( row, ColTime )->text().trimmed() : QString();
    const QString platform = m_sceneTable->item( row, ColPlatform ) ? m_sceneTable->item( row, ColPlatform )->text().trimmed() : QString();

    sicnu::temporal::TemporalSceneRef scene;
    scene.path = path;
    if ( !timeStr.isEmpty() )
    {
      scene.time = sicnu::temporal::parseAcquisitionTime( timeStr );
      scene.timeSource = QStringLiteral( "explicit" );
    }
    else
    {
      QString inspectErr;
      sicnu::temporal::inspectScene( path, QString(), &scene, &inspectErr );
    }
    if ( !platform.isEmpty() )
      scene.platform = platform;
    scene.originalIndex = row;
    col.scenes().push_back( std::move( scene ) );
  }
  col.sortScenes();
  return col;
}

void TemporalAnalysisDialog::onRun()
{
  Json::Value params( Json::objectValue );
  auto *dm = dataManager();
  const auto collection = buildCollectionFromUi();

  if ( dm && collection.sceneCount() > 0 )
  {
    QString colName = tr( "Time Series Analysis Collection %1" ).arg( QDateTime::currentDateTime().toString( QStringLiteral( "yyyy-MM-dd hh:mm" ) ) );
    sicnu::data::CollectionId existingId = m_activeCollectionId.value_or( sicnu::data::CollectionId() );
    QString saveErr;
    const sicnu::data::CollectionId colId = sicnu::temporal::saveCollectionToWorkspace( *dm, colName, collection, existingId, &saveErr );
    if ( !colId.isNull() )
    {
      m_activeCollectionId = colId;
      params["collection"] = colId.toString().toStdString();
    }
    else
    {
      params["scenes"] = buildScenesJson();
    }
  }
  else
  {
    params["scenes"] = buildScenesJson();
  }

  params["output"] = outputPath().toStdString();
  const QString role = m_bandRoleCombo->currentData().toString();
  if ( !role.isEmpty() )
    params["band_role"] = role.toStdString();

  const QString id = m_algorithmCombo->currentData().toString();
  if ( id == QLatin1String( "rs:temporal_summary" ) )
  {
    params["include_median"] = m_medianCheck->isChecked();
  }
  else if ( id == QLatin1String( "rs:temporal_composite" ) )
  {
    params["method"] = m_compositeMethodCombo->currentData().toString().toStdString();
    params["period"] = m_periodCombo->currentData().toString().toStdString();
  }
  else if ( id == QLatin1String( "rs:temporal_index_series" ) )
  {
    params["index"] = m_indexCombo->currentData().toString().toStdString();
  }
  else if ( id == QLatin1String( "rs:temporal_anomaly" ) )
  {
    params["method"] = m_anomalyMethodCombo->currentData().toString().toStdString();
  }
  else if ( id == QLatin1String( "rs:temporal_extract_series" ) )
  {
    const QStringList xy = m_pointEdit->text().split( QLatin1Char( ',' ) );
    if ( !m_pointEdit->text().trimmed().isEmpty() )
    {
      if ( xy.size() != 2 )
      {
        QMessageBox::warning( this, dialogTitle(), tr( "Point coordinates must be in x,y format." ) );
        return;
      }
      Json::Value point( Json::arrayValue );
      point.append( xy.at( 0 ).trimmed().toDouble() );
      point.append( xy.at( 1 ).trimmed().toDouble() );
      params["point"] = point;
    }
    else
    {
      Json::Value polygon( Json::arrayValue );
      const QStringList vertices = m_polygonEdit->text().split( QLatin1Char( ';' ) );
      for ( const QString &v : vertices )
      {
        const QStringList xyv = v.split( QLatin1Char( ',' ) );
        if ( xyv.size() != 2 )
        {
          QMessageBox::warning( this, dialogTitle(), tr( "Polygon vertices must be x,y pairs separated by semicolons." ) );
          return;
        }
        Json::Value vertex( Json::arrayValue );
        vertex.append( xyv.at( 0 ).trimmed().toDouble() );
        vertex.append( xyv.at( 1 ).trimmed().toDouble() );
        polygon.append( vertex );
      }
      params["polygon"] = polygon;
    }
  }

  // Capture the operator's full result: grouped composites produce one file
  // per period and only the first lands in "output" — the shell must be able
  // to load every produced raster (#719).
  m_producedOutputs.clear();
  runOperatorTask( id, params, [this]( const Json::Value &result ) {
    collectProducedOutputs( result );
  } );
}

void TemporalAnalysisDialog::collectProducedOutputs( const Json::Value &result )
{
  const auto addPath = [this]( const std::string &path ) {
    if ( path.empty() )
      return;
    const QString qpath = QString::fromStdString( path );
    if ( !m_producedOutputs.contains( qpath ) )
      m_producedOutputs.append( qpath );
  };
  addPath( result["output"].asString() );
  const Json::Value &outputs = result["outputs"];
  if ( outputs.isArray() )
  {
    for ( const Json::Value &entry : outputs )
      addPath( entry["output"].asString() );
  }
}
