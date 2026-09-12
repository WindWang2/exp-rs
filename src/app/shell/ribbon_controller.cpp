/***************************************************************************
 * ribbon_controller.cpp  —  ArcGIS Pro–style RS product ribbon
 ***************************************************************************/
#include "ribbon_controller.h"

#include "main_window.h"
#include "workflow/pipeline_editor_dock.h"
#include "workbench/command_registry.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QDockWidget>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QSettings>
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
  m.largeBtnMinWidth = qMax( 56, fm.horizontalAdvance( QStringLiteral( tr("four Chinese characters") ) ) + 12 );
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
  const int textW = btn->text().isEmpty() ? fm.horizontalAdvance( QStringLiteral( tr("Label") ) ) : fm.horizontalAdvance( btn->text() );
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

QToolButton *RibbonController::addCommandButton( GroupHost &group,
                                                 const QString &commandId,
                                                 bool large )
{
  if ( !group.toolsLayout || !m_window || !m_window->commandRegistry() )
    return nullptr;
  QAction *action = m_window->commandRegistry()->action( commandId );
  if ( !action )
    return nullptr;

  auto *btn = new QToolButton( group.widget );
  if ( large )
    polishLargeButton( btn );
  else
    polishSmallButton( btn );
  btn->setText( action->text() );
  btn->setIcon( action->icon() );
  btn->setObjectName( QStringLiteral( "cmdBtn_%1" ).arg( commandId ) );
  btn->setAccessibleName( action->text() );
  btn->setCheckable( action->isCheckable() );

  // One-way projection: enabled/checked/text follow the registry action
  // (which itself follows the SelectionContext). Disabled buttons explain
  // themselves through the registry's unavailability reason.
  auto syncFromAction = [btn, action, this ] {
    btn->setText( action->text() );
    btn->setIcon( action->icon() );
    btn->setEnabled( action->isEnabled() );
    btn->setChecked( action->isChecked() );
    QString tip = action->toolTip();
    const QString reason = m_window->commandRegistry()->unavailabilityReason( action->objectName().mid( 4 ) );
    if ( !action->isEnabled() && !reason.isEmpty() )
      tip += QStringLiteral( "\n⚠ %1" ).arg( reason );
    btn->setToolTip( tip );
    btn->setStatusTip( tip );
  };
  syncFromAction();
  connect( action, &QAction::changed, btn, syncFromAction );
  connect( btn, &QToolButton::clicked, btn, [action] {
    if ( action->isEnabled() )
      action->trigger();
  } );

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
    combo->addItem( tr( "Band %1" ).arg( b ), b );
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

    if ( auto *existingRgb = dynamic_cast<QgsMultiBandColorRenderer *>( layer->renderer() ) )
    {
      existingRgb->setRedBand( r );
      existingRgb->setGreenBand( g );
      existingRgb->setBlueBand( b );
    }
    else
    {
      auto *renderer = new QgsMultiBandColorRenderer( layer->dataProvider(), r, g, b );
      layer->setRenderer( renderer );
      layer->setDefaultContrastEnhancement();
    }
  }
  else
  {
    int gray = m_grayBandCombo ? m_grayBandCombo->currentData().toInt() : 1;
    if ( gray < 1 ) gray = 1;
    if ( gray > n ) gray = n;

    if ( auto *existingGray = dynamic_cast<QgsSingleBandGrayRenderer *>( layer->renderer() ) )
    {
      existingGray->setGrayBand( gray );
    }
    else
    {
      auto *renderer = new QgsSingleBandGrayRenderer( layer->dataProvider(), gray );
      layer->setRenderer( renderer );
      layer->setDefaultContrastEnhancement();
    }
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

    if ( auto *existingRgb = dynamic_cast<QgsMultiBandColorRenderer *>( layer->renderer() ) )
    {
      existingRgb->setRedBand( r );
      existingRgb->setGreenBand( g );
      existingRgb->setBlueBand( b );
    }
    else
    {
      layer->setRenderer( new QgsMultiBandColorRenderer( layer->dataProvider(), r, g, b ) );
      layer->setDefaultContrastEnhancement();
    }
  }
  else
  {
    int gray = m_grayBandCombo ? m_grayBandCombo->currentData().toInt() : 1;
    gray = qBound( 1, gray, n );

    if ( auto *existingGray = dynamic_cast<QgsSingleBandGrayRenderer *>( layer->renderer() ) )
    {
      existingGray->setGrayBand( gray );
    }
    else
    {
      layer->setRenderer( new QgsSingleBandGrayRenderer( layer->dataProvider(), gray ) );
      layer->setDefaultContrastEnhancement();
    }
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

QWidget *RibbonController::createRibbonBar()
{
  auto *bar = new QWidget;
  bar->setObjectName( QStringLiteral( "rsRibbonBar" ) );
  bar->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  // tabs 30 + content 96 = 126 (minimum layout height; QAT inlined into tab row)
  bar->setMinimumHeight( 126 );

  auto *root = new QVBoxLayout( bar );
  root->setContentsMargins( 0, 0, 0, 0 );
  root->setSpacing( 0 );

  // ── 1. Tab strip (QAT buttons inlined at left) ─────────────────────────
  auto *tabRow = new QWidget( bar );
  tabRow->setObjectName( QStringLiteral( "rsRibbonTabRow" ) );
  tabRow->setMinimumHeight( 30 );
  auto *tabLay = new QHBoxLayout( tabRow );
  tabLay->setContentsMargins( 6, 0, 6, 0 );
  tabLay->setSpacing( 2 );

  // Quick Access buttons (新建, 打开, 保存, 偏好设置) on the left
  auto addQatBtn = [&]( const char *icon, const QString &tip, auto slot ) {
    auto *btn = new QToolButton( tabRow );
    polishSmallButton( btn );
    btn->setIcon( ribbonIcon( icon ) );
    btn->setToolTip( tip );
    connect( btn, &QToolButton::clicked, m_window, slot );
    tabLay->addWidget( btn );
    return btn;
  };

  addQatBtn( "new_project", tr( "New Project" ), &QgisDesktopWindow::newProject );
  addQatBtn( "o_en", tr( "Open Project" ), &QgisDesktopWindow::openProject );
  addQatBtn( "s_ve", tr( "Save Project" ), &QgisDesktopWindow::saveProject );

  auto *sep = new QFrame( tabRow );
  sep->setObjectName( QStringLiteral( "rsRibbonQatSep" ) );
  sep->setFrameShape( QFrame::VLine );
  sep->setFixedWidth( 1 );
  sep->setFixedHeight( 16 );
  tabLay->addWidget( sep );

  addQatBtn( "hel_", tr( "Preferences" ), &QgisDesktopWindow::options );

  auto *qatTabSep = new QFrame( tabRow );
  qatTabSep->setObjectName( QStringLiteral( "rsRibbonQatSep" ) );
  qatTabSep->setFrameShape( QFrame::VLine );
  qatTabSep->setFixedWidth( 1 );
  qatTabSep->setFixedHeight( 18 );
  tabLay->addWidget( qatTabSep );

  auto *stack = new QStackedWidget( bar );
  stack->setObjectName( QStringLiteral( "rsRibbonStack" ) );
  stack->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
  stack->setMinimumHeight( 96 );

  auto *tabGroup = new QButtonGroup( bar );
  tabGroup->setExclusive( true );

  auto addTab = [&]( const QString &title, QWidget *page, const QString &tip = QString() ) -> QPushButton * {
    auto *tabBtn = new QPushButton( title, tabRow );
    polishTabButton( tabBtn );
    tabBtn->installEventFilter( this );
    if ( !tip.isEmpty() )
    {
      tabBtn->setToolTip( tip );
      tabBtn->setStatusTip( tip );
    }
    const int index = stack->addWidget( page );
    tabGroup->addButton( tabBtn, index );
    tabLay->addWidget( tabBtn );
    connect( tabBtn, &QPushButton::clicked, this, [this, stack, index]() {
      stack->setCurrentIndex( index );
      if ( m_ribbonCollapsed )
        setRibbonCollapsed( false );
    } );
    return tabBtn;
  };

  // ── 工程 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto g = addGroup( pl, tr( "Project" ) );
    if ( auto *btn = addToolButton( g, tr( "New" ), "new_project", tr( "New Empty Project" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::newProject );
    if ( auto *btn = addToolButton( g, tr( "Open" ), "o_en", tr( "Open Project File" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openProject );
    if ( auto *btn = addToolButton( g, tr( "Save" ), "s_ve", tr( "Save Project" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::saveProject );
    addGroupSeparator( pl );
    auto g2 = addGroup( pl, tr( "Data Entry" ) );
    if ( auto *btn = addToolButton( g2, tr( "Import" ), "i_ort", tr( "Import Layer" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::importLayer );
    if ( auto *btn = addToolButton( g2, tr( "Example" ), "r_ster", tr( "Load Teaching Sample Data" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::loadSampleData );
    addTab( tr( "Project" ), pageW, tr( "Project - Project file operations (new/open/save, import, preferences)" ) );
  }

  // ── 编辑（通用：撤销 / 剪贴板 / 选择，非矢量数字化）──────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );

    auto history = addGroup( pl, tr( "History" ) );
    if ( auto *btn = addToolButton( history, tr( "Undo" ), "mActionToggleEditing",
                                    tr( "Undo (Ctrl+Z)" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::undo );
    if ( auto *btn = addToolButton( history, tr( "Redo" ), "mActionSaveEdits",
                                    tr( "Redo (Ctrl+Y)" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::redo );

    addGroupSeparator( pl );
    auto clip = addGroup( pl, tr( "Clipboard" ) );
    if ( auto *btn = addToolButton( clip, tr( "Cut" ), "cut_fill", tr( "Cut Selected Features" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::cutFeatures );
    if ( auto *btn = addToolButton( clip, tr( "Copy" ), "l_yer_st_ck", tr( "Copy Selected Features" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::copyFeatures );
    if ( auto *btn = addToolButton( clip, tr( "Paste" ), "i_ort", tr( "Paste Features" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::pasteFeatures );

    addGroupSeparator( pl );
    auto sel = addGroup( pl, tr( "Select" ) );
    if ( auto *btn = addToolButton( sel, tr( "Select All" ), "select", tr( "Select All Features of the Current Layer" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::selectAll );
    if ( auto *btn = addToolButton( sel, tr( "Select" ), "mActionSelectRectangle",
                                    tr( "Select Features by Rectangle" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::selectFeatures );
    if ( auto *btn = addToolButton( sel, tr( "Delete" ), "mActionDeleteSelectedFeatures",
                                    tr( "Delete Selected Features" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::deleteSelectedFeatures );
    if ( auto *btn = addToolButton( sel, tr( "Attribute Table" ), "t_ble", tr( "Open Attribute Table" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openAttributeTable );

    addTab( tr( "Edit" ), pageW, tr( "Edit - Feature Editing and Digitizing Tools" ) );
  }

  // ── 矢量编辑（数字化 / 几何修改 — 与「编辑」剪贴板分离）────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );

    auto manage = addGroup( pl, tr( "Session" ) );
    if ( auto *btn = addToolButton( manage, tr( "Start Editing" ), "mActionToggleEditing",
                                    tr( "Toggle the vector layer editing session" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::toggleEditing );
    if ( auto *btn = addToolButton( manage, tr( "Save Edits" ), "mActionSaveEdits",
                                    tr( "Save Vector Edits to Data Source" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::saveEdits );

    addGroupSeparator( pl );
    auto capture = addGroup( pl, tr( "Features" ) );
    if ( auto *btn = addToolButton( capture, tr( "Select" ), "mActionSelectRectangle",
                                    tr( "Select Features" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::selectFeatures );
    if ( auto *btn = addToolButton( capture, tr( "Add" ), "mActionCapturePoint",
                                    tr( "Add Feature" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::addFeature );
    if ( auto *btn = addToolButton( capture, tr( "Node" ), "mActionVertexTool",
                                    tr( "Node Tool" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::vertexTool );

    addGroupSeparator( pl );
    auto reshape = addGroup( pl, tr( "Modify" ) );
    if ( auto *btn = addToolButton( reshape, tr( "Move" ), "mActionMoveFeature",
                                    tr( "Move Features" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::moveFeature );
    if ( auto *btn = addToolButton( reshape, tr( "Rotate" ), "mActionRotateFeature",
                                    tr( "Rotate Features" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::rotateFeature );
    if ( auto *btn = addToolButton( reshape, tr( "Reshape" ), "mActionReshape",
                                    tr( "Reshape" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::reshapeGeometry );
    if ( auto *btn = addToolButton( reshape, tr( "Segmentation" ), "mActionSplitFeatures",
                                    tr( "Split Features" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::splitFeatures );

    addGroupSeparator( pl );
    auto advanced = addGroup( pl, tr( "Advanced" ) );
    if ( auto *btn = addToolButton( advanced, tr( "Offset" ), "mActionOffsetCurve",
                                    tr( "Offset Curve" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::offsetCurve );
    if ( auto *btn = addToolButton( advanced, tr( "Simplify" ), "mActionSimplify",
                                    tr( "Simplify Features" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::simplifyFeature );
    if ( auto *btn = addToolButton( advanced, tr( "Reverse" ), "mActionReverseLine",
                                    tr( "Reverse Line" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::reverseLine );
    if ( auto *btn = addToolButton( advanced, tr( "Add Ring" ), "mActionAddRing",
                                    tr( "Add Ring" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::addRing );
    if ( auto *btn = addToolButton( advanced, tr( "Fill Ring" ), "mActionFillRing",
                                    tr( "Fill Ring" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::fillRing );
    if ( auto *btn = addToolButton( advanced, tr( "Delete Part" ), "mActionDeletePart",
                                    tr( "Delete Part" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::deletePart );

    addTab( tr( "Vector Editing" ), pageW, tr( "Vector Editing - Vector Data Processing and Geometry Operations" ) );
  }

  // ── 地图（导航 + 查询 + 波段合成下拉 + 外观）────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );

    auto nav = addGroup( pl, tr( "Navigation" ) );
    // Workbench 5.0: 地图 tab buttons are CommandRegistry projections —
    // enablement follows the selection context, handlers stay single.
    addCommandButton( nav, QStringLiteral( "map.pan" ) );
    addCommandButton( nav, QStringLiteral( "map.zoomIn" ) );
    addCommandButton( nav, QStringLiteral( "map.zoomOut" ) );
    addCommandButton( nav, QStringLiteral( "map.zoomFull" ) );
    addCommandButton( nav, QStringLiteral( "map.refresh" ) );

    addGroupSeparator( pl );
    auto inquiry = addGroup( pl, tr( "Query" ) );
    addCommandButton( inquiry, QStringLiteral( "map.identify" ) );
    addCommandButton( inquiry, QStringLiteral( "map.measureDistance" ) );
    addCommandButton( inquiry, QStringLiteral( "map.measureArea" ) );

    addGroupSeparator( pl );
    // 波段合成：下拉选择（模式 / R G B / 灰度）
    auto bands = addGroup( pl, tr( "Band Composition" ) );
    m_renderModeCombo = addComboBox( bands, tr( "Mode" ),
                                     tr( "RGB true color or single-band grayscale display" ), 96 );
    if ( m_renderModeCombo )
    {
      m_renderModeCombo->addItem( tr( "RGB true color" ), 0 );
      m_renderModeCombo->addItem( tr( "Grayscale" ), 1 );
    }
    m_redBandCombo = addComboBox( bands, tr( "Red R" ), tr( "Band used by the red channel" ), 80 );
    m_greenBandCombo = addComboBox( bands, tr( "Green G" ), tr( "Band used by the green channel" ), 80 );
    m_blueBandCombo = addComboBox( bands, tr( "Blue B" ), tr( "Band used by the blue channel" ), 80 );
    m_grayBandCombo = addComboBox( bands, tr( "Grayscale" ), tr( "Band used for grayscale display" ), 80 );

    addGroupSeparator( pl );
    // 不透明度在底部状态栏（唯一入口），此处只留属性入口
    auto look = addGroup( pl, tr( "Appearance" ) );
    addCommandButton( look, QStringLiteral( "layer.properties" ) );

    addTab( tr( "Map" ), pageW, tr( "Map - View Navigation, Layer Management and Identify Tools" ) )->setChecked( true );
  }

  // ── 数据 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto catalog = addGroup( pl, tr( "Data Catalog" ) );
    if ( auto *btn = addToolButton( catalog, tr( "Data Management" ), "d_t_b_se",
                                    tr( "Open the Data Asset Catalog (Data Manager)" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::showDataManagerPanel );
    addGroupSeparator( pl );
    auto layer = addGroup( pl, tr( "Add Layer" ) );
    if ( auto *btn = addToolButton( layer, tr( "Raster" ), "r_ster", tr( "Add Raster Layer" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::addRasterLayer );
    if ( auto *btn = addToolButton( layer, tr( "Vector" ), "vector", tr( "Add Vector Layer" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::addVectorLayer );
    if ( auto *btn = addToolButton( layer, tr( "STAC" ), "s_tellite", tr( "STAC Catalog Search" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::browseStacCatalog );
    addGroupSeparator( pl );
    auto reg = addGroup( pl, tr( "Registration" ) );
    if ( auto *btn = addToolButton( reg, tr( "Image Registration" ), "geocorrection", tr( "Image to Image" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openGeorefImageToImage );
    if ( auto *btn = addToolButton( reg, tr( "On-Map Registration" ), "geocorrection", tr( "Image to Map" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openGeorefImageToMap );
    addTab( tr( "Data" ), pageW, tr( "Data - Data Asset Catalog and Data Management" ) );
  }

  // ── 预处理 ─────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto prep = addGroup( pl, tr( "Preprocessing" ) );
    if ( auto *btn = addToolButton( prep, tr( "Atmospheric Correction" ), "at_os_corr", tr( "Atmospheric / Radiometric Correction" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.atmospheric_correction" ) );
      } );
    if ( auto *btn = addToolButton( prep, tr( "Image Fusion" ), "p_nsh_r_en", tr( "Panchromatic + multispectral fusion" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.image_fusion" ) );
      } );
    if ( auto *btn = addToolButton( prep, tr( "Mosaic" ), "mos_ic", tr( "Multi-Scene Mosaic" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.mosaic" ) );
      } );
    if ( auto *btn = addToolButton( prep, tr( "Speckle Filtering" ), "sar_process", tr( "SAR Speckle Filtering" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openSpeckleFilterDialog );
    addTab( tr( "Preprocessing" ), pageW, tr( "Preprocessing - Radiometric/Atmospheric Correction, Registration, Fusion, Clipping" ) );
  }

  // ── 增强 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto adj = addGroup( pl, tr( "Display Adjustment" ) );
    if ( QSlider *brightness = addSlider( adj, tr( "Brightness" ), -100, 100, 0,
                                          tr( "Current raster display brightness" ) ) )
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
    if ( QSlider *contrast = addSlider( adj, tr( "Contrast" ), -100, 100, 0,
                                        tr( "Current raster display contrast" ) ) )
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
    auto tools = addGroup( pl, tr( "Stretch and Filtering" ) );
    if ( auto *btn = addToolButton( tools, tr( "Display Stretch" ), "enh_nce",
                                    tr( "Changes display contrast only; no file is written" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openDisplayStretchPanel );
    if ( auto *btn = addToolButton( tools, tr( "Write Contrast" ), "enh_nce",
                                    tr( "Stretch and Export GeoTIFF" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openContrastStretchDialog );
    if ( auto *btn = addToolButton( tools, tr( "Spatial Filtering" ), "destri_ing", tr( "Smoothing / Sharpening" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openSpatialFilterDialog );
    if ( auto *btn = addToolButton( tools, tr( "Enhancement Panel" ), "enh_nce", tr( "Combined Image Enhancement Panel" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openImageEnhancementPanel );
    addTab( tr( "Enhancement" ), pageW, tr( "Enhancement - Image Enhancement, Stretch, Filtering, Band Math" ) );
  }

  // ── 分析 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto spectral = addGroup( pl, tr( "Spectrum" ) );
    if ( auto *btn = addToolButton( spectral, tr( "Spectral Indices" ), "veget_tion_index",
                                    tr( "NDVI / EVI, etc." ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.spectral_index" ) );
      } );
    if ( auto *btn = addToolButton( spectral, tr( "Band Math" ), "b_nd_m_th", tr( "Band Expression" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.band_math" ) );
      } );
    if ( auto *btn = addToolButton( spectral, tr( "Principal Component" ), "pca", tr( "PCA" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.pca" ) );
      } );
    addGroupSeparator( pl );
    auto spatial = addGroup( pl, tr( "Spatial Analysis" ) );
    if ( auto *btn = addToolButton( spatial, tr( "Change Detection" ), "ch_nge_detect", tr( "Two-Date Change" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.change_detection" ) );
      } );
    if ( auto *btn = addToolButton( spatial, tr( "Terrain" ), "dem", tr( "Slope / Aspect" ) ) )
      connect( btn, &QToolButton::clicked, this, [this]() {
        emit openWorkflowTool( QStringLiteral( "tool.rs.terrain_analysis" ) );
      } );
    addTab( tr( "Analysis" ), pageW, tr( "Analysis - Spectral Indices, Change Detection, Terrain, Classification" ) );
  }

  // ── 分类 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto cls = addGroup( pl, tr( "Classification" ) );
    if ( auto *btn = addToolButton( cls, tr( "Supervised Classification" ), "su_ervised", tr( "Pixel-Level Supervised Classification" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openClassificationWindow );
    if ( auto *btn = addToolButton( cls, tr( "Object Classification" ), "seg_ent_tion", tr( "Object-Based (OBIA)" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::openObiaWindow );
    addTab( tr( "Classification" ), pageW, tr( "Classification - Supervised/Unsupervised and Accuracy Assessment" ) );
  }

  // ── 制图 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto map = addGroup( pl, tr( "Outputs" ) );
    if ( auto *btn = addToolButton( map, tr( "Print Layout" ), "print_l_yout", tr( "New Print Layout" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::newLayout );
    if ( auto *btn = addToolButton( map, tr( "Swipe Comparison" ), "l_yer_st_ck", tr( "Swipe Comparison Layer" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::toggleSwipeTool );
    addTab( tr( "Cartography" ), pageW, tr( "Cartography - Layout Design and Map Output" ) );
  }

  // ── 任务 ───────────────────────────────────────────────────────────────
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pl = pageLayoutOf( pageW );
    auto jobs = addGroup( pl, tr( "Jobs" ) );
    if ( auto *btn = addToolButton( jobs, tr( "Task Center" ), "b_tch_queue",
                                    tr( "Queue, Progress and Log" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, [this]() {
        // Sole product task list: bottom RsJobPanel.
        if ( auto *dock = m_window->findChild<QDockWidget *>( QStringLiteral( "rsJobPanelDock" ) ) )
        {
          dock->show();
          dock->raise();
        }
      } );
    }
    if ( auto *btn = addToolButton( jobs, tr( "Processing History" ), "log_viewer", tr( "Processing History" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::showProcessingHistory );
    if ( auto *btn = addToolButton( jobs, tr( "Toolbox" ), "model_builder", tr( "Processing Toolbox" ) ) )
      connect( btn, &QToolButton::clicked, m_window, &QgisDesktopWindow::showProcessingToolbox );
    addTab( tr( "Tasks" ), pageW, tr( "Tasks - Task Center, Processing History and Batch" ) );
  }

  // --- TAB: 流程 (Workflow Editor & Execution) ---
  {
    QWidget *pageW = makeTabPage();
    QHBoxLayout *pLayout = pageLayoutOf( pageW );

    GroupHost editGrp = addGroup( pLayout, tr( "Pipeline Editing" ) );
    if ( auto *btn = addToolButton( editGrp, tr( "New Pipeline" ), "file_new", tr( "New empty workflow canvas" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, [this]() {
        if ( auto *dock = m_window->findChild<QDockWidget *>( QStringLiteral( "rsPipelineEditorDock" ) ) )
        {
          dock->show();
          dock->raise();
        }
      } );
    }
    if ( auto *btn = addToolButton( editGrp, tr( "Open Pipeline" ), "file_open", tr( "Open Workflow JSON Definition" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, [this]() {
        if ( auto *dock = m_window->findChild<sicnu::workflow::gui::PipelineEditorDock *>( QStringLiteral( "rsPipelineEditorDock" ) ) )
        {
          dock->show();
          dock->raise();
          dock->onOpenClicked();
        }
        else if ( auto *generic = m_window->findChild<QDockWidget *>( QStringLiteral( "rsPipelineEditorDock" ) ) )
        {
          generic->show();
          generic->raise();
        }
      } );
    }
    if ( auto *btn = addToolButton( editGrp, tr( "Pipeline Editor" ), "model_builder", tr( "Show/hide the workflow graphical editor" ) ) )
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

    GroupHost execGrp = addGroup( pLayout, tr( "Pipeline Control" ) );
    if ( auto *btn = addToolButton( execGrp, tr( "Run Full Pipeline" ), "task_run", tr( "Runs the whole pipeline in DAG topological order" ) ) )
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
    if ( auto *btn = addToolButton( execGrp, tr( "Stop Run" ), "task_cancel", tr( "Stop the currently running workflow" ) ) )
    {
      connect( btn, &QToolButton::clicked, m_window, [this]() {
        if ( auto *dock = m_window->findChild<sicnu::workflow::gui::PipelineEditorDock *>( QStringLiteral( "rsPipelineEditorDock" ) ) )
        {
          emit dock->stopWorkflowRequested();
        }
      } );
    }

    addTab( tr( "Pipeline" ), pageW, tr( "Pipeline - Workflow Editor and Model Building" ) );
  }

  tabLay->addStretch( 1 );

  auto *brand = new QLabel( tr( "SICNU GEO RS" ), tabRow );
  brand->setObjectName( QStringLiteral( "rsRibbonQatBrand" ) );
  tabLay->addWidget( brand );

  auto *helpBtn = new QToolButton( tabRow );
  polishSmallButton( helpBtn );
  helpBtn->setText( tr( "?" ) );
  helpBtn->setToolTip( tr( "Help content (opens the help document)" ) );
  helpBtn->setStatusTip( tr( "Open Help Document" ) );
  helpBtn->setWhatsThis( tr( "Help content (opens the help document). Press Shift+F1 and click any widget to see its explanation." ) );
  helpBtn->setToolButtonStyle( Qt::ToolButtonTextOnly );
  connect( helpBtn, &QToolButton::clicked, m_window, &QgisDesktopWindow::helpContents );
  tabLay->addWidget( helpBtn );

  auto *collapseBtn = new QToolButton( tabRow );
  collapseBtn->setObjectName( QStringLiteral( "rsRibbonCollapseBtn" ) );
  collapseBtn->setArrowType( Qt::UpArrow );
  collapseBtn->setAutoRaise( true );
  collapseBtn->setCursor( Qt::PointingHandCursor );
  collapseBtn->setFocusPolicy( Qt::StrongFocus );
  collapseBtn->setToolTip( tr( "Collapse Ribbon (Ctrl+F1)" ) );
  collapseBtn->setShortcut( QKeySequence( Qt::CTRL | Qt::Key_F1 ) );
  collapseBtn->setFixedSize( 28, 24 );
  tabLay->addWidget( collapseBtn );

  m_ribbonBar = bar;
  m_stack = stack;
  m_collapseBtn = collapseBtn;
  m_ribbonCollapsed = false;

  connect( collapseBtn, &QToolButton::clicked, this, &RibbonController::toggleRibbonCollapse );

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

void RibbonController::setRibbonCollapsed( bool collapsed )
{
  if ( m_ribbonCollapsed == collapsed )
    return;
  m_ribbonCollapsed = collapsed;
  if ( m_stack )
    m_stack->setVisible( !collapsed );
  if ( m_ribbonBar )
  {
    const int h = collapsed ? 30 : 126;
    m_ribbonBar->setMinimumHeight( h );
    m_ribbonBar->setFixedHeight( h );
    m_ribbonBar->updateGeometry();
  }
  if ( m_collapseBtn )
  {
    m_collapseBtn->setArrowType( collapsed ? Qt::DownArrow : Qt::UpArrow );
    m_collapseBtn->setToolTip( collapsed ? tr( "Expand Ribbon (Ctrl+F1)" ) : tr( "Collapse Ribbon (Ctrl+F1)" ) );
  }
  // Persist alongside the dock-layout state (restored in setupRibbonAndTaskPanel).
  QSettings settings;
  settings.setValue( QStringLiteral( "ribbon/collapsed" ), collapsed );
  emit ribbonCollapsedChanged( collapsed );
}

bool RibbonController::eventFilter( QObject *watched, QEvent *event )
{
  if ( event && event->type() == QEvent::MouseButtonDblClick )
  {
    if ( qobject_cast<QPushButton *>( watched ) )
    {
      toggleRibbonCollapse();
      return true;
    }
  }
  return QObject::eventFilter( watched, event );
}
