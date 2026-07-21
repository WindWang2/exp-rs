/***************************************************************************
 * ribbon_controller.cpp  —  six-tab RS workflow ribbon above the map canvas
 ***************************************************************************/
#include "ribbon_controller.h"

#include "main_window.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QIcon ribbonIcon( const char *alias )
{
  return QIcon( QStringLiteral( ":/icons/" ) + QLatin1String( alias ) );
}

void polishButton( QToolButton *btn )
{
  if ( !btn )
    return;
  btn->setToolButtonStyle( Qt::ToolButtonTextUnderIcon );
  btn->setIconSize( QSize( 24, 24 ) );
  btn->setAutoRaise( true );
  btn->setMinimumWidth( 64 );
  btn->setFocusPolicy( Qt::NoFocus );
}

} // namespace

RibbonController::RibbonController( QgisDesktopWindow *window, QObject *parent )
  : QObject( parent )
  , m_window( window )
{
}

QWidget *RibbonController::makeTabPage()
{
  auto *page = new QWidget;
  auto *layout = new QHBoxLayout( page );
  layout->setContentsMargins( 8, 4, 8, 6 );
  layout->setSpacing( 4 );
  layout->addStretch( 1 );
  return page;
}

QToolButton *RibbonController::addToolButton( QHBoxLayout *layout,
                                              const QString &text,
                                              const char *iconAlias,
                                              const QString &tooltip )
{
  if ( !layout )
    return nullptr;

  auto *btn = new QToolButton( layout->parentWidget() );
  polishButton( btn );
  btn->setText( text );
  btn->setIcon( ribbonIcon( iconAlias ) );
  if ( !tooltip.isEmpty() )
  {
    btn->setToolTip( tooltip );
    btn->setStatusTip( tooltip );
  }
  // Insert before the trailing stretch.
  layout->insertWidget( layout->count() - 1, btn );
  return btn;
}

QWidget *RibbonController::createRibbonBar()
{
  auto *bar = new QWidget;
  bar->setObjectName( QStringLiteral( "rsRibbonBar" ) );

  auto *root = new QVBoxLayout( bar );
  root->setContentsMargins( 0, 0, 0, 0 );
  root->setSpacing( 0 );

  auto *tabs = new QTabWidget( bar );
  tabs->setObjectName( QStringLiteral( "rsRibbonTabBar" ) );
  tabs->setDocumentMode( true );
  tabs->setMovable( false );
  tabs->setUsesScrollButtons( true );
  root->addWidget( tabs );

  // ---- 工程 ----
  {
    QWidget *page = makeTabPage();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );

    if ( QToolButton *btn = addToolButton( hl, tr( "新建" ), "new_project",
                                           tr( "新建工程" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::newProject );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "打开" ), "o_en",
                                           tr( "打开工程" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openProject );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "保存" ), "s_ve",
                                           tr( "保存工程" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::saveProject );
    }

    tabs->addTab( page, tr( "工程" ) );
  }

  // ---- 数据 ----
  {
    QWidget *page = makeTabPage();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );

    if ( QToolButton *btn = addToolButton( hl, tr( "导入" ), "i_ort",
                                           tr( "导入图层" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::importLayer );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "STAC" ), "s_tellite",
                                           tr( "浏览 STAC 目录" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::browseStacCatalog );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "几何校正" ), "geocorrection",
                                           tr( "影像对影像几何校正" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openGeorefImageToImage );
    }

    tabs->addTab( page, tr( "数据" ) );
  }

  // ---- 预处理 ----
  {
    QWidget *page = makeTabPage();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );

    if ( QToolButton *btn = addToolButton( hl, tr( "大气校正" ), "at_os_corr",
                                           tr( "大气校正" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.atmospheric_correction" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "融合" ), "p_nsh_r_en",
                                           tr( "影像融合" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.image_fusion" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "镶嵌" ), "mos_ic",
                                           tr( "多景镶嵌" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.mosaic" ) );
      } );
    }

    tabs->addTab( page, tr( "预处理" ) );
  }

  // ---- 分析 ----
  {
    QWidget *page = makeTabPage();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );

    if ( QToolButton *btn = addToolButton( hl, tr( "光谱指数" ), "veget_tion_index",
                                           tr( "光谱指数：NDVI / EVI 等" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.spectral_index" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "波段运算" ), "b_nd_m_th",
                                           tr( "波段运算表达式" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.band_math" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "PCA" ), "pca",
                                           tr( "主成分分析" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.pca" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "变化检测" ), "ch_nge_detect",
                                           tr( "变化检测" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.change_detection" ) );
      } );
    }
    if ( QToolButton *btn = addToolButton( hl, tr( "地形" ), "dem",
                                           tr( "地形分析" ) ) )
    {
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.terrain_analysis" ) );
      } );
    }

    tabs->addTab( page, tr( "分析" ) );
  }

  // ---- 分类/解译 ----
  {
    QWidget *page = makeTabPage();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );

    if ( QToolButton *btn = addToolButton( hl, tr( "监督分类" ), "su_ervised",
                                           tr( "像元级监督分类" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openClassificationWindow );
    }
    // OBIA keeps its dedicated workspace window (RsObiaMainWindow); ribbon
    // mirrors the 栅格 menu entry. lab.obia is a catalog stub only for now.
    if ( QToolButton *btn = addToolButton( hl, tr( "OBIA" ), "seg_ent_tion",
                                           tr( "面向对象分类：分割 + 对象级分类" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openObiaWindow );
    }

    tabs->addTab( page, tr( "分类/解译" ) );
  }

  // ---- 制图 ----
  {
    QWidget *page = makeTabPage();
    auto *hl = qobject_cast<QHBoxLayout *>( page->layout() );

    if ( QToolButton *btn = addToolButton( hl, tr( "布局" ), "print_l_yout",
                                           tr( "新建打印布局" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::newLayout );
    }

    tabs->addTab( page, tr( "制图" ) );
  }

  // Default to 分析 for W1 demo path
  tabs->setCurrentIndex( 3 );

  return bar;
}
