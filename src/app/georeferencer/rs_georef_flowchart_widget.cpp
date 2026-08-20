// rs_georef_flowchart_widget.cpp — Interactive flowchart panel for Geometric Correction workflow.
#include "rs_georef_flowchart_widget.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

RsGeorefFlowchartWidget::RsGeorefFlowchartWidget( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsGeorefFlowchartWidget" ) );
  setupUi();
}

void RsGeorefFlowchartWidget::setupUi()
{
  auto *rootLayout = new QVBoxLayout( this );
  rootLayout->setContentsMargins( 8, 8, 8, 8 );
  rootLayout->setSpacing( 6 );

  // --- Header Area ---
  auto *headerFrame = new QFrame( this );
  headerFrame->setObjectName( QStringLiteral( "georefFlowchartHeader" ) );
  headerFrame->setFrameShape( QFrame::StyledPanel );
  headerFrame->setStyleSheet(
    QStringLiteral( "QFrame#georefFlowchartHeader { "
                    "  background: palette(window); "
                    "  border: 1px solid palette(midlight); "
                    "  border-radius: 6px; "
                    "  padding: 4px; "
                    "}" ) );
  auto *headerLayout = new QVBoxLayout( headerFrame );
  headerLayout->setContentsMargins( 6, 6, 6, 6 );
  headerLayout->setSpacing( 4 );

  auto *titleRow = new QHBoxLayout();
  auto *titleIcon = new QLabel( QStringLiteral( "🌐" ), headerFrame );
  auto *titleText = new QLabel( tr( "遥感影像几何校正流程" ), headerFrame );
  QFont titleFont = titleText->font();
  titleFont.setBold( true );
  titleFont.setPointSize( titleFont.pointSize() + 1 );
  titleText->setFont( titleFont );
  titleRow->addWidget( titleIcon );
  titleRow->addWidget( titleText );
  titleRow->addStretch( 1 );
  headerLayout->addLayout( titleRow );

  m_progressLabel = new QLabel( tr( "流程进度: 0/7 步已就绪 (0%)" ), headerFrame );
  m_progressLabel->setStyleSheet( QStringLiteral( "color: palette(placeholder-text); font-size: 11px;" ) );
  headerLayout->addWidget( m_progressLabel );

  m_progressBar = new QProgressBar( headerFrame );
  m_progressBar->setRange( 0, static_cast<int>( FlowStep::Count ) );
  m_progressBar->setValue( 0 );
  m_progressBar->setTextVisible( false );
  m_progressBar->setFixedHeight( 6 );
  m_progressBar->setStyleSheet(
    QStringLiteral( "QProgressBar { "
                    "  background-color: palette(midlight); "
                    "  border-radius: 3px; "
                    "} "
                    "QProgressBar::chunk { "
                    "  background-color: #1565c0; "
                    "  border-radius: 3px; "
                    "}" ) );
  headerLayout->addWidget( m_progressBar );

  rootLayout->addWidget( headerFrame );

  // --- Scroll Area for Flowchart Steps ---
  m_scrollArea = new QScrollArea( this );
  m_scrollArea->setWidgetResizable( true );
  m_scrollArea->setFrameShape( QFrame::NoFrame );
  m_scrollArea->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );

  m_cardContainer = new QWidget( m_scrollArea );
  m_cardsLayout = new QVBoxLayout( m_cardContainer );
  m_cardsLayout->setContentsMargins( 2, 4, 2, 4 );
  m_cardsLayout->setSpacing( 4 );

  // Define 7 Steps
  struct StepMeta {
    FlowStep step;
    QString num;
    QString title;
    QString desc;
    QString action;
  };

  const StepMeta steps[] = {
    { FlowStep::LoadSource, QStringLiteral( "1" ), tr( "加载源影像" ),
      tr( "打开待几何校正/配准的原始遥感栅格影像" ), tr( "打开影像" ) },
    { FlowStep::CollectGcps, QStringLiteral( "2" ), tr( "采集控制点 (GCP)" ),
      tr( "在源影像与参考底图上采集同名控制点对，或使用自动匹配" ), tr( "控制点采集" ) },
    { FlowStep::SelectModel, QStringLiteral( "3" ), tr( "选择变换模型" ),
      tr( "设定几何校正数学模型（多项式1-3阶/线性/Helmert/薄板样条/RPC）" ), tr( "模型参数" ) },
    { FlowStep::CheckResiduals, QStringLiteral( "4" ), tr( "残差与精度检查" ),
      tr( "计算控制点像元残差(dx, dy)与总 RMS 均方根误差，剔除粗差点" ), tr( "残差检查" ) },
    { FlowStep::ConfigureWarp, QStringLiteral( "5" ), tr( "配置校正参数" ),
      tr( "指定目标坐标系(CRS)、像元分辨率、重采样方法与输出路径" ), tr( "输出配置" ) },
    { FlowStep::ExecuteWarp, QStringLiteral( "6" ), tr( "执行重采样校正" ),
      tr( "启动后台多线程重采样变换引擎，生成几何纠正后栅格" ), tr( "开始校正" ) },
    { FlowStep::VerifyResult, QStringLiteral( "7" ), tr( "成果加载与验证" ),
      tr( "将纠正后的栅格加载至主地图画布，与基准底图进行空间叠加比对" ), tr( "加载成果" ) }
  };

  m_cards.resize( static_cast<int>( FlowStep::Count ) );

  for ( int i = 0; i < static_cast<int>( FlowStep::Count ); ++i )
  {
    const auto &s = steps[i];
    QFrame *card = createStepCard( s.step, s.num, s.title, s.desc, s.action );
    m_cardsLayout->addWidget( card );

    // Connector line / arrow between steps
    if ( i < static_cast<int>( FlowStep::Count ) - 1 )
    {
      auto *arrowLabel = new QLabel( QStringLiteral( "↓" ), m_cardContainer );
      arrowLabel->setAlignment( Qt::AlignCenter );
      arrowLabel->setStyleSheet( QStringLiteral( "color: palette(mid); font-size: 13px; font-weight: bold; margin: -2px 0px;" ) );
      m_cardsLayout->addWidget( arrowLabel );
    }
  }

  m_cardsLayout->addStretch( 1 );
  m_scrollArea->setWidget( m_cardContainer );
  rootLayout->addWidget( m_scrollArea, 1 );

  refreshState();
}

QFrame *RsGeorefFlowchartWidget::createStepCard( FlowStep step, const QString &num,
                                                 const QString &title, const QString &desc,
                                                 const QString &actionText )
{
  auto *card = new QFrame( m_cardContainer );
  card->setObjectName( QStringLiteral( "georefStepCard_%1" ).arg( static_cast<int>( step ) ) );
  card->setFrameShape( QFrame::StyledPanel );
  card->setCursor( Qt::PointingHandCursor );

  auto *cardLay = new QVBoxLayout( card );
  cardLay->setContentsMargins( 8, 6, 8, 6 );
  cardLay->setSpacing( 4 );

  // Top line: Number badge + Title + Status badge
  auto *topRow = new QHBoxLayout();
  topRow->setSpacing( 6 );

  auto *numBadge = new QLabel( num, card );
  numBadge->setFixedSize( 20, 20 );
  numBadge->setAlignment( Qt::AlignCenter );
  numBadge->setStyleSheet(
    QStringLiteral( "background-color: palette(midlight); "
                    "color: palette(text); "
                    "border-radius: 10px; "
                    "font-weight: bold; "
                    "font-size: 11px;" ) );
  topRow->addWidget( numBadge );

  auto *titleLbl = new QLabel( title, card );
  QFont tf = titleLbl->font();
  tf.setBold( true );
  titleLbl->setFont( tf );
  topRow->addWidget( titleLbl, 1 );

  auto *statusBadge = new QLabel( tr( "未开始" ), card );
  statusBadge->setStyleSheet(
    QStringLiteral( "background: palette(midlight); "
                    "color: palette(placeholder-text); "
                    "border-radius: 4px; "
                    "padding: 1px 6px; "
                    "font-size: 10px;" ) );
  topRow->addWidget( statusBadge );

  cardLay->addLayout( topRow );

  // Description
  auto *descLbl = new QLabel( desc, card );
  descLbl->setWordWrap( true );
  descLbl->setStyleSheet( QStringLiteral( "color: palette(placeholder-text); font-size: 11px;" ) );
  cardLay->addWidget( descLbl );

  // Bottom line: Dynamic metric tag + Action button
  auto *bottomRow = new QHBoxLayout();
  bottomRow->setSpacing( 4 );

  auto *metricLbl = new QLabel( card );
  metricLbl->setStyleSheet( QStringLiteral( "color: #0288d1; font-weight: 500; font-size: 11px;" ) );
  bottomRow->addWidget( metricLbl, 1 );

  auto *actBtn = new QPushButton( actionText, card );
  actBtn->setFixedHeight( 24 );
  actBtn->setStyleSheet(
    QStringLiteral( "QPushButton { "
                    "  font-size: 11px; "
                    "  padding: 2px 8px; "
                    "  border-radius: 4px; "
                    "  border: 1px solid palette(mid); "
                    "  background: palette(button); "
                    "} "
                    "QPushButton:hover { "
                    "  background: palette(midlight); "
                    "}" ) );
  bottomRow->addWidget( actBtn );

  cardLay->addLayout( bottomRow );

  const int idx = static_cast<int>( step );
  StepCard sc;
  sc.step = step;
  sc.cardFrame = card;
  sc.numberLabel = numBadge;
  sc.titleLabel = titleLbl;
  sc.statusBadge = statusBadge;
  sc.detailLabel = descLbl;
  sc.metricLabel = metricLbl;
  sc.actionBtn = actBtn;
  sc.isComplete = false;
  sc.isActive = ( step == m_activeStep );
  m_cards[idx] = sc;

  // Connect button click
  connect( actBtn, &QPushButton::clicked, this, [this, step]() {
    emit stepClicked( step );
    switch ( step )
    {
      case FlowStep::LoadSource:
        emit openSourceRequested();
        break;
      case FlowStep::CollectGcps:
        emit collectGcpsRequested();
        break;
      case FlowStep::SelectModel:
        emit selectModelRequested();
        break;
      case FlowStep::CheckResiduals:
        emit checkResidualsRequested();
        break;
      case FlowStep::ConfigureWarp:
        emit configWarpRequested();
        break;
      case FlowStep::ExecuteWarp:
        emit executeWarpRequested();
        break;
      case FlowStep::VerifyResult:
        emit loadResultRequested();
        break;
      default:
        break;
    }
  } );

  return card;
}

void RsGeorefFlowchartWidget::bindSession( RsGeoreferencingSession *session )
{
  m_session = session;
  if ( !m_session )
    return;

  connect( m_session, &RsGeoreferencingSession::gcpsChanged,
           this, &RsGeorefFlowchartWidget::refreshState );
  connect( m_session, &RsGeoreferencingSession::fitChanged,
           this, &RsGeorefFlowchartWidget::refreshState );
  connect( m_session, &RsGeoreferencingSession::warpFinished,
           this, [this]( long, bool success, const QString &, const QString &outPath ) {
             if ( success && !outPath.isEmpty() )
             {
               setOutputInfo( outPath, true );
             }
             refreshState();
           } );

  refreshState();
}

void RsGeorefFlowchartWidget::setActiveStep( FlowStep step )
{
  m_activeStep = step;
  for ( int i = 0; i < m_cards.size(); ++i )
  {
    m_cards[i].isActive = ( m_cards[i].step == step );
    updateCardStyle( m_cards[i] );
  }
}

void RsGeorefFlowchartWidget::setSourceRasterInfo( const QString &sourcePath, int width,
                                                   int height, int bands )
{
  m_hasSource = !sourcePath.isEmpty();
  if ( m_hasSource )
  {
    QFileInfo fi( sourcePath );
    if ( width > 0 && height > 0 )
      m_sourceText = QStringLiteral( "%1 (%2×%3, %4波段)" )
                       .arg( fi.fileName() )
                       .arg( width )
                       .arg( height )
                       .arg( bands );
    else
      m_sourceText = fi.fileName();
  }
  else
  {
    m_sourceText = tr( "未加载影像" );
  }
  refreshState();
}

void RsGeorefFlowchartWidget::setGcpInfo( int totalGcps, int enabledGcps )
{
  if ( totalGcps > 0 )
    m_gcpText = tr( "%1 个 GCP (启用 %2 点)" ).arg( totalGcps ).arg( enabledGcps );
  else
    m_gcpText = tr( "0 个 GCP (启用 0)" );
  refreshState();
}

void RsGeorefFlowchartWidget::setModelInfo( const QString &methodName, int minGcpRequired )
{
  if ( !methodName.isEmpty() )
    m_modelText = QStringLiteral( "%1 (需 ≥%2 点)" ).arg( methodName ).arg( minGcpRequired );
  refreshState();
}

void RsGeorefFlowchartWidget::setResidualInfo( double rmsPixels, bool isFitReady, const QString &statusText )
{
  if ( isFitReady && rmsPixels >= 0.0 )
  {
    QString grade = ( rmsPixels <= 0.5 ) ? tr( " (优)" )
                  : ( rmsPixels <= 1.0 ) ? tr( " (良好)" )
                                         : tr( " (需优化)" );
    m_residualText = QStringLiteral( "RMS: %1 px%2" ).arg( rmsPixels, 0, 'f', 2 ).arg( grade );
  }
  else if ( !statusText.isEmpty() )
  {
    m_residualText = statusText;
  }
  else
  {
    m_residualText = tr( "未解算" );
  }
  refreshState();
}

void RsGeorefFlowchartWidget::setWarpConfigInfo( const QString &destCrs, const QString &resampling, double pixelSize )
{
  QStringList parts;
  if ( !destCrs.isEmpty() )
    parts.append( destCrs );
  if ( !resampling.isEmpty() )
    parts.append( resampling );
  if ( pixelSize > 0.0 )
    parts.append( QStringLiteral( "%1 m" ).arg( pixelSize ) );

  if ( !parts.isEmpty() )
    m_warpConfigText = parts.join( QStringLiteral( " / " ) );
  else
    m_warpConfigText = tr( "未配置参数" );
  refreshState();
}

void RsGeorefFlowchartWidget::setWarpExecutionInfo( bool isRunning, const QString &statusText )
{
  if ( isRunning )
    m_warpExecText = tr( "校正任务运行中…" );
  else if ( !statusText.isEmpty() )
    m_warpExecText = statusText;
  else
    m_warpExecText = tr( "就绪，等待执行" );
  refreshState();
}

void RsGeorefFlowchartWidget::setOutputInfo( const QString &outputPath, bool isLoaded )
{
  m_hasOutput = !outputPath.isEmpty();
  if ( m_hasOutput )
  {
    QFileInfo fi( outputPath );
    m_verifyText = isLoaded ? tr( "已加载至主地图: %1" ).arg( fi.fileName() )
                            : tr( "已输出: %1" ).arg( fi.fileName() );
  }
  else
  {
    m_verifyText = tr( "未加载成果" );
  }
  refreshState();
}

void RsGeorefFlowchartWidget::refreshState()
{
  int enabledGcps = 0;
  int totalGcps = 0;
  bool fitReady = false;
  double rms = -1.0;
  int minGcp = 3;

  if ( m_session )
  {
    totalGcps = m_session->gcps().size();
    enabledGcps = QgsGeorefTransform::enabledGcpCount( m_session->gcps() );
    minGcp = QgsGeorefTransform::minimumGcpCountFor( m_session->transformMethod() );
    fitReady = m_session->isFitReady();
    rms = m_session->lastFit().rms;
    if ( !m_session->sourceRasterPath().isEmpty() )
      m_hasSource = true;
  }

  // Card 0: Load Source
  const int idx0 = static_cast<int>( FlowStep::LoadSource );
  if ( idx0 < m_cards.size() )
  {
    m_cards[idx0].isComplete = m_hasSource;
    m_cards[idx0].metricLabel->setText( m_sourceText );
    updateCardStyle( m_cards[idx0] );
  }

  // Card 1: Collect GCPs
  const int idx1 = static_cast<int>( FlowStep::CollectGcps );
  if ( idx1 < m_cards.size() )
  {
    m_cards[idx1].isComplete = ( enabledGcps >= minGcp );
    if ( totalGcps > 0 )
      m_gcpText = tr( "%1 个 GCP (启用 %2 点)" ).arg( totalGcps ).arg( enabledGcps );
    m_cards[idx1].metricLabel->setText( m_gcpText );
    updateCardStyle( m_cards[idx1] );
  }

  // Card 2: Select Model
  const int idx2 = static_cast<int>( FlowStep::SelectModel );
  if ( idx2 < m_cards.size() )
  {
    m_cards[idx2].isComplete = m_hasSource && ( enabledGcps >= minGcp );
    m_cards[idx2].metricLabel->setText( m_modelText );
    updateCardStyle( m_cards[idx2] );
  }

  // Card 3: Check Residuals
  const int idx3 = static_cast<int>( FlowStep::CheckResiduals );
  if ( idx3 < m_cards.size() )
  {
    m_cards[idx3].isComplete = fitReady && ( rms >= 0.0 );
    if ( fitReady && rms >= 0.0 )
    {
      QString grade = ( rms <= 0.5 ) ? tr( " (优)" )
                    : ( rms <= 1.0 ) ? tr( " (良好)" )
                                           : tr( " (需优化)" );
      m_residualText = QStringLiteral( "RMS: %1 px%2" ).arg( rms, 0, 'f', 2 ).arg( grade );
    }
    m_cards[idx3].metricLabel->setText( m_residualText );
    updateCardStyle( m_cards[idx3] );
  }

  // Card 4: Configure Warp
  const int idx4 = static_cast<int>( FlowStep::ConfigureWarp );
  if ( idx4 < m_cards.size() )
  {
    m_cards[idx4].isComplete = fitReady;
    m_cards[idx4].metricLabel->setText( m_warpConfigText );
    updateCardStyle( m_cards[idx4] );
  }

  // Card 5: Execute Warp
  const int idx5 = static_cast<int>( FlowStep::ExecuteWarp );
  if ( idx5 < m_cards.size() )
  {
    m_cards[idx5].isComplete = m_hasOutput;
    m_cards[idx5].metricLabel->setText( m_warpExecText );
    updateCardStyle( m_cards[idx5] );
  }

  // Card 6: Verify Result
  const int idx6 = static_cast<int>( FlowStep::VerifyResult );
  if ( idx6 < m_cards.size() )
  {
    m_cards[idx6].isComplete = m_hasOutput;
    m_cards[idx6].metricLabel->setText( m_verifyText );
    updateCardStyle( m_cards[idx6] );
  }

  updateOverallProgress();
}

void RsGeorefFlowchartWidget::updateCardStyle( StepCard &card )
{
  if ( !card.cardFrame )
    return;

  QString borderStyle;
  QString statusText;
  QString statusStyle;
  QString badgeStyle;

  if ( card.isComplete )
  {
    borderStyle = QStringLiteral( "border: 1px solid #4caf50; background: palette(window);" );
    statusText = tr( "✓ 已就绪" );
    statusStyle = QStringLiteral( "background: #e8f5e9; color: #2e7d32; border-radius: 4px; padding: 1px 6px; font-size: 10px; font-weight: bold;" );
    badgeStyle = QStringLiteral( "background: #4caf50; color: white; border-radius: 10px; font-weight: bold; font-size: 11px;" );
  }
  else if ( card.isActive )
  {
    borderStyle = QStringLiteral( "border: 2px solid #0288d1; background: palette(window);" );
    statusText = tr( "● 当前步骤" );
    statusStyle = QStringLiteral( "background: #e1f5fe; color: #0277bd; border-radius: 4px; padding: 1px 6px; font-size: 10px; font-weight: bold;" );
    badgeStyle = QStringLiteral( "background: #0288d1; color: white; border-radius: 10px; font-weight: bold; font-size: 11px;" );
  }
  else
  {
    borderStyle = QStringLiteral( "border: 1px solid palette(midlight); background: palette(window);" );
    statusText = tr( "未就绪" );
    statusStyle = QStringLiteral( "background: palette(midlight); color: palette(placeholder-text); border-radius: 4px; padding: 1px 6px; font-size: 10px;" );
    badgeStyle = QStringLiteral( "background: palette(midlight); color: palette(text); border-radius: 10px; font-weight: bold; font-size: 11px;" );
  }

  card.cardFrame->setStyleSheet(
    QStringLiteral( "QFrame#%1 { %2 border-radius: 6px; }" )
      .arg( card.cardFrame->objectName() )
      .arg( borderStyle ) );
  card.statusBadge->setText( statusText );
  card.statusBadge->setStyleSheet( statusStyle );
  card.numberLabel->setStyleSheet( badgeStyle );
}

void RsGeorefFlowchartWidget::updateOverallProgress()
{
  int completed = 0;
  for ( const auto &c : m_cards )
  {
    if ( c.isComplete )
      ++completed;
  }

  const int total = m_cards.size();
  const int percent = total > 0 ? ( completed * 100 / total ) : 0;
  m_progressBar->setValue( completed );
  m_progressLabel->setText(
    tr( "流程进度: %1/%2 步已就绪 (%3%)" ).arg( completed ).arg( total ).arg( percent ) );
}
