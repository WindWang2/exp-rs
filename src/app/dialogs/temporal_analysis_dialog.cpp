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
    { "rs:temporal_summary", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "时序统计（均值/最值/标准差/计数）" ) },
    { "rs:temporal_composite", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "时序合成（最佳像元/均值/中值）" ) },
    { "rs:temporal_index_series", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "指数时序（逐日 NDVI/EVI/…栈）" ) },
    { "rs:temporal_trend", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "线性趋势（斜率/截距/R²）" ) },
    { "rs:temporal_anomaly", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "时序异常（z-score / 差值）" ) },
    { "rs:temporal_extract_series", QT_TRANSLATE_NOOP( "TemporalAnalysisDialog", "点/ROI 时间序列提取（CSV）" ) },
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
  QGroupBox *inputGroup = setupInputGroup( mainLayout, tr( "时相场景" ) );
  inputGroup->setToolTip( tr( "多时相栅格列表。时间自动从产品元数据/文件名解析，可手动修改；运行前按时间排序。" ) );
  auto *groupLayout = new QVBoxLayout( inputGroup );
  groupLayout->setContentsMargins( 10, 8, 10, 8 );
  groupLayout->setSpacing( 8 );

  m_sceneTable = new QTableWidget( 0, ColCount, inputGroup );
  m_sceneTable->setObjectName( QStringLiteral( "temporalSceneTable" ) );
  m_sceneTable->setHorizontalHeaderLabels( { tr( "文件" ), tr( "时间 (ISO)" ), tr( "平台" ), tr( "模态" ), tr( "状态" ) } );
  m_sceneTable->horizontalHeader()->setSectionResizeMode( ColPath, QHeaderView::Stretch );
  m_sceneTable->horizontalHeader()->setSectionResizeMode( ColTime, QHeaderView::ResizeToContents );
  m_sceneTable->horizontalHeader()->setSectionResizeMode( ColPlatform, QHeaderView::ResizeToContents );
  m_sceneTable->horizontalHeader()->setSectionResizeMode( ColStatus, QHeaderView::ResizeToContents );
  m_sceneTable->setMinimumHeight( 160 );
  m_sceneTable->setAlternatingRowColors( true );
  SicnuDialogHelp::tip( m_sceneTable, tr( "时相列表：时间列可编辑（YYYY-MM-DD 或完整时间戳）；状态列显示网格一致性与 QA 波段。" ) );
  groupLayout->addWidget( m_sceneTable );

  auto *btnRow = new QHBoxLayout();
  btnRow->setSpacing( 8 );
  auto *addBtn = new QPushButton( tr( "添加时相…" ), inputGroup );
  SicnuUi::markSecondary( addBtn );
  SicnuDialogHelp::tip( addBtn, tr( "添加一个或多个时相栅格文件（自动解析获取时间）。" ) );
  connect( addBtn, &QPushButton::clicked, this, &TemporalAnalysisDialog::addScenes );
  btnRow->addWidget( addBtn );

  auto *removeBtn = new QPushButton( tr( "移除选中" ), inputGroup );
  SicnuUi::markSecondary( removeBtn );
  SicnuDialogHelp::tip( removeBtn, tr( "从时相列表移除选中的栅格。" ) );
  connect( removeBtn, &QPushButton::clicked, this, &TemporalAnalysisDialog::removeSelectedScenes );
  btnRow->addWidget( removeBtn );

  auto *preflightBtn = new QPushButton( tr( "预检 (Preflight)" ), inputGroup );
  SicnuDialogHelp::tip( preflightBtn,
                        tr( "运行时间/网格/波段角色/辐射一致性检查，不做任何计算。" ) );
  connect( preflightBtn, &QPushButton::clicked, this, &TemporalAnalysisDialog::runPreflight );
  btnRow->addWidget( preflightBtn );
  btnRow->addStretch();
  groupLayout->addLayout( btnRow );

  auto *filterRow = new QHBoxLayout();
  filterRow->setSpacing( 8 );
  filterRow->addWidget( new QLabel( tr( "日期过滤：" ), inputGroup ) );
  m_filterEdit = new QLineEdit( inputGroup );
  m_filterEdit->setObjectName( QStringLiteral( "temporalFilterEdit" ) );
  m_filterEdit->setPlaceholderText( tr( "例如 2025-04（按时间列过滤显示，不影响计算）" ) );
  SicnuDialogHelp::tip( m_filterEdit, tr( "仅过滤列表显示；参与计算的是全部未移除的时相。" ) );
  connect( m_filterEdit, &QLineEdit::textChanged, this, &TemporalAnalysisDialog::filterChanged );
  filterRow->addWidget( m_filterEdit, 1 );
  groupLayout->addLayout( filterRow );

  m_preflightLabel = SicnuUi::makeHintLabel( inputGroup, tr( "尚未预检：点击“预检”检查时间/网格/辐射一致性。" ) );
  groupLayout->addWidget( m_preflightLabel );

  // ---- algorithm + parameters ----
  QGroupBox *paramGroup = setupParamGroup( mainLayout, tr( "分析与参数" ) );
  auto *paramLayout = new QVBoxLayout( paramGroup );
  paramLayout->setContentsMargins( 10, 8, 10, 8 );
  paramLayout->setSpacing( 8 );

  auto *algRow = new QHBoxLayout();
  algRow->addWidget( new QLabel( tr( "分析：" ), paramGroup ) );
  m_algorithmCombo = new QComboBox( paramGroup );
  m_algorithmCombo->setObjectName( QStringLiteral( "temporalAlgorithmCombo" ) );
  for ( const auto &entry : kAlgorithms )
    m_algorithmCombo->addItem( tr( entry.label ), QString::fromLatin1( entry.id ) );
  SicnuDialogHelp::tip( m_algorithmCombo, tr( "时间序列分析算法（经处理注册表执行，可在工具箱/Agent 中复用）。" ) );
  connect( m_algorithmCombo, &QComboBox::currentIndexChanged, this, &TemporalAnalysisDialog::algorithmChanged );
  algRow->addWidget( m_algorithmCombo, 1 );

  algRow->addWidget( new QLabel( tr( "波段角色：" ), paramGroup ) );
  m_bandRoleCombo = new QComboBox( paramGroup );
  m_bandRoleCombo->setObjectName( QStringLiteral( "temporalBandRoleCombo" ) );
  m_bandRoleCombo->addItem( tr( "波段 1" ), QString() );
  m_bandRoleCombo->addItem( QStringLiteral( "blue" ), QStringLiteral( "blue" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "green" ), QStringLiteral( "green" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "red" ), QStringLiteral( "red" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "red_edge" ), QStringLiteral( "red_edge" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "nir" ), QStringLiteral( "nir" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "swir1" ), QStringLiteral( "swir1" ) );
  m_bandRoleCombo->addItem( QStringLiteral( "swir2" ), QStringLiteral( "swir2" ) );
  SicnuDialogHelp::tip( m_bandRoleCombo, tr( "分析波段按语义角色解析（元数据优先，缺省按常规顺序回退并警告）。" ) );
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
      page, tr( "输出波段：count / valid_count / mean / min / max / stddev。勾选中值时按内存预算自动缩小分块（精确值，非近似）。" ) ) );
    m_medianCheck = new QCheckBox( tr( "包含中值 (median)" ), page );
    m_medianCheck->setObjectName( QStringLiteral( "temporalMedianCheck" ) );
    SicnuDialogHelp::tip( m_medianCheck, tr( "逐像元精确中值；长时序自动减小 tile 尺寸以满足内存预算。" ) );
    lay->addWidget( m_medianCheck );
    m_paramStack->addWidget( page );
  }
  // page: composite
  {
    auto *page = new QFrame( m_paramStack );
    auto *lay = new QHBoxLayout( page );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->addWidget( new QLabel( tr( "方法：" ), page ) );
    m_compositeMethodCombo = new QComboBox( page );
    m_compositeMethodCombo->setObjectName( QStringLiteral( "temporalCompositeMethod" ) );
    m_compositeMethodCombo->addItem( tr( "最佳像元" ), QStringLiteral( "best_pixel" ) );
    m_compositeMethodCombo->addItem( tr( "均值" ), QStringLiteral( "mean" ) );
    m_compositeMethodCombo->addItem( tr( "中值" ), QStringLiteral( "median" ) );
    SicnuDialogHelp::tip( m_compositeMethodCombo,
                          tr( "最佳像元：有效观测中质量分最高（并列取最接近目标日期，再取更早时相）；"
                              "输出含有效观测数与质量分波段。" ) );
    lay->addWidget( m_compositeMethodCombo );
    lay->addWidget( new QLabel( tr( "周期：" ), page ) );
    m_periodCombo = new QComboBox( page );
    m_periodCombo->setObjectName( QStringLiteral( "temporalPeriodCombo" ) );
    m_periodCombo->addItem( tr( "全部" ), QStringLiteral( "all" ) );
    m_periodCombo->addItem( tr( "逐月" ), QStringLiteral( "month" ) );
    m_periodCombo->addItem( tr( "逐季" ), QStringLiteral( "quarter" ) );
    m_periodCombo->addItem( tr( "季节" ), QStringLiteral( "season" ) );
    m_periodCombo->addItem( tr( "逐年" ), QStringLiteral( "year" ) );
    SicnuDialogHelp::tip( m_periodCombo, tr( "按周期分组时每个周期输出一个文件（后缀为起始日期）。" ) );
    lay->addWidget( m_periodCombo );
    lay->addStretch();
    m_paramStack->addWidget( page );
  }
  // page: index series
  {
    auto *page = new QFrame( m_paramStack );
    auto *lay = new QHBoxLayout( page );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->addWidget( new QLabel( tr( "指数：" ), page ) );
    m_indexCombo = new QComboBox( page );
    m_indexCombo->setObjectName( QStringLiteral( "temporalIndexCombo" ) );
    for ( const char *idx : { "NDVI", "EVI", "SAVI", "NDWI", "NDBI", "MNDWI", "NBR", "NDRE", "NDSI", "NDTI" } )
      m_indexCombo->addItem( QString::fromLatin1( idx ), QString::fromLatin1( idx ) );
    SicnuDialogHelp::tip( m_indexCombo,
                          tr( "与单景光谱指数相同的计算内核；输出为逐日期一个波段的栈（保留获取时间元数据）。" ) );
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
      page, tr( "输出：slope（每天）/ intercept / R² / n / RMSE。回归使用真实获取时间间隔，斜率×365.25 = 年变化率。" ) ) );
    m_paramStack->addWidget( page );
  }
  // page: anomaly
  {
    auto *page = new QFrame( m_paramStack );
    auto *lay = new QHBoxLayout( page );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->addWidget( new QLabel( tr( "方法：" ), page ) );
    m_anomalyMethodCombo = new QComboBox( page );
    m_anomalyMethodCombo->setObjectName( QStringLiteral( "temporalAnomalyMethod" ) );
    m_anomalyMethodCombo->addItem( tr( "z-score" ), QStringLiteral( "zscore" ) );
    m_anomalyMethodCombo->addItem( tr( "与基线均值之差" ), QStringLiteral( "difference" ) );
    SicnuDialogHelp::tip( m_anomalyMethodCombo,
                          tr( "基线默认为除目标时相外的全部时相；可用参数 baseline_start/end 缩小。" ) );
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
    grid->addWidget( new QLabel( tr( "点 (x, y)：" ), page ), 0, 0 );
    m_pointEdit = new QLineEdit( page );
    m_pointEdit->setObjectName( QStringLiteral( "temporalPointEdit" ) );
    m_pointEdit->setPlaceholderText( tr( "地图坐标，例如 460000.5, 3390020.25（与点/多边形二选一）" ) );
    SicnuDialogHelp::tip( m_pointEdit, tr( "点坐标须与时相集合同一坐标系。" ) );
    grid->addWidget( m_pointEdit, 0, 1 );
    grid->addWidget( new QLabel( tr( "多边形 ROI：" ), page ), 1, 0 );
    m_polygonEdit = new QLineEdit( page );
    m_polygonEdit->setObjectName( QStringLiteral( "temporalPolygonEdit" ) );
    m_polygonEdit->setPlaceholderText( tr( "顶点串 x1,y1;x2,y2;x3,y3;…（闭合环，仅扫描包围盒）" ) );
    SicnuDialogHelp::tip( m_polygonEdit, tr( "ROI 统计：mean/median/min/max/stddev/valid_count，按日期输出 CSV。" ) );
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
    this, tr( "添加时相栅格" ), QString(),
    tr( "栅格 (*.tif *.tiff *.img *.asc);;所有文件 (*)" ) );
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
    timeItem->setToolTip( tr( "可编辑：YYYY-MM-DD 或 ISO 时间戳；留空表示未知（预检将拒绝）。" ) );
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
      m_sceneTable->item( row, ColStatus )->setText( tr( "无法打开" ) );
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
      m_sceneTable->item( row, ColStatus )->setText( tr( "网格未知" ) );
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
    status << ( gridOk ? tr( "网格一致" ) : tr( "网格不一致" ) );
    const QString timeText = m_sceneTable->item( row, ColTime )->text().trimmed();
    if ( timeText.isEmpty() )
      status << tr( "缺时间" );
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
    QMessageBox::warning( this, dialogTitle(), tr( "请先添加时相场景。" ) );
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
  summary << tr( "时相: %1" ).arg( report.sceneCount );
  if ( !collection.timeRangeStartIso().isEmpty() )
    summary << tr( "区间: %1 → %2" )
                   .arg( collection.timeRangeStartIso(), collection.timeRangeEndIso() );
  summary << ( report.gridCompatible ? tr( "网格: 一致" ) : tr( "网格: 不一致" ) );
  summary << tr( "辐射态: %1" )
                 .arg( report.commonRadiometricState.isEmpty()
                           ? tr( "未知（警告）" )
                           : report.commonRadiometricState );
  int blocking = 0;
  for ( const auto &issue : report.issues )
    if ( issue.blocking )
      ++blocking;
  summary << ( blocking == 0 ? tr( "预检通过" )
                             : tr( "预检失败： %1 个阻断问题" ).arg( blocking ) );
  m_preflightLabel->setText( summary.join( QStringLiteral( "  |  " ) ) );

  if ( !report.ok() )
  {
    QStringList lines;
    for ( const auto &issue : report.issues )
      if ( issue.blocking )
        lines << QStringLiteral( "[%1] %2" ).arg( issue.code, issue.message );
    QMessageBox::warning( this, tr( "时间序列预检" ),
                          lines.join( QLatin1Char( '\n' ) ).left( 2000 ) );
  }
}

bool TemporalAnalysisDialog::validateInputs()
{
  if ( m_sceneTable->rowCount() < 2 )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "时间序列分析至少需要 2 个时相。" ) );
    return false;
  }
  for ( int row = 0; row < m_sceneTable->rowCount(); ++row )
  {
    if ( m_sceneTable->item( row, ColTime )->text().trimmed().isEmpty() )
    {
      QMessageBox::warning( this, dialogTitle(),
                            tr( "第 %1 行缺少获取时间（产品元数据/文件名未解析出，请手动填写）。" ).arg( row + 1 ) );
      return false;
    }
  }
  if ( outputPath().isEmpty() )
  {
    QMessageBox::warning( this, dialogTitle(), tr( "请指定输出文件路径。" ) );
    return false;
  }
  const QString id = m_algorithmCombo->currentData().toString();
  if ( id == QLatin1String( "rs:temporal_extract_series" ) )
  {
    const bool hasPoint = !m_pointEdit->text().trimmed().isEmpty();
    const bool hasPoly = !m_polygonEdit->text().trimmed().isEmpty();
    if ( hasPoint == hasPoly )
    {
      QMessageBox::warning( this, dialogTitle(), tr( "序列提取需要且只能填写 点 或 多边形 之一。" ) );
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
    m_sceneTable->setItem( row, ColStatus, new QTableWidgetItem( tr( "已加载" ) ) );
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
    QString colName = tr( "时序分析集合 %1" ).arg( QDateTime::currentDateTime().toString( QStringLiteral( "yyyy-MM-dd hh:mm" ) ) );
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
        QMessageBox::warning( this, dialogTitle(), tr( "点坐标格式应为 x,y。" ) );
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
          QMessageBox::warning( this, dialogTitle(), tr( "多边形顶点格式应为 x,y（分号分隔）。" ) );
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
