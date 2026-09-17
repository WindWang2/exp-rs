// rs_georef_flowchart_widget.cpp — Interactive flowchart panel for Geometric Correction workflow.
#include "rs_georef_flowchart_widget.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
  // Dynamic QSS properties only re-resolve after a repolish.
  void setFlowProp( QWidget *w, const char *prop, const QString &value )
  {
    if ( w->property( prop ).toString() == value )
      return;
    w->setProperty( prop, value );
    w->style()->unpolish( w );
    w->style()->polish( w );
  }
}

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
  auto *titleText = new QLabel( tr( "Remote-Sensing Geometric Correction Pipeline" ), headerFrame );
  QFont titleFont = titleText->font();
  titleFont.setBold( true );
  titleFont.setPointSize( titleFont.pointSize() + 1 );
  titleText->setFont( titleFont );
  titleRow->addWidget( titleIcon );
  titleRow->addWidget( titleText );
  titleRow->addStretch( 1 );
  headerLayout->addLayout( titleRow );

  m_progressLabel = new QLabel( tr( "Pipeline progress: 0/7 steps ready (0%)" ), headerFrame );
  m_progressLabel->setStyleSheet( QStringLiteral( "color: palette(placeholder-text); font-size: 11px;" ) );
  headerLayout->addWidget( m_progressLabel );

  m_progressBar = new QProgressBar( headerFrame );
  m_progressBar->setObjectName( QStringLiteral( "rsFlowProgress" ) );
  m_progressBar->setRange( 0, static_cast<int>( FlowStep::Count ) );
  m_progressBar->setValue( 0 );
  m_progressBar->setTextVisible( false );
  m_progressBar->setFixedHeight( 6 );
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
    { FlowStep::LoadSource, QStringLiteral( "1" ), tr( "Load Source Image" ),
      tr( "Open the raw remote-sensing raster to be geometrically corrected / registered" ), tr( "Open Image" ) },
    { FlowStep::CollectGcps, QStringLiteral( "2" ), tr( "Collect Control Points (GCPs)" ),
      tr( "Collect conjugate control point pairs on the source image and reference base map, or use auto matching" ), tr( "Control Point Collection" ) },
    { FlowStep::SelectModel, QStringLiteral( "3" ), tr( "Select Transform Model" ),
      tr( "Set the geometric correction model (polynomial order 1–3 / linear / Helmert / thin plate spline / RPC)" ), tr( "Model Parameters" ) },
    { FlowStep::CheckResiduals, QStringLiteral( "4" ), tr( "Residual and Accuracy Check" ),
      tr( "Compute control point pixel residuals (dx, dy) and the total RMS, rejecting gross errors" ), tr( "Residual Check" ) },
    { FlowStep::ConfigureWarp, QStringLiteral( "5" ), tr( "Configure Correction Parameters" ),
      tr( "Specify the target CRS, pixel resolution, resampling method and output path" ), tr( "Output Settings" ) },
    { FlowStep::ExecuteWarp, QStringLiteral( "6" ), tr( "Run Resampling Correction" ),
      tr( "Starts the background multi-threaded resampling engine to produce the corrected raster" ), tr( "Start Correction" ) },
    { FlowStep::VerifyResult, QStringLiteral( "7" ), tr( "Result Loading and Verification" ),
      tr( "Load the corrected raster onto the main map canvas for spatial overlay against the base map" ), tr( "Load Results" ) }
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
  numBadge->setProperty( "flowNum", QStringLiteral( "idle" ) );
  topRow->addWidget( numBadge );

  auto *titleLbl = new QLabel( title, card );
  QFont tf = titleLbl->font();
  tf.setBold( true );
  titleLbl->setFont( tf );
  topRow->addWidget( titleLbl, 1 );

  auto *statusBadge = new QLabel( tr( "Not started" ), card );
  statusBadge->setProperty( "flowBadge", QStringLiteral( "idle" ) );
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
  metricLbl->setProperty( "rsTone", QStringLiteral( "info" ) );
  metricLbl->setStyleSheet( QStringLiteral( "font-weight: 500; font-size: 11px;" ) );
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
      m_sourceText = tr("%1 (%2×%3, %4 bands)" )
                       .arg( fi.fileName() )
                       .arg( width )
                       .arg( height )
                       .arg( bands );
    else
      m_sourceText = fi.fileName();
  }
  else
  {
    m_sourceText = tr( "No image loaded" );
  }
  refreshState();
}

void RsGeorefFlowchartWidget::setGcpInfo( int totalGcps, int enabledGcps )
{
  if ( totalGcps > 0 )
    m_gcpText = tr( "%1 GCPs (%2 enabled)" ).arg( totalGcps ).arg( enabledGcps );
  else
    m_gcpText = tr( "0 GCPs (0 enabled)" );
  refreshState();
}

void RsGeorefFlowchartWidget::setModelInfo( const QString &methodName, int minGcpRequired )
{
  if ( !methodName.isEmpty() )
    m_modelText = tr("%1 (needs ≥ %2 points)" ).arg( methodName ).arg( minGcpRequired );
  refreshState();
}

void RsGeorefFlowchartWidget::setResidualInfo( double rmsPixels, bool isFitReady, const QString &statusText )
{
  if ( isFitReady && rmsPixels >= 0.0 )
  {
    QString grade = ( rmsPixels <= 0.5 ) ? tr( " (excellent)" )
                  : ( rmsPixels <= 1.0 ) ? tr( " (good)" )
                                         : tr( " (needs tuning)" );
    m_residualText = QStringLiteral( "RMS: %1 px%2" ).arg( rmsPixels, 0, 'f', 2 ).arg( grade );
  }
  else if ( !statusText.isEmpty() )
  {
    m_residualText = statusText;
  }
  else
  {
    m_residualText = tr( "Unsolved" );
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
    m_warpConfigText = tr( "Parameters not configured" );
  refreshState();
}

void RsGeorefFlowchartWidget::setWarpExecutionInfo( bool isRunning, const QString &statusText )
{
  if ( isRunning )
    m_warpExecText = tr( "Correction task running..." );
  else if ( !statusText.isEmpty() )
    m_warpExecText = statusText;
  else
    m_warpExecText = tr( "Ready, waiting to run" );
  refreshState();
}

void RsGeorefFlowchartWidget::setOutputInfo( const QString &outputPath, bool isLoaded )
{
  m_hasOutput = !outputPath.isEmpty();
  if ( m_hasOutput )
  {
    QFileInfo fi( outputPath );
    m_verifyText = isLoaded ? tr( "Loaded into the main map: %1" ).arg( fi.fileName() )
                            : tr( "Output: %1" ).arg( fi.fileName() );
  }
  else
  {
    m_verifyText = tr( "No results loaded" );
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
      m_gcpText = tr( "%1 GCPs (%2 enabled)" ).arg( totalGcps ).arg( enabledGcps );
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
      QString grade = ( rms <= 0.5 ) ? tr( " (excellent)" )
                    : ( rms <= 1.0 ) ? tr( " (good)" )
                                           : tr( " (needs tuning)" );
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

  QString state;
  QString statusText;
  if ( card.isComplete )
  {
    state = QStringLiteral( "done" );
    statusText = tr( "✓ Ready" );
  }
  else if ( card.isActive )
  {
    state = QStringLiteral( "active" );
    statusText = tr( "● Current step" );
  }
  else
  {
    state = QStringLiteral( "idle" );
    statusText = tr( "Not Ready" );
  }

  setFlowProp( card.cardFrame, "flowState", state );
  setFlowProp( card.statusBadge, "flowBadge", state );
  setFlowProp( card.numberLabel, "flowNum", state );
  card.statusBadge->setText( statusText );
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
    tr( "Pipeline progress: %1/%2 steps ready (%3%)" ).arg( completed ).arg( total ).arg( percent ) );
}
