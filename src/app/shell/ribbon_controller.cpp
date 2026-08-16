/***************************************************************************
 * ribbon_controller.cpp  —  ArcGIS Pro–style RS product ribbon
 ***************************************************************************/
#include "ribbon_controller.h"

#include "main_window.h"
#include "workflow/pipeline_editor_dock.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterrenderer.h>
#include <qgsbrightnesscontrastfilter.h>
#include <qgsmultibandcolorrenderer.h>
#include <qgssinglebandgrayrenderer.h>

namespace {

struct RibbonMetrics
{
  int iconSize = 28;
  int largeBtnHeight = 64;
  int largeBtnMinWidth = 56;
  int titleHeight = 16;
  int pageHeight = 96;
};

RibbonMetrics computeRibbonMetrics( const QFontMetrics &fm )
{
  RibbonMetrics m;
  m.iconSize = qMax( 24, qMin( 32, fm.height() * 2 ) );
  m.largeBtnHeight = m.iconSize + ( fm.lineSpacing() * 2 ) + 10;
  m.largeBtnMinWidth = qMax( 56, fm.horizontalAdvance( QStringLiteral( "四个汉字" ) ) + 12 );
  m.titleHeight = qMax( 16, fm.height() + 4 );
  m.pageHeight = m.largeBtnHeight + m.titleHeight + 12 + 4;
  return m;
}

QIcon ribbonIcon( const char *alias )
{
  return QIcon( QStringLiteral( ":/icons/" ) + QLatin1String( alias ) );
}

void polishLargeButton( QToolButton *btn )
{
  if ( !btn )
    return;
  btn->setObjectName( QStringLiteral( "rsRibbonLargeBtn" ) );
  btn->setToolButtonStyle( Qt::ToolButtonTextUnderIcon );
  const RibbonMetrics rm = computeRibbonMetrics( btn->fontMetrics() );
  btn->setIconSize( QSize( rm.iconSize, rm.iconSize ) );
  btn->setAutoRaise( true );
  btn->setMinimumHeight( rm.largeBtnHeight );
  btn->setMinimumWidth( rm.largeBtnMinWidth );
  btn->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Preferred );
  btn->setFocusPolicy( Qt::StrongFocus );
  btn->setCursor( Qt::PointingHandCursor );
}

void polishSmallButton( QToolButton *btn )
{
  if ( !btn )
    return;
  btn->setObjectName( QStringLiteral( "rsRibbonQatBtn" ) );
  btn->setToolButtonStyle( Qt::ToolButtonIconOnly );
  const QFontMetrics fm = btn->fontMetrics();
  const int iconSize = qMax( 16, fm.height() );
  btn->setIconSize( QSize( iconSize, iconSize ) );
  btn->setAutoRaise( true );
  const int btnW = qMax( 28, iconSize + 12 );
  const int btnH = qMax( 24, iconSize + 8 );
  btn->setMinimumSize( btnW, btnH );
  btn->setFocusPolicy( Qt::StrongFocus );
  btn->setCursor( Qt::PointingHandCursor );
}

void polishTabButton( QPushButton *btn )
{
  if ( !btn )
    return;
  btn->setCheckable( true );
  btn->setFlat( true );
  btn->setFocusPolicy( Qt::StrongFocus );
  btn->setCursor( Qt::PointingHandCursor );
  const QFontMetrics fm = btn->fontMetrics();
  btn->setMinimumHeight( qMax( 28, fm.height() + 10 ) );
  const int textW = btn->text().isEmpty() ? fm.horizontalAdvance( QStringLiteral( "标签" ) ) : fm.horizontalAdvance( btn->text() );
  btn->setMinimumWidth( qMax( 56, textW + 20 ) );
  btn->setObjectName( QStringLiteral( "rsRibbonTabButton" ) );
}

} // namespace

RibbonController::RibbonController( QgisDesktopWindow *window, QObject *parent )
  : QObject( parent )
  , m_window( window )
{
}

void RibbonController::installChromeContextMenu( QWidget *widget )
{
  if ( !widget || !m_window )
    return;
  widget->setContextMenuPolicy( Qt::CustomContextMenu );
  // Disconnect prior hook if re-installed (idempotent for nested installs).
  disconnect( widget, &QWidget::customContextMenuRequested, this, nullptr );
  connect( widget, &QWidget::customContextMenuRequested, this,
           [this, widget]( const QPoint &pos ) {
             QMenu *menu = m_window->createPopupMenu();
             if ( !menu )
               return;
             menu->setAttribute( Qt::WA_DeleteOnClose );
             menu->popup( widget->mapToGlobal( pos ) );
           } );
}

QgsRasterLayer *RibbonController::currentRasterLayer() const
{
  if ( !m_window || !m_window->mapCanvas() )
    return nullptr;
  if ( auto *rl = qobject_cast<QgsRasterLayer *>( m_window->mapCanvas()->currentLayer() ) )
    return rl;
  for ( QgsMapLayer *l : m_window->mapCanvas()->layers() )
  {
    if ( auto *rl = qobject_cast<QgsRasterLayer *>( l ) )
      return rl;
  }
  return nullptr;
}

QWidget *RibbonController::makeTabPage()
{
  // Scrollable page so groups never crush into a right-side blob
  auto *scroll = new QScrollArea;
  scroll->setObjectName( QStringLiteral( "rsRibbonPageScroll" ) );
  scroll->setWidgetResizable( true );
  scroll->setFrameShape( QFrame::NoFrame );
  scroll->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
  scroll->setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
  
  const RibbonMetrics rm = computeRibbonMetrics( scroll->fontMetrics() );
  scroll->setMinimumHeight( rm.pageHeight );
  scroll->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );

  auto *page = new QWidget;
  page->setObjectName( QStringLiteral( "rsRibbonPage" ) );
  auto *layout = new QHBoxLayout( page );
  layout->setContentsMargins( 8, 4, 8, 4 );
  layout->setSpacing( 0 );
  layout->addStretch( 1 );
  scroll->setWidget( page );
  // store page layout pointer via dynamic property for addGroup
  scroll->setProperty( "pageWidget", QVariant::fromValue<QWidget *>( page ) );
  return scroll;
}

static QHBoxLayout *pageLayoutOf( QWidget *scrollOrPage )
{
  if ( !scrollOrPage )
    return nullptr;
  if ( auto *sa = qobject_cast<QScrollArea *>( scrollOrPage ) )
  {
    if ( QWidget *page = sa->widget() )
      return qobject_cast<QHBoxLayout *>( page->layout() );
  }
  return qobject_cast<QHBoxLayout *>( scrollOrPage->layout() );
}

RibbonController::GroupHost RibbonController::addGroup( QHBoxLayout *pageLayout, const QString &title )
{
  GroupHost g;
  if ( !pageLayout )
    return g;

  auto *group = new QWidget( pageLayout->parentWidget() );
  group->setObjectName( QStringLiteral( "rsRibbonGroup" ) );
  auto *vl = new QVBoxLayout( group );
  vl->setContentsMargins( 6, 2, 6, 0 );
  vl->setSpacing( 2 );

  auto *toolsRow = new QWidget( group );
  auto *toolsLay = new QHBoxLayout( toolsRow );
  toolsLay->setContentsMargins( 0, 0, 0, 0 );
  toolsLay->setSpacing( 2 );
  toolsLay->addStretch( 1 );
  vl->addWidget( toolsRow, 1 );

  auto *titleLbl = new QLabel( title, group );
  titleLbl->setObjectName( QStringLiteral( "rsRibbonGroupTitle" ) );
  titleLbl->setAlignment( Qt::AlignHCenter | Qt::AlignVCenter );
  titleLbl->setFixedHeight( qMax( 16, titleLbl->fontMetrics().height() + 4 ) );
  vl->addWidget( titleLbl );

  pageLayout->insertWidget( pageLayout->count() - 1, group );

  g.widget = group;
  g.toolsLayout = toolsLay;
  return g;
}

void RibbonController::addGroupSeparator( QHBoxLayout *pageLayout )
{
  if ( !pageLayout )
    return;
  auto *line = new QFrame( pageLayout->parentWidget() );
  line->setObjectName( QStringLiteral( "rsRibbonGroupSep" ) );
  line->setFrameShape( QFrame::VLine );
  line->setFrameShadow( QFrame::Plain );
  line->setFixedWidth( 1 );
  line->setMinimumHeight( 64 );
  line->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Preferred );
  pageLayout->insertWidget( pageLayout->count() - 1, line );
}

QToolButton *RibbonController::addToolButton( GroupHost &group,
                                              const QString &text,
                                              const char *iconAlias,
                                              const QString &tooltip,
                                              bool large )
{
  if ( !group.toolsLayout )
    return nullptr;

  auto *btn = new QToolButton( group.widget );
  if ( large )
    polishLargeButton( btn );
  else
    polishSmallButton( btn );
  btn->setText( text );
  btn->setIcon( ribbonIcon( iconAlias ) );
  if ( !tooltip.isEmpty() )
  {
    btn->setToolTip( tooltip );
    btn->setStatusTip( tooltip );
  }
  group.toolsLayout->insertWidget( group.toolsLayout->count() - 1, btn );
  return btn;
}

QSlider *RibbonController::addSlider( GroupHost &group,
                                      const QString &title,
                                      int minVal,
                                      int maxVal,
                                      int value,
                                      const QString &tooltip,
                                      const QString &suffix )
{
  if ( !group.toolsLayout )
    return nullptr;

  auto *box = new QWidget( group.widget );
  box->setObjectName( QStringLiteral( "rsRibbonSliderBox" ) );
  box->setMinimumHeight( 64 );
  box->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Preferred );
  box->setMinimumWidth( 128 );
  box->setMaximumWidth( 168 );
  auto *vl = new QVBoxLayout( box );
  vl->setContentsMargins( 4, 2, 4, 2 );
  vl->setSpacing( 2 );

  auto *header = new QHBoxLayout();
  header->setContentsMargins( 0, 0, 0, 0 );
  auto *titleLbl = new QLabel( title, box );
  titleLbl->setObjectName( QStringLiteral( "rsRibbonSliderTitle" ) );
  auto *valueLbl = new QLabel( box );
  valueLbl->setObjectName( QStringLiteral( "rsRibbonSliderValue" ) );
  valueLbl->setMinimumWidth( 36 );
  valueLbl->setAlignment( Qt::AlignRight | Qt::AlignVCenter );
  header->addWidget( titleLbl );
  header->addStretch( 1 );
  header->addWidget( valueLbl );
  vl->addLayout( header );

  auto *slider = new QSlider( Qt::Horizontal, box );
  slider->setObjectName( QStringLiteral( "rsRibbonSlider" ) );
  slider->setRange( minVal, maxVal );
  slider->setValue( value );
  slider->setMinimumHeight( 18 );
  slider->setFocusPolicy( Qt::StrongFocus );
  if ( !tooltip.isEmpty() )
    slider->setToolTip( tooltip );
  vl->addWidget( slider );
  vl->addStretch( 1 );

  auto updateValue = [valueLbl, suffix]( int v ) {
    valueLbl->setText( suffix.isEmpty() ? QString::number( v )
                                        : QStringLiteral( "%1%2" ).arg( v ).arg( suffix ) );
  };
  updateValue( value );
  connect( slider, &QSlider::valueChanged, box, updateValue );

  group.toolsLayout->insertWidget( group.toolsLayout->count() - 1, box );
  return slider;
}

QComboBox *RibbonController::addComboBox( GroupHost &group,
                                          const QString &title,
                                          const QString &tooltip,
                                          int minWidth )
{
  if ( !group.toolsLayout )
    return nullptr;

  auto *box = new QWidget( group.widget );
  box->setObjectName( QStringLiteral( "rsRibbonComboBox" ) );
  box->setMinimumHeight( 64 );
  box->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Preferred );
  box->setMinimumWidth( minWidth );
  box->setMaximumWidth( qMax( minWidth + 40, 140 ) );
  auto *vl = new QVBoxLayout( box );
  vl->setContentsMargins( 4, 2, 4, 2 );
  vl->setSpacing( 2 );

  auto *titleLbl = new QLabel( title, box );
  titleLbl->setObjectName( QStringLiteral( "rsRibbonComboTitle" ) );
  titleLbl->setAlignment( Qt::AlignLeft | Qt::AlignVCenter );
  vl->addWidget( titleLbl );

  auto *combo = new QComboBox( box );
  combo->setObjectName( QStringLiteral( "rsRibbonCombo" ) );
  combo->setMinimumHeight( 24 );
  combo->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
  combo->setMinimumContentsLength( 4 );
  combo->setFocusPolicy( Qt::StrongFocus );
  if ( !tooltip.isEmpty() )
  {
    combo->setToolTip( tooltip );
    box->setToolTip( tooltip );
  }
  vl->addWidget( combo );
  vl->addStretch( 1 );

  group.toolsLayout->insertWidget( group.toolsLayout->count() - 1, box );
  return combo;
}

void RibbonController::fillBandItems( QComboBox *combo, int bandCount, int selectedBand )
{
  if ( !combo )
    return;
  const QSignalBlocker blocker( combo );
  combo->clear();
  if ( bandCount < 1 )
  {
    combo->addItem( tr( "—" ), 0 );
    combo->setEnabled( false );
    return;
  }
  combo->setEnabled( true );
  for ( int b = 1; b <= bandCount; ++b )
    combo->addItem( tr( "波段 %1" ).arg( b ), b );
  const int idx = combo->findData( selectedBand );
  combo->setCurrentIndex( idx >= 0 ? idx : 0 );
}

void RibbonController::syncBandCombos()
{
  if ( m_bandComboUpdating )
    return;
  m_bandComboUpdating = true;

  QgsRasterLayer *layer = currentRasterLayer();
  const int n = ( layer && layer->isValid() ) ? layer->bandCount() : 0;

  int red = 1, green = 2, blue = 3, gray = 1;
  bool isGray = false;
  if ( layer && layer->isValid() )
  {
    if ( auto *g = dynamic_cast<QgsSingleBandGrayRenderer *>( layer->renderer() ) )
    {
      isGray = true;
      gray = g->inputBand();
    }
    else if ( auto *rgb = dynamic_cast<QgsMultiBandColorRenderer *>( layer->renderer() ) )
    {
      red = rgb->redBand();
      green = rgb->greenBand();
      blue = rgb->blueBand();
    }
    else if ( n >= 3 )
    {
      red = 1;
      green = 2;
      blue = 3;
    }
    else
    {
      isGray = true;
      gray = 1;
    }
  }

  if ( m_renderModeCombo )
  {
    const QSignalBlocker blocker( m_renderModeCombo );
    m_renderModeCombo->setEnabled( n > 0 );
    // 0 = RGB, 1 = Gray
    const int want = isGray || n < 3 ? 1 : 0;
    if ( m_renderModeCombo->currentData().toInt() != want )
    {
      const int idx = m_renderModeCombo->findData( want );
      if ( idx >= 0 )
        m_renderModeCombo->setCurrentIndex( idx );
    }
  }

  fillBandItems( m_redBandCombo, n, red );
  fillBandItems( m_greenBandCombo, n, green );
  fillBandItems( m_blueBandCombo, n, blue );
  fillBandItems( m_grayBandCombo, n, gray );

  const bool rgbMode = m_renderModeCombo
                       && m_renderModeCombo->currentData().toInt() == 0
                       && n >= 3;
  if ( m_redBandCombo )
    m_redBandCombo->setEnabled( rgbMode );
  if ( m_greenBandCombo )
    m_greenBandCombo->setEnabled( rgbMode );
  if ( m_blueBandCombo )
    m_blueBandCombo->setEnabled( rgbMode );
  if ( m_grayBandCombo )
    m_grayBandCombo->setEnabled( n > 0 && !rgbMode );

  m_bandComboUpdating = false;
}

void RibbonController::applyRenderModeFromCombo()
{
  if ( m_bandComboUpdating || !m_renderModeCombo )
    return;
  QgsRasterLayer *layer = currentRasterLayer();
  if ( !layer || !layer->isValid() || !layer->dataProvider() )
    return;

  const int mode = m_renderModeCombo->currentData().toInt(); // 0 RGB, 1 Gray
  const int n = layer->bandCount();
  if ( mode == 0 && n >= 3 )
  {
    int r = m_redBandCombo ? m_redBandCombo->currentData().toInt() : 1;
    int g = m_greenBandCombo ? m_greenBandCombo->currentData().toInt() : 2;
    int b = m_blueBandCombo ? m_blueBandCombo->currentData().toInt() : 3;
    if ( r < 1 ) r = 1;
    if ( g < 1 ) g = qMin( 2, n );
    if ( b < 1 ) b = qMin( 3, n );
    auto *renderer = new QgsMultiBandColorRenderer( layer->dataProvider(), r, g, b );
    layer->setRenderer( renderer );
  }
  else
  {
    int gray = m_grayBandCombo ? m_grayBandCombo->currentData().toInt() : 1;
    if ( gray < 1 ) gray = 1;
    if ( gray > n ) gray = n;
    auto *renderer = new QgsSingleBandGrayRenderer( layer->dataProvider(), gray );
    layer->setRenderer( renderer );
  }
  layer->triggerRepaint();
  syncBandCombos();
}

void RibbonController::applyBandCompositionFromCombos()
{
  if ( m_bandComboUpdating )
    return;
  QgsRasterLayer *layer = currentRasterLayer();
  if ( !layer || !layer->isValid() || !layer->dataProvider() )
    return;

  const int n = layer->bandCount();
  const bool wantRgb = m_renderModeCombo
                       && m_renderModeCombo->currentData().toInt() == 0
                       && n >= 3;

  if ( wantRgb )
  {
    int r = m_redBandCombo ? m_redBandCombo->currentData().toInt() : 1;
    int g = m_greenBandCombo ? m_greenBandCombo->currentData().toInt() : 2;
    int b = m_blueBandCombo ? m_blueBandCombo->currentData().toInt() : 3;
    r = qBound( 1, r, n );
    g = qBound( 1, g, n );
    b = qBound( 1, b, n );
    layer->setRenderer( new QgsMultiBandColorRenderer( layer->dataProvider(), r, g, b ) );
  }
  else
  {
    int gray = m_grayBandCombo ? m_grayBandCombo->currentData().toInt() : 1;
    gray = qBound( 1, gray, n );
    layer->setRenderer( new QgsSingleBandGrayRenderer( layer->dataProvider(), gray ) );
  }
  layer->triggerRepaint();
}

void RibbonController::wireBandComboSignals()
{
  auto onBand = [this]( int ) {
    applyBandCompositionFromCombos();
  };
  if ( m_renderModeCombo )
    connect( m_renderModeCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, [this]( int ) { applyRenderModeFromCombo(); } );
  if ( m_redBandCombo )
    connect( m_redBandCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, onBand );
  if ( m_greenBandCombo )
    connect( m_greenBandCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, onBand );
  if ( m_blueBandCombo )
    connect( m_blueBandCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, onBand );
  if ( m_grayBandCombo )
    connect( m_grayBandCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, onBand );

  if ( m_window && m_window->mapCanvas() )
  {
    connect( m_window->mapCanvas(), &QgsMapCanvas::currentLayerChanged,
             this, [this]( QgsMapLayer * ) { syncBandCombos(); } );
    connect( m_window->mapCanvas(), &QgsMapCanvas::layersChanged,
             this, &RibbonController::syncBandCombos );
  }
}

QWidget *RibbonController::makeQuickAccessToolbar( QWidget *parent )
{
  auto *qat = new QWidget( parent );
  qat->setObjectName( QStringLiteral( "rsRibbonQat" ) );
  qat->setFixedHeight( 28 );
  auto *lay = new QHBoxLayout( qat );
  lay->setContentsMargins( 8, 2, 8, 2 );
  lay->setSpacing( 2 );

  auto addQat = [&]( const char *icon, const QString &tip, auto slot ) {
    auto *btn = new QToolButton( qat );
    polishSmallButton( btn );
    btn->setIcon( ribbonIcon( icon ) );
    btn->setToolTip( tip );
    connect( btn, &QToolButton::clicked, m_window, slot );
    lay->addWidget( btn );
    return btn;
  };

  addQat( "new_project", tr( "新建工程" ), &QgisDesktopWindow::newProject );
  addQat( "o_en", tr( "打开工程" ), &QgisDesktopWindow::openProject );
  addQat( "s_ve", tr( "保存工程" ), &QgisDesktopWindow::saveProject );

  auto *sep = new QFrame( qat );
  sep->setObjectName( QStringLiteral( "rsRibbonQatSep" ) );
  sep->setFrameShape( QFrame::VLine );
  sep->setFixedWidth( 1 );
  sep->setFixedHeight( 16 );
  lay->addWidget( sep );

  addQat( "hel_", tr( "偏好设置" ), &QgisDesktopWindow::options );

  lay->addStretch( 1 );

  auto *brand = new QLabel( tr( "SICNU GEO RS" ), qat );
  brand->setObjectName( QStringLiteral( "rsRibbonQatBrand" ) );
  lay->addWidget( brand );

  auto *helpBtn = new QToolButton( qat );
  polishSmallButton( helpBtn );
  helpBtn->setText( tr( "?" ) );
  helpBtn->setToolTip( tr( "帮助内容（打开帮助文档）" ) );
  helpBtn->setStatusTip( tr( "打开帮助文档" ) );
  helpBtn->setWhatsThis( tr( "帮助内容（打开帮助文档）。按 Shift+F1 后点击任意控件可查看该控件的说明。" ) );
  helpBtn->setToolButtonStyle( Qt::ToolButtonTextOnly );
  connect( helpBtn, &QToolButton::clicked, m_window, &QgisDesktopWindow::helpContents );
  lay->addWidget( helpBtn );

  return qat;
}

QWidget *RibbonController::createRibbonBar()
{
  auto *bar = new QWidget;
  bar->setObjectName( QStringLiteral( "rsRibbonBar" ) );
  bar->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  // QAT 28 + tabs 30 + content 96 = 154 (minimum layout height)
  bar->setMinimumHeight( 154 );

  auto *root = new QVBoxLayout( bar );
  root->setContentsMargins( 0, 0, 0, 0 );
  root->setSpacing( 0 );

  // ── 1. Quick Access Toolbar (ArcGIS Pro QAT) ───────────────────────────
  root->addWidget( makeQuickAccessToolbar( bar ) );

  // ── 2. Tab strip ───────────────────────────────────────────────────────
  auto *tabRow = new QWidget( bar );
  tabRow->setObjectName( QStringLiteral( "rsRibbonTabRow" ) );
  tabRow->setMinimumHeight( 30 );
  auto *tabLay = new QHBoxLayout( tabRow );
  tabLay->setContentsMargins( 8, 0, 8, 0 );
  tabLay->setSpacing( 0 );

  auto *stack = new QStackedWidget( bar );
  stack->setObjectName( QStringLiteral( "rsRibbonStack" ) );
  stack->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  stack->setMinimumHeight( 96 );

  auto *tabGroup = new QButtonGroup( bar );
  tabGroup->setExclusive( true );

  auto addTab = [&]( const QString &title, QWidget *page, const QString &tip = QString() ) -> QPushButton * {
    auto *tabBtn = new QPushButton( title, tabRow );
    polishTabButton( tabBtn );
    if ( !tip.isEmpty() )
    {
      tabBtn->setToolTip( tip );
      tabBtn->setStatusTip( tip );
    }
    const int index = stack->addWidget( page );
    tabGroup->addButton( tabBtn, index );
    tabLay->addWidget( tabBtn );
    connect( tabBtn, &QPushButton::clicked, stack, [stack, index]() {
      stack->setCurrentIndex( index );
    } );
    return tabBtn;
  };

  // ── 工程 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto g = addGroup( pl, tr( "工程" ) );
    if ( auto *btn = addToolButton( g, tr( "新建" ), "new_project", tr( "新建空白工程" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::newProject );
    if ( auto *btn = addToolButton( g, tr( "打开" ), "o_en", tr( "打开工程文件" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openProject );
    if ( auto *btn = addToolButton( g, tr( "保存" ), "s_ve", tr( "保存工程" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::saveProject );
    addGroupSeparator( pl );
    auto g2 = addGroup( pl, tr( "数据入口" ) );
    if ( auto *btn = addToolButton( g2, tr( "导入" ), "i_ort", tr( "导入图层" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::importLayer );
    if ( auto *btn = addToolButton( g2, tr( "示例" ), "r_ster", tr( "加载教学示例数据" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::loadSampleData );
    addTab( tr( "工程" ), pageW, tr( "工程 - 工程文件操作（新建/打开/保存、导入、偏好）" ) );
  }

  // ── 编辑（通用：撤销 / 剪贴板 / 选择，非矢量数字化）──────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );

    auto history = addGroup( pl, tr( "历史" ) );
    if ( auto *btn = addToolButton( history, tr( "撤销" ), "mActionToggleEditing",
                                    tr( "撤销上一步 (Ctrl+Z)" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::undo );
    if ( auto *btn = addToolButton( history, tr( "重做" ), "mActionSaveEdits",
                                    tr( "重做 (Ctrl+Y)" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::redo );

    addGroupSeparator( pl );
    auto clip = addGroup( pl, tr( "剪贴板" ) );
    if ( auto *btn = addToolButton( clip, tr( "剪切" ), "cut_fill", tr( "剪切选中要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::cutFeatures );
    if ( auto *btn = addToolButton( clip, tr( "复制" ), "l_yer_st_ck", tr( "复制选中要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::copyFeatures );
    if ( auto *btn = addToolButton( clip, tr( "粘贴" ), "i_ort", tr( "粘贴要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::pasteFeatures );

    addGroupSeparator( pl );
    auto sel = addGroup( pl, tr( "选择" ) );
    if ( auto *btn = addToolButton( sel, tr( "全选" ), "select", tr( "选择当前图层全部要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::selectAll );
    if ( auto *btn = addToolButton( sel, tr( "选择" ), "mActionSelectRectangle",
                                    tr( "矩形选择要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::selectFeatures );
    if ( auto *btn = addToolButton( sel, tr( "删除" ), "mActionDeleteSelectedFeatures",
                                    tr( "删除选中要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::deleteSelectedFeatures );
    if ( auto *btn = addToolButton( sel, tr( "属性表" ), "t_ble", tr( "打开属性表" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openAttributeTable );

    addTab( tr( "编辑" ), pageW, tr( "编辑 - 要素编辑与数字化工具" ) );
  }

  // ── 矢量编辑（数字化 / 几何修改 — 与「编辑」剪贴板分离）────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );

    auto manage = addGroup( pl, tr( "会话" ) );
    if ( auto *btn = addToolButton( manage, tr( "开始编辑" ), "mActionToggleEditing",
                                    tr( "切换矢量图层编辑会话" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::toggleEditing );
    if ( auto *btn = addToolButton( manage, tr( "保存编辑" ), "mActionSaveEdits",
                                    tr( "保存矢量编辑到数据源" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::saveEdits );

    addGroupSeparator( pl );
    auto capture = addGroup( pl, tr( "要素" ) );
    if ( auto *btn = addToolButton( capture, tr( "选择" ), "mActionSelectRectangle",
                                    tr( "选择要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::selectFeatures );
    if ( auto *btn = addToolButton( capture, tr( "添加" ), "mActionCapturePoint",
                                    tr( "添加要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::addFeature );
    if ( auto *btn = addToolButton( capture, tr( "节点" ), "mActionVertexTool",
                                    tr( "节点工具" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::vertexTool );

    addGroupSeparator( pl );
    auto reshape = addGroup( pl, tr( "修改" ) );
    if ( auto *btn = addToolButton( reshape, tr( "移动" ), "mActionMoveFeature",
                                    tr( "移动要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::moveFeature );
    if ( auto *btn = addToolButton( reshape, tr( "旋转" ), "mActionRotateFeature",
                                    tr( "旋转要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::rotateFeature );
    if ( auto *btn = addToolButton( reshape, tr( "整形" ), "mActionReshape",
                                    tr( "整形" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::reshapeGeometry );
    if ( auto *btn = addToolButton( reshape, tr( "分割" ), "mActionSplitFeatures",
                                    tr( "分割要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::splitFeatures );

    addGroupSeparator( pl );
    auto advanced = addGroup( pl, tr( "高级" ) );
    if ( auto *btn = addToolButton( advanced, tr( "偏移" ), "mActionOffsetCurve",
                                    tr( "偏移曲线" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::offsetCurve );
    if ( auto *btn = addToolButton( advanced, tr( "简化" ), "mActionSimplify",
                                    tr( "简化要素" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::simplifyFeature );
    if ( auto *btn = addToolButton( advanced, tr( "反向" ), "mActionReverseLine",
                                    tr( "线反向" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::reverseLine );
    if ( auto *btn = addToolButton( advanced, tr( "挖环" ), "mActionAddRing",
                                    tr( "添加环" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::addRing );
    if ( auto *btn = addToolButton( advanced, tr( "填环" ), "mActionFillRing",
                                    tr( "填充环" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::fillRing );
    if ( auto *btn = addToolButton( advanced, tr( "删部件" ), "mActionDeletePart",
                                    tr( "删除部件" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::deletePart );

    addTab( tr( "矢量编辑" ), pageW, tr( "矢量编辑 - 矢量数据处理与几何操作" ) );
  }

  // ── 地图（导航 + 查询 + 波段合成下拉 + 外观）────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );

    auto nav = addGroup( pl, tr( "导航" ) );
    if ( auto *btn = addToolButton( nav, tr( "平移" ), "p_n", tr( "平移地图" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::panMap );
    if ( auto *btn = addToolButton( nav, tr( "放大" ), "zoo_in", tr( "放大" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::zoomIn );
    if ( auto *btn = addToolButton( nav, tr( "缩小" ), "zoo_out", tr( "缩小" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::zoomOut );
    if ( auto *btn = addToolButton( nav, tr( "全图" ), "full_extent", tr( "缩放到全图" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::zoomFullExtent );
    if ( auto *btn = addToolButton( nav, tr( "刷新" ), "refresh_view", tr( "刷新地图" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::refreshMap );

    addGroupSeparator( pl );
    auto inquiry = addGroup( pl, tr( "查询" ) );
    if ( auto *btn = addToolButton( inquiry, tr( "识别" ), "identify", tr( "识别要素 / 像元" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::identifyFeatures );
    if ( auto *btn = addToolButton( inquiry, tr( "测距" ), "me_sure_dist", tr( "测量距离" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::measureDistance );
    if ( auto *btn = addToolButton( inquiry, tr( "测面" ), "me_sure_are_", tr( "测量面积" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::measureArea );

    addGroupSeparator( pl );
    // 波段合成：下拉选择（模式 / R G B / 灰度）
    auto bands = addGroup( pl, tr( "波段合成" ) );
    m_renderModeCombo = addComboBox( bands, tr( "模式" ),
                                     tr( "RGB 真彩色 或 灰度单波段显示" ), 96 );
    if ( m_renderModeCombo )
    {
      m_renderModeCombo->addItem( tr( "RGB 真彩色" ), 0 );
      m_renderModeCombo->addItem( tr( "灰度" ), 1 );
    }
    m_redBandCombo = addComboBox( bands, tr( "红 R" ), tr( "红色通道使用的波段" ), 80 );
    m_greenBandCombo = addComboBox( bands, tr( "绿 G" ), tr( "绿色通道使用的波段" ), 80 );
    m_blueBandCombo = addComboBox( bands, tr( "蓝 B" ), tr( "蓝色通道使用的波段" ), 80 );
    m_grayBandCombo = addComboBox( bands, tr( "灰度" ), tr( "灰度显示使用的波段" ), 80 );

    addGroupSeparator( pl );
    // 不透明度在底部状态栏（唯一入口），此处只留属性入口
    auto look = addGroup( pl, tr( "外观" ) );
    if ( auto *btn = addToolButton( look, tr( "图层属性" ), "dis_l_y", tr( "打开图层属性" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::layerProperties );

    addTab( tr( "地图" ), pageW, tr( "地图 - 视图导航、图层管理与识别工具" ) )->setChecked( true );
  }

  // ── 数据 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto catalog = addGroup( pl, tr( "数据目录" ) );
    if ( auto *btn = addToolButton( catalog, tr( "数据管理" ), "d_t_b_se",
                                    tr( "打开数据资产目录（Data Manager）" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::showDataManagerPanel );
    addGroupSeparator( pl );
    auto layer = addGroup( pl, tr( "添加图层" ) );
    if ( auto *btn = addToolButton( layer, tr( "栅格" ), "r_ster", tr( "添加栅格图层" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::addRasterLayer );
    if ( auto *btn = addToolButton( layer, tr( "矢量" ), "vector", tr( "添加矢量图层" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::addVectorLayer );
    if ( auto *btn = addToolButton( layer, tr( "STAC" ), "s_tellite", tr( "STAC 目录检索" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::browseStacCatalog );
    addGroupSeparator( pl );
    auto reg = addGroup( pl, tr( "配准" ) );
    if ( auto *btn = addToolButton( reg, tr( "影像配准" ), "geocorrection", tr( "影像对影像" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openGeorefImageToImage );
    if ( auto *btn = addToolButton( reg, tr( "图上配准" ), "geocorrection", tr( "影像对地图" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openGeorefImageToMap );
    addTab( tr( "数据" ), pageW, tr( "数据 - 数据资产目录与数据管理" ) );
  }

  // ── 预处理 ─────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto prep = addGroup( pl, tr( "预处理" ) );
    if ( auto *btn = addToolButton( prep, tr( "大气校正" ), "at_os_corr", tr( "大气 / 辐射校正" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.atmospheric_correction" ) );
      } );
    if ( auto *btn = addToolButton( prep, tr( "影像融合" ), "p_nsh_r_en", tr( "全色 + 多光谱融合" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.image_fusion" ) );
      } );
    if ( auto *btn = addToolButton( prep, tr( "镶嵌" ), "mos_ic", tr( "多景镶嵌" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.mosaic" ) );
      } );
    if ( auto *btn = addToolButton( prep, tr( "斑点滤波" ), "sar_process", tr( "SAR 斑点滤波" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openSpeckleFilterDialog );
    addTab( tr( "预处理" ), pageW, tr( "预处理 - 辐射/大气校正、配准、融合、裁剪" ) );
  }

  // ── 增强 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto adj = addGroup( pl, tr( "显示调整" ) );
    if ( QSlider *brightness = addSlider( adj, tr( "亮度" ), -100, 100, 0,
                                          tr( "当前栅格显示亮度" ) ) )
    {
      connect( brightness, &QSlider::valueChanged, m_window, [this]( int v ) {
        if ( auto *layer = currentRasterLayer() )
        {
          if ( layer->brightnessFilter() )
          {
            layer->brightnessFilter()->setBrightness( v );
            layer->triggerRepaint();
          }
        }
      } );
    }
    if ( QSlider *contrast = addSlider( adj, tr( "对比度" ), -100, 100, 0,
                                        tr( "当前栅格显示对比度" ) ) )
    {
      connect( contrast, &QSlider::valueChanged, m_window, [this]( int v ) {
        if ( auto *layer = currentRasterLayer() )
        {
          if ( layer->brightnessFilter() )
          {
            layer->brightnessFilter()->setContrast( v );
            layer->triggerRepaint();
          }
        }
      } );
    }
    addGroupSeparator( pl );
    auto tools = addGroup( pl, tr( "拉伸与滤波" ) );
    if ( auto *btn = addToolButton( tools, tr( "显示拉伸" ), "enh_nce",
                                    tr( "仅改显示对比度，不写出文件" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openDisplayStretchPanel );
    if ( auto *btn = addToolButton( tools, tr( "对比度写出" ), "enh_nce",
                                    tr( "拉伸并导出 GeoTIFF" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openContrastStretchDialog );
    if ( auto *btn = addToolButton( tools, tr( "空间滤波" ), "destri_ing", tr( "平滑 / 锐化" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openSpatialFilterDialog );
    if ( auto *btn = addToolButton( tools, tr( "增强面板" ), "enh_nce", tr( "影像增强综合面板" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openImageEnhancementPanel );
    addTab( tr( "增强" ), pageW, tr( "增强 - 影像增强、拉伸、滤波、波段运算" ) );
  }

  // ── 分析 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto spectral = addGroup( pl, tr( "光谱" ) );
    if ( auto *btn = addToolButton( spectral, tr( "光谱指数" ), "veget_tion_index",
                                    tr( "NDVI / EVI 等" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.spectral_index" ) );
      } );
    if ( auto *btn = addToolButton( spectral, tr( "波段运算" ), "b_nd_m_th", tr( "波段表达式" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.band_math" ) );
      } );
    if ( auto *btn = addToolButton( spectral, tr( "主成分" ), "pca", tr( "PCA" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.pca" ) );
      } );
    addGroupSeparator( pl );
    auto spatial = addGroup( pl, tr( "空间分析" ) );
    if ( auto *btn = addToolButton( spatial, tr( "变化检测" ), "ch_nge_detect", tr( "两期变化" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.change_detection" ) );
      } );
    if ( auto *btn = addToolButton( spatial, tr( "地形" ), "dem", tr( "坡度 / 坡向" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.terrain_analysis" ) );
      } );
    addTab( tr( "分析" ), pageW, tr( "分析 - 光谱指数、变化检测、地形、分类" ) );
  }

  // ── 分类 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto cls = addGroup( pl, tr( "分类" ) );
    if ( auto *btn = addToolButton( cls, tr( "监督分类" ), "su_ervised", tr( "像元级监督分类" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openClassificationWindow );
    if ( auto *btn = addToolButton( cls, tr( "对象分类" ), "seg_ent_tion", tr( "面向对象 OBIA" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openObiaWindow );
    addTab( tr( "分类" ), pageW, tr( "分类 - 监督/非监督分类与精度评价" ) );
  }

  // ── 制图 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto map = addGroup( pl, tr( "输出" ) );
    if ( auto *btn = addToolButton( map, tr( "打印布局" ), "print_l_yout", tr( "新建打印布局" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::newLayout );
    if ( auto *btn = addToolButton( map, tr( "卷帘对比" ), "l_yer_st_ck", tr( "卷帘对比图层" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::toggleSwipeTool );
    addTab( tr( "制图" ), pageW, tr( "制图 - 布局设计与制图输出" ) );
  }

  // ── 任务 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto jobs = addGroup( pl, tr( "作业" ) );
    if ( auto *btn = addToolButton( jobs, tr( "任务中心" ), "b_tch_queue",
                                    tr( "队列、进度与日志" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, [this]() {
        // Sole product task list: bottom RsJobPanel (not the old right TaskCenterDock).
        if ( auto *dock = m_window->findChild<QDockWidget *>( QStringLiteral( "rsJobPanelDock" ) ) )
        {
          dock->show();
          dock->raise();
        }
      } );
    }
    if ( auto *btn = addToolButton( jobs, tr( "处理历史" ), "log_viewer", tr( "处理历史" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::showProcessingHistory );
    if ( auto *btn = addToolButton( jobs, tr( "工具箱" ), "model_builder", tr( "处理工具箱" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::showProcessingToolbox );
    addTab( tr( "任务" ), pageW, tr( "任务 - 任务中心、处理历史与批量" ) );
  }

  // --- TAB: 流程 (Workflow Editor & Execution) ---
  {
    QWidget *pageW = makeTabPage();
    auto *pLayout = qobject_cast<QHBoxLayout *>( pageW->layout() );

    GroupHost editGrp = addGroup( pLayout, tr( "流程编辑" ) );
    if ( auto *btn = addToolButton( editGrp, tr( "新建流程" ), "file_new", tr( "新建空白工作流画布" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, [this]() {
        if ( auto *dock = m_window->findChild<QDockWidget *>( QStringLiteral( "rsPipelineEditorDock" ) ) )
        {
          dock->show();
          dock->raise();
        }
      } );
    }
    if ( auto *btn = addToolButton( editGrp, tr( "打开流程" ), "file_open", tr( "打开工作流 JSON 定义" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, [this]() {
        if ( auto *dock = m_window->findChild<QDockWidget *>( QStringLiteral( "rsPipelineEditorDock" ) ) )
        {
          dock->show();
          dock->raise();
        }
      } );
    }
    if ( auto *btn = addToolButton( editGrp, tr( "流程编辑器" ), "model_builder", tr( "显示/隐藏工作流图形编辑器" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, [this]() {
        if ( auto *dock = m_window->findChild<QDockWidget *>( QStringLiteral( "rsPipelineEditorDock" ) ) )
        {
          dock->setVisible( !dock->isVisible() );
          if ( dock->isVisible() )
            dock->raise();
        }
      } );
    }

    GroupHost execGrp = addGroup( pLayout, tr( "流程控制" ) );
    if ( auto *btn = addToolButton( execGrp, tr( "运行全流程" ), "task_run", tr( "按 DAG 拓扑顺序顺序执行全流程" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, [this]() {
        if ( auto *dock = m_window->findChild<sicnu::workflow::gui::PipelineEditorDock *>( QStringLiteral( "rsPipelineEditorDock" ) ) )
        {
          dock->show();
          dock->raise();
          emit dock->runFullWorkflowRequested();
        }
      } );
    }
    if ( auto *btn = addToolButton( execGrp, tr( "停止运行" ), "task_cancel", tr( "停止当前正在运行的工作流" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, [this]() {
        if ( auto *dock = m_window->findChild<sicnu::workflow::gui::PipelineEditorDock *>( QStringLiteral( "rsPipelineEditorDock" ) ) )
        {
          emit dock->stopWorkflowRequested();
        }
      } );
    }

    addTab( tr( "流程" ), pageW, tr( "流程 - 工作流编辑器与模型构建" ) );
  }

  tabLay->addStretch( 1 );
  root->addWidget( tabRow );
  root->addWidget( stack );

  // Tab order: 0工程 1编辑 2矢量编辑 3地图 …
  // Default: 地图 — ArcGIS Pro opens on Map
  constexpr int kMapTab = 3;
  stack->setCurrentIndex( kMapTab );
  if ( QAbstractButton *b = tabGroup->button( kMapTab ) )
    b->setChecked( true );

  wireBandComboSignals();
  syncBandCombos();

  // QGIS-style: right-click ribbon to toggle panels / toolbars.
  installChromeContextMenu( bar );
  installChromeContextMenu( tabRow );
  installChromeContextMenu( stack );
  if ( QWidget *qat = bar->findChild<QWidget *>( QStringLiteral( "rsRibbonQat" ) ) )
    installChromeContextMenu( qat );
  // Nested scroll pages / empty group areas
  const auto scrolls = bar->findChildren<QScrollArea *>();
  for ( QScrollArea *s : scrolls )
  {
    installChromeContextMenu( s );
    if ( s->widget() )
      installChromeContextMenu( s->widget() );
  }

  return bar;
}
