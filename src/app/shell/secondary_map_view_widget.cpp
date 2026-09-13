#include "shell/secondary_map_view_widget.h"

#include <qgslayertree.h>
#include <qgslayertreemodel.h>
#include <qgslayertreeview.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayerstore.h>
#include <qgsmaptoolpan.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

SecondaryMapViewWidget::SecondaryMapViewWidget( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsSecondaryMapView" ) );
  setMinimumWidth( 280 );

  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 0, 0, 0, 0 );
  root->setSpacing( 0 );

  // Header chrome
  auto *header = new QWidget( this );
  header->setObjectName( QStringLiteral( "rsSecondaryMapViewHeader" ) );
  header->setFixedHeight( 28 );
  auto *headerLay = new QHBoxLayout( header );
  headerLay->setContentsMargins( 8, 2, 4, 2 );
  headerLay->setSpacing( 4 );

  m_titleLabel = new QLabel( tr( "Second View" ), header );
  m_titleLabel->setObjectName( QStringLiteral( "rsSecondaryMapViewTitle" ) );
  headerLay->addWidget( m_titleLabel, 1 );

  m_activateBtn = new QToolButton( header );
  m_activateBtn->setText( tr( "Active" ) );
  m_activateBtn->setCheckable( true );
  m_activateBtn->setToolTip( tr( "Route open / show operations to this view" ) );
  connect( m_activateBtn, &QToolButton::clicked, this, &SecondaryMapViewWidget::activateRequested );
  headerLay->addWidget( m_activateBtn );

  m_syncBtn = new QToolButton( header );
  m_syncBtn->setText( tr( "Sync Main View" ) );
  m_syncBtn->setToolTip( tr( "Clone the main view's display layers into this view (rendered independently)" ) );
  connect( m_syncBtn, &QToolButton::clicked, this, &SecondaryMapViewWidget::syncFromMainRequested );
  headerLay->addWidget( m_syncBtn );

  m_closeBtn = new QToolButton( header );
  m_closeBtn->setText( tr( "Close" ) );
  m_closeBtn->setToolTip( tr( "Close the second view and release its display lease" ) );
  connect( m_closeBtn, &QToolButton::clicked, this, &SecondaryMapViewWidget::closeRequested );
  headerLay->addWidget( m_closeBtn );

  root->addWidget( header );

  // Body: layer list | canvas
  auto *body = new QSplitter( Qt::Horizontal, this );
  body->setObjectName( QStringLiteral( "rsSecondaryMapViewBody" ) );

  m_layerTree = std::make_unique<QgsLayerTree>();
  m_layerTreeModel = new QgsLayerTreeModel( m_layerTree.get(), this );
  m_layerTreeModel->setFlag( QgsLayerTreeModel::ShowLegend, false );
  m_layerTreeView = new QgsLayerTreeView( body );
  m_layerTreeView->setModel( m_layerTreeModel );
  m_layerTreeView->setMinimumWidth( 120 );
  m_layerTreeView->setMaximumWidth( 220 );
  body->addWidget( m_layerTreeView );

  m_layerStore = new QgsMapLayerStore( this );
  m_canvas = new QgsMapCanvas( body );
  m_canvas->setObjectName( QStringLiteral( "rsSecondaryMapCanvas" ) );
  m_canvas->setCanvasColor( QColor( QStringLiteral( "#e9ecf0" ) ) );
  m_canvas->enableAntiAliasing( true );
  m_canvas->setParallelRenderingEnabled( true );
  body->addWidget( m_canvas );
  body->setStretchFactor( 0, 0 );
  body->setStretchFactor( 1, 1 );

  root->addWidget( body, 1 );

  m_panTool = new QgsMapToolPan( m_canvas );
  m_canvas->setMapTool( m_panTool );

  setActiveHighlight( false );
}

SecondaryMapViewWidget::~SecondaryMapViewWidget() = default;

sicnu::display::DisplayViewSpec SecondaryMapViewWidget::viewSpec() const
{
  return sicnu::display::DisplayViewSpec{
      m_canvas, m_layerTree.get(), m_layerStore };
}

void SecondaryMapViewWidget::setViewId( sicnu::display::DisplayViewId id )
{
  m_viewId = id;
}

void SecondaryMapViewWidget::setActiveHighlight( bool active )
{
  if ( m_activateBtn )
    m_activateBtn->setChecked( active );
  if ( m_titleLabel )
  {
    m_titleLabel->setText( active ? tr( "Second View (active)" ) : tr( "Second View" ) );
  }
  auto *header = findChild<QWidget *>( QStringLiteral( "rsSecondaryMapViewHeader" ) );
  if ( header )
  {
    header->setProperty( "active", active );
    header->style()->unpolish( header );
    header->style()->polish( header );
  }
}

void SecondaryMapViewWidget::mousePressEvent( QMouseEvent *event )
{
  if ( event->button() == Qt::LeftButton )
    emit activateRequested();
  QWidget::mousePressEvent( event );
}
