/***************************************************************************
 * ribbon_controller.cpp  —  compact top-chrome RS workflow ribbon
 ***************************************************************************/
#include "ribbon_controller.h"

#include "main_window.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QIcon ribbonIcon( const char *alias )
{
  return QIcon( QStringLiteral( ":/icons/" ) + QLatin1String( alias ) );
}

void polishToolButton( QToolButton *btn )
{
  if ( !btn )
    return;
  btn->setToolButtonStyle( Qt::ToolButtonTextUnderIcon );
  btn->setIconSize( QSize( 20, 20 ) );
  btn->setAutoRaise( true );
  btn->setFixedHeight( 48 );
  btn->setMinimumWidth( 48 );
  btn->setMaximumWidth( 72 );
  btn->setFocusPolicy( Qt::NoFocus );
  btn->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
}

void polishTabButton( QPushButton *btn )
{
  if ( !btn )
    return;
  btn->setCheckable( true );
  btn->setFlat( true );
  btn->setFocusPolicy( Qt::NoFocus );
  btn->setCursor( Qt::PointingHandCursor );
  btn->setFixedHeight( 26 );
  btn->setMinimumWidth( 52 );
  btn->setObjectName( QStringLiteral( "rsRibbonTabButton" ) );
}

} // namespace

RibbonController::RibbonController( QgisDesktopWindow *window, QObject *parent )
  : QObject( parent )
  , m_window( window )
{
}

QWidget *RibbonController::makeToolStrip()
{
  auto *page = new QWidget;
  page->setObjectName( QStringLiteral( "rsRibbonPage" ) );
  page->setFixedHeight( 52 );
  auto *layout = new QHBoxLayout( page );
  layout->setContentsMargins( 8, 2, 8, 2 );
  layout->setSpacing( 2 );
  layout->addStretch( 1 );
  return page;
}

void RibbonController::addGroupSeparator( QHBoxLayout *layout )
{
  if ( !layout )
    return;
  auto *line = new QFrame( layout->parentWidget() );
  line->setObjectName( QStringLiteral( "rsRibbonSep" ) );
  line->setFrameShape( QFrame::VLine );
  line->setFrameShadow( QFrame::Plain );
  line->setFixedWidth( 1 );
  line->setFixedHeight( 36 );
  layout->insertWidget( layout->count() - 1, line );
}

QToolButton *RibbonController::addToolButton( QHBoxLayout *layout,
                                              const QString &text,
                                              const char *iconAlias,
                                              const QString &tooltip )
{
  if ( !layout )
    return nullptr;

  auto *btn = new QToolButton( layout->parentWidget() );
  polishToolButton( btn );
  btn->setText( text );
  btn->setIcon( ribbonIcon( iconAlias ) );
  if ( !tooltip.isEmpty() )
  {
    btn->setToolTip( tooltip );
    btn->setStatusTip( tooltip );
  }
  layout->insertWidget( layout->count() - 1, btn );
  return btn;
}

QWidget *RibbonController::createRibbonBar()
{
  auto *bar = new QWidget;
  bar->setObjectName( QStringLiteral( "rsRibbonBar" ) );
  bar->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
  bar->setFixedHeight( 80 );

  auto *root = new QVBoxLayout( bar );
  root->setContentsMargins( 0, 0, 0, 0 );
  root->setSpacing( 0 );

  // ---- Tab row (text-only, not QTabWidget chrome) ----
  auto *tabRow = new QWidget( bar );
  tabRow->setObjectName( QStringLiteral( "rsRibbonTabRow" ) );
  tabRow->setFixedHeight( 28 );
  auto *tabLay = new QHBoxLayout( tabRow );
  tabLay->setContentsMargins( 8, 0, 8, 0 );
  tabLay->setSpacing( 0 );

  auto *stack = new QStackedWidget( bar );
  stack->setObjectName( QStringLiteral( "rsRibbonStack" ) );
  stack->setFixedHeight( 52 );

  auto *tabGroup = new QButtonGroup( bar );
  tabGroup->setExclusive( true );

  auto addTab = [&]( const QString &title, QWidget *page ) {
    auto *tabBtn = new QPushButton( title, tabRow );
    polishTabButton( tabBtn );
    const int index = stack->addWidget( page );
    tabGroup->addButton( tabBtn, index );
    tabLay->addWidget( tabBtn );
    connect( tabBtn, &QPushButton::clicked, stack, [stack, index]() {
      stack->setCurrentIndex( index );
    } );
    return tabBtn;
  };

  // ---- 工程 ----
  {
    QWidget *page = makeToolStrip();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );
    if ( QToolButton *btn = addToolButton( hl, tr( "新建" ), "new_project", tr( "新建工程" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::newProject );
    if ( QToolButton *btn = addToolButton( hl, tr( "打开" ), "o_en", tr( "打开工程" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openProject );
    if ( QToolButton *btn = addToolButton( hl, tr( "保存" ), "s_ve", tr( "保存工程" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::saveProject );
    addGroupSeparator( hl );
    if ( QToolButton *btn = addToolButton( hl, tr( "导入" ), "i_ort", tr( "导入图层" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::importLayer );
    addTab( tr( "工程" ), page );
  }

  // ---- 数据 ----
  {
    QWidget *page = makeToolStrip();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );
    if ( QToolButton *btn = addToolButton( hl, tr( "栅格" ), "r_ster", tr( "添加栅格图层" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::addRasterLayer );
    if ( QToolButton *btn = addToolButton( hl, tr( "矢量" ), "vector", tr( "添加矢量图层" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::addVectorLayer );
    if ( QToolButton *btn = addToolButton( hl, tr( "STAC" ), "s_tellite", tr( "浏览 STAC 目录" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::browseStacCatalog );
    addGroupSeparator( hl );
    if ( QToolButton *btn = addToolButton( hl, tr( "几何校正" ), "geocorrection",
                                           tr( "影像对影像几何校正" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openGeorefImageToImage );
    addTab( tr( "数据" ), page );
  }

  // ---- 预处理 ----
  {
    QWidget *page = makeToolStrip();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );
    if ( QToolButton *btn = addToolButton( hl, tr( "大气校正" ), "at_os_corr", tr( "大气校正" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.atmospheric_correction" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "融合" ), "p_nsh_r_en", tr( "影像融合" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.image_fusion" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "镶嵌" ), "mos_ic", tr( "多景镶嵌" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.mosaic" ) );
      } );
    }
    addTab( tr( "预处理" ), page );
  }

  // ---- 分析 ----
  {
    QWidget *page = makeToolStrip();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );
    if ( QToolButton *btn = addToolButton( hl, tr( "光谱指数" ), "veget_tion_index",
                                           tr( "光谱指数：NDVI / EVI 等" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.spectral_index" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "波段运算" ), "b_nd_m_th", tr( "波段运算" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.band_math" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "PCA" ), "pca", tr( "主成分分析" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.pca" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "变化检测" ), "ch_nge_detect", tr( "变化检测" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.change_detection" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "地形" ), "dem", tr( "地形分析" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.terrain_analysis" ) );
      } );
    }
    addTab( tr( "分析" ), page )->setChecked( true );
  }

  // ---- 分类/解译 ----
  {
    QWidget *page = makeToolStrip();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );
    if ( QToolButton *btn = addToolButton( hl, tr( "监督分类" ), "su_ervised", tr( "像元级监督分类" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openClassificationWindow );
    if ( QToolButton *btn = addToolButton( hl, tr( "OBIA" ), "seg_ent_tion",
                                           tr( "面向对象分类" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openObiaWindow );
    addTab( tr( "分类" ), page );
  }

  // ---- 制图 ----
  {
    QWidget *page = makeToolStrip();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );
    if ( QToolButton *btn = addToolButton( hl, tr( "布局" ), "print_l_yout", tr( "新建打印布局" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::newLayout );
    addTab( tr( "制图" ), page );
  }

  // ---- 视图（地图导航，替代经典地图工具栏主入口）----
  {
    QWidget *page = makeToolStrip();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );
    if ( QToolButton *btn = addToolButton( hl, tr( "平移" ), "p_n", tr( "平移地图" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::panMap );
    if ( QToolButton *btn = addToolButton( hl, tr( "放大" ), "zoo_in", tr( "放大" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::zoomIn );
    if ( QToolButton *btn = addToolButton( hl, tr( "缩小" ), "zoo_out", tr( "缩小" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::zoomOut );
    if ( QToolButton *btn = addToolButton( hl, tr( "全图" ), "full_extent", tr( "全图显示" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::zoomFullExtent );
    addGroupSeparator( hl );
    if ( QToolButton *btn = addToolButton( hl, tr( "识别" ), "identify", tr( "识别要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::identifyFeatures );
    if ( QToolButton *btn = addToolButton( hl, tr( "测距" ), "me_sure_dist", tr( "测距" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::measureDistance );
    if ( QToolButton *btn = addToolButton( hl, tr( "测面" ), "me_sure_are_", tr( "测面" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::measureArea );
    addTab( tr( "视图" ), page );
  }

  tabLay->addStretch( 1 );
  root->addWidget( tabRow );
  root->addWidget( stack );

  // Default: 分析 (index 3)
  stack->setCurrentIndex( 3 );
  if ( QAbstractButton *b = tabGroup->button( 3 ) )
    b->setChecked( true );

  return bar;
}
