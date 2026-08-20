// rs_classify_flowchart_widget.cpp — Interactive flowchart panel for Classification workflow.
#include "rs_classify_flowchart_widget.h"

#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

RsClassifyFlowchartWidget::RsClassifyFlowchartWidget( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsClassifyFlowchartWidget" ) );
  setupUi();
}

void RsClassifyFlowchartWidget::setupUi()
{
  auto *rootLayout = new QVBoxLayout( this );
  rootLayout->setContentsMargins( 8, 8, 8, 8 );
  rootLayout->setSpacing( 6 );

  // --- Header Area ---
  auto *headerFrame = new QFrame( this );
  headerFrame->setObjectName( QStringLiteral( "classifyFlowchartHeader" ) );
  headerFrame->setFrameShape( QFrame::StyledPanel );
  headerFrame->setStyleSheet(
    QStringLiteral( "QFrame#classifyFlowchartHeader { "
                    "  background: palette(window); "
                    "  border: 1px solid palette(midlight); "
                    "  border-radius: 6px; "
                    "  padding: 4px; "
                    "}" ) );
  auto *headerLayout = new QVBoxLayout( headerFrame );
  headerLayout->setContentsMargins( 6, 6, 6, 6 );
  headerLayout->setSpacing( 4 );

  auto *titleRow = new QHBoxLayout();
  auto *titleIcon = new QLabel( QStringLiteral( "📊" ), headerFrame );
  auto *titleText = new QLabel( tr( "遥感影像分类处理流程" ), headerFrame );
  QFont titleFont = titleText->font();
  titleFont.setBold( true );
  titleFont.setPointSize( titleFont.pointSize() + 1 );
  titleText->setFont( titleFont );
  titleRow->addWidget( titleIcon );
  titleRow->addWidget( titleText );
  titleRow->addStretch( 1 );
  headerLayout->addLayout( titleRow );

  m_progressLabel = new QLabel( tr( "流程进度: 0/8 步已完成 (0%)" ), headerFrame );
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
                    "  background-color: #2e7d32; "
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

  // Define 8 Steps
  struct StepMeta {
    FlowStep step;
    QString num;
    QString title;
    QString desc;
    QString action;
  };

  const StepMeta steps[] = {
    { FlowStep::SourceRaster, QStringLiteral( "1" ), tr( "输入源影像" ),
      tr( "加载多波段遥感栅格影像，检查波段与空间参考" ), tr( "打开影像" ) },
    { FlowStep::ClassSystem, QStringLiteral( "2" ), tr( "定义分类体系" ),
      tr( "创建/导入地物类别代码、类别名称与显示调色板" ), tr( "类别管理" ) },
    { FlowStep::SampleCollection, QStringLiteral( "3" ), tr( "采集训练样本" ),
      tr( "在源影像上数字化多边形/点 ROI 训练样本并提取像元" ), tr( "样本采集" ) },
    { FlowStep::SampleEvaluation, QStringLiteral( "4" ), tr( "样本可分性评价" ),
      tr( "计算 JM 分离度距离矩阵与类别均值光谱特征曲线" ), tr( "可分性评价" ) },
    { FlowStep::TrainAndClassify, QStringLiteral( "5" ), tr( "分类器训练与分类" ),
      tr( "训练随机森林/SVM/正态贝叶斯/KNN等分类器并分块流式预测" ), tr( "执行分类" ) },
    { FlowStep::AccuracyAssessment, QStringLiteral( "6" ), tr( "分类精度评定" ),
      tr( "基于独立验证集计算混淆矩阵、总体精度(OA)与Kappa系数" ), tr( "精度评定" ) },
    { FlowStep::PostProcessing, QStringLiteral( "7" ), tr( "分类后处理" ),
      tr( "碎斑过滤(Majority/Sieve)、聚类合并(Clump)与类别重编码" ), tr( "后处理" ) },
    { FlowStep::ExportAndLoad, QStringLiteral( "8" ), tr( "成果导出与加载" ),
      tr( "输出最终分类专题图 GeoTIFF、矢量 Shapefile 或保存模型" ), tr( "导出成果" ) }
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

QFrame *RsClassifyFlowchartWidget::createStepCard( FlowStep step, const QString &num,
                                                   const QString &title, const QString &desc,
                                                   const QString &actionText )
{
  auto *card = new QFrame( m_cardContainer );
  card->setObjectName( QStringLiteral( "classifyStepCard_%1" ).arg( static_cast<int>( step ) ) );
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
  metricLbl->setStyleSheet( QStringLiteral( "color: #1976d2; font-weight: 500; font-size: 11px;" ) );
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
      case FlowStep::SourceRaster:
        emit openSourceRequested();
        break;
      case FlowStep::ClassSystem:
        emit manageClassesRequested();
        break;
      case FlowStep::SampleCollection:
        emit collectSamplesRequested();
        break;
      case FlowStep::SampleEvaluation:
        emit evaluateRequested();
        break;
      case FlowStep::TrainAndClassify:
        emit classifyRequested();
        break;
      case FlowStep::AccuracyAssessment:
        emit accuracyRequested();
        break;
      case FlowStep::PostProcessing:
        emit postProcessRequested();
        break;
      case FlowStep::ExportAndLoad:
        emit exportRequested();
        break;
      default:
        break;
    }
  } );

  return card;
}

void RsClassifyFlowchartWidget::bindController( RsClassifyWorkflowController *controller )
{
  m_controller = controller;
  if ( !m_controller )
    return;

  connect( m_controller, &RsClassifyWorkflowController::currentStepChanged,
           this, [this]( RsClassifyStep s ) {
             setActiveStep( static_cast<FlowStep>( static_cast<int>( s ) + 1 ) );
             refreshState();
           } );

  connect( m_controller, &RsClassifyWorkflowController::completionChanged,
           this, &RsClassifyFlowchartWidget::refreshState );

  refreshState();
}

void RsClassifyFlowchartWidget::setActiveStep( FlowStep step )
{
  m_activeStep = step;
  for ( int i = 0; i < m_cards.size(); ++i )
  {
    m_cards[i].isActive = ( m_cards[i].step == step );
    updateCardStyle( m_cards[i] );
  }
}

void RsClassifyFlowchartWidget::setSourceRasterInfo( const QString &fileName, int width,
                                                     int height, int bands, const QString &crs )
{
  m_hasSource = !fileName.isEmpty() && width > 0 && height > 0;
  if ( m_hasSource )
  {
    m_sourceRasterText = QStringLiteral( "%1 (%2×%3, %4波段)" )
                           .arg( fileName )
                           .arg( width )
                           .arg( height )
                           .arg( bands );
    if ( !crs.isEmpty() )
      m_sourceRasterText += QStringLiteral( " [%1]" ).arg( crs );
  }
  else
  {
    m_sourceRasterText = tr( "未加载影像" );
  }
  refreshState();
}

void RsClassifyFlowchartWidget::setClassCountInfo( int totalClasses )
{
  if ( totalClasses > 0 )
    m_classCountText = tr( "已定义 %1 个地物类别" ).arg( totalClasses );
  else
    m_classCountText = tr( "未定义类别" );
  refreshState();
}

void RsClassifyFlowchartWidget::setSampleInfo( int totalRois, int totalPixels )
{
  if ( totalRois > 0 || totalPixels > 0 )
    m_sampleText = tr( "%1 个 ROI, 共 %2 像元" ).arg( totalRois ).arg( totalPixels );
  else
    m_sampleText = tr( "0 个 ROI, 0 像元" );
  refreshState();
}

void RsClassifyFlowchartWidget::setEvaluationInfo( const QString &summary )
{
  m_evalText = summary.isEmpty() ? tr( "未评估" ) : summary;
  refreshState();
}

void RsClassifyFlowchartWidget::setClassificationInfo( const QString &methodName, int durationMs )
{
  if ( !methodName.isEmpty() )
  {
    if ( durationMs > 0 )
      m_classifyText = QStringLiteral( "%1 (耗时 %2 ms)" ).arg( methodName ).arg( durationMs );
    else
      m_classifyText = methodName;
  }
  else
  {
    m_classifyText = tr( "未分类" );
  }
  refreshState();
}

void RsClassifyFlowchartWidget::setAccuracyInfo( double overallAccuracy, double kappa )
{
  if ( overallAccuracy >= 0.0 )
    m_accuracyText = QStringLiteral( "OA: %1% | Kappa: %2" )
                       .arg( overallAccuracy * 100.0, 0, 'f', 1 )
                       .arg( kappa, 0, 'f', 3 );
  else
    m_accuracyText = tr( "未评估精度" );
  refreshState();
}

void RsClassifyFlowchartWidget::setPostProcessInfo( const QString &operations )
{
  m_postText = operations.isEmpty() ? tr( "未进行后处理" ) : operations;
  refreshState();
}

void RsClassifyFlowchartWidget::setExportInfo( const QString &outputPath )
{
  m_exportText = outputPath.isEmpty() ? tr( "未导出" ) : tr( "已导出至: %1" ).arg( outputPath );
  refreshState();
}

void RsClassifyFlowchartWidget::refreshState()
{
  // Step 0: Source Raster
  const int idxSource = static_cast<int>( FlowStep::SourceRaster );
  if ( idxSource < m_cards.size() )
  {
    m_cards[idxSource].isComplete = m_hasSource;
    m_cards[idxSource].metricLabel->setText( m_sourceRasterText );
    updateCardStyle( m_cards[idxSource] );
  }

  // Steps 1-7 from Controller if present
  if ( m_controller )
  {
    const FlowStep mapping[] = {
      FlowStep::ClassSystem,
      FlowStep::SampleCollection,
      FlowStep::SampleEvaluation,
      FlowStep::TrainAndClassify,
      FlowStep::AccuracyAssessment,
      FlowStep::PostProcessing,
      FlowStep::ExportAndLoad
    };

    const QString metrics[] = {
      m_classCountText,
      m_sampleText,
      m_evalText,
      m_classifyText,
      m_accuracyText,
      m_postText,
      m_exportText
    };

    for ( int i = 0; i < 7; ++i )
    {
      const auto stepEnum = static_cast<RsClassifyStep>( i );
      const int cardIdx = static_cast<int>( mapping[i] );
      if ( cardIdx < m_cards.size() )
      {
        m_cards[cardIdx].isComplete = m_controller->isStepComplete( stepEnum );
        m_cards[cardIdx].metricLabel->setText( metrics[i] );
        updateCardStyle( m_cards[cardIdx] );
      }
    }
  }

  updateOverallProgress();
}

void RsClassifyFlowchartWidget::updateCardStyle( StepCard &card )
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
    statusText = tr( "✓ 已完成" );
    statusStyle = QStringLiteral( "background: #e8f5e9; color: #2e7d32; border-radius: 4px; padding: 1px 6px; font-size: 10px; font-weight: bold;" );
    badgeStyle = QStringLiteral( "background: #4caf50; color: white; border-radius: 10px; font-weight: bold; font-size: 11px;" );
  }
  else if ( card.isActive )
  {
    borderStyle = QStringLiteral( "border: 2px solid #1976d2; background: palette(window);" );
    statusText = tr( "● 当前步骤" );
    statusStyle = QStringLiteral( "background: #e3f2fd; color: #1565c0; border-radius: 4px; padding: 1px 6px; font-size: 10px; font-weight: bold;" );
    badgeStyle = QStringLiteral( "background: #1976d2; color: white; border-radius: 10px; font-weight: bold; font-size: 11px;" );
  }
  else
  {
    borderStyle = QStringLiteral( "border: 1px solid palette(midlight); background: palette(window);" );
    statusText = tr( "未开始" );
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

void RsClassifyFlowchartWidget::updateOverallProgress()
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
    tr( "流程进度: %1/%2 步已完成 (%3%)" ).arg( completed ).arg( total ).arg( percent ) );
}
