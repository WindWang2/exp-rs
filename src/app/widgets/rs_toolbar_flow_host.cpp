#include "widgets/rs_toolbar_flow_host.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHash>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSettings>
#include <QStyle>
#include <QStyleOption>
#include <QToolBar>

#include <algorithm>

RsToolbarFlowHost::RsToolbarFlowHost( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsToolbarFlowHost" ) );
  setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
  setMinimumHeight( 0 );
  setMaximumHeight( kMaxRows * kRowH );
  setMouseTracking( true );
}

void RsToolbarFlowHost::setProductToolbars( const QList<QToolBar *> &bars )
{
  // Keep existing width/order when possible.
  QHash<QString, Chip> prev;
  for ( const Chip &c : m_chips )
  {
    if ( c.tb )
      prev.insert( c.tb->objectName(), c );
  }

  // Tear down old frames (keep toolbars).
  for ( Chip &c : m_chips )
  {
    if ( c.tb )
      c.tb->setParent( this );
    if ( c.frame )
    {
      c.frame->hide();
      c.frame->deleteLater();
    }
    c.frame = nullptr;
    c.dragGrip = nullptr;
    c.resizeGrip = nullptr;
  }
  m_chips.clear();

  int i = 0;
  for ( QToolBar *tb : bars )
  {
    if ( !tb )
      continue;
    Chip c;
    c.tb = tb;
    c.order = i++;
    c.width = kDefaultBarW;
    if ( prev.contains( tb->objectName() ) )
    {
      c.width = prev.value( tb->objectName() ).width;
      c.order = prev.value( tb->objectName() ).order;
    }
    m_chips.append( c );
  }

  loadSettings();
  ensureChips();
  reflow();
}

void RsToolbarFlowHost::applyVisibility( const QHash<QToolBar *, bool> &wantByBar )
{
  ensureChips();
  for ( Chip &c : m_chips )
  {
    if ( !c.tb )
      continue;
    c.visible = wantByBar.value( c.tb, false );
    if ( c.frame )
      c.frame->setVisible( c.visible );
    if ( c.tb )
    {
      if ( c.visible )
        c.tb->show();
      else
        c.tb->hide();
    }
  }
  reflow();
}

void RsToolbarFlowHost::ensureChips()
{
  for ( Chip &c : m_chips )
  {
    if ( !c.tb || c.frame )
      continue;

    auto *frame = new QWidget( this );
    frame->setObjectName( QStringLiteral( "rsToolbarChip" ) );
    frame->setFixedHeight( kRowH );
    frame->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );

    auto *lay = new QHBoxLayout( frame );
    lay->setContentsMargins( 0, 0, 0, 0 );
    lay->setSpacing( 0 );

    auto *drag = new QWidget( frame );
    drag->setObjectName( QStringLiteral( "rsToolbarDragGrip" ) );
    drag->setFixedWidth( kGripW );
    drag->setCursor( Qt::SizeAllCursor );
    drag->setToolTip( tr( "拖动以排列工具栏（可放到同一行或第二行）" ) );
    drag->installEventFilter( this );

    c.tb->setParent( frame, Qt::Widget );
    c.tb->setWindowFlags( Qt::Widget );
    c.tb->setMovable( false );
    c.tb->setFloatable( false );
    c.tb->setAllowedAreas( {} );
    c.tb->setFixedHeight( kRowH );
    c.tb->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    c.tb->setMinimumWidth( 0 );

    auto *resize = new QWidget( frame );
    resize->setObjectName( QStringLiteral( "rsToolbarResizeGrip" ) );
    resize->setFixedWidth( kResizeW );
    resize->setCursor( Qt::SizeHorCursor );
    resize->setToolTip( tr( "拖动以调整工具栏长度，显示更多图标" ) );
    resize->installEventFilter( this );

    lay->addWidget( drag );
    lay->addWidget( c.tb, 1 );
    lay->addWidget( resize );

    c.frame = frame;
    c.dragGrip = drag;
    c.resizeGrip = resize;
    frame->hide();
  }
}

void RsToolbarFlowHost::loadSettings()
{
  QSettings s;
  for ( Chip &c : m_chips )
  {
    if ( !c.tb )
      continue;
    const QString name = c.tb->objectName();
    if ( name.isEmpty() )
      continue;
    c.width = s.value( QStringLiteral( "mainwindow/toolbarFlow/width/%1" ).arg( name ),
                       kDefaultBarW )
                .toInt();
    c.width = qBound( kMinBarW, c.width, 1600 );
    c.order = s.value( QStringLiteral( "mainwindow/toolbarFlow/order/%1" ).arg( name ), c.order )
                .toInt();
  }
}

void RsToolbarFlowHost::saveSettings() const
{
  QSettings s;
  for ( const Chip &c : m_chips )
  {
    if ( !c.tb )
      continue;
    const QString name = c.tb->objectName();
    if ( name.isEmpty() )
      continue;
    s.setValue( QStringLiteral( "mainwindow/toolbarFlow/width/%1" ).arg( name ), c.width );
    s.setValue( QStringLiteral( "mainwindow/toolbarFlow/order/%1" ).arg( name ), c.order );
  }
}

RsToolbarFlowHost::Chip *RsToolbarFlowHost::chipFor( QToolBar *tb )
{
  for ( Chip &c : m_chips )
  {
    if ( c.tb == tb )
      return &c;
  }
  return nullptr;
}

RsToolbarFlowHost::Chip *RsToolbarFlowHost::chipForFrame( QWidget *frame )
{
  for ( Chip &c : m_chips )
  {
    if ( c.frame == frame || c.dragGrip == frame || c.resizeGrip == frame )
      return &c;
  }
  return nullptr;
}

QList<RsToolbarFlowHost::Chip *> RsToolbarFlowHost::visibleChipsSorted()
{
  QList<Chip *> out;
  for ( Chip &c : m_chips )
  {
    if ( c.visible && c.frame )
      out.append( &c );
  }
  std::sort( out.begin(), out.end(), []( const Chip *a, const Chip *b ) {
    if ( a->order != b->order )
      return a->order < b->order;
    return a->tb->objectName() < b->tb->objectName();
  } );
  return out;
}

void RsToolbarFlowHost::reflow()
{
  const QList<Chip *> items = visibleChipsSorted();
  if ( items.isEmpty() )
  {
    m_usedRows = 0;
    setFixedHeight( 0 );
    emit geometryChanged();
    return;
  }

  const int avail = qMax( kMinBarW + kGripW + kResizeW, width() - 2 * kMargin );
  int row = 0;
  int x = kMargin;
  int maxRow = 0;

  for ( Chip *c : items )
  {
    if ( !c || !c->frame )
      continue;

    // Total chip width includes grips.
    const int chipW = qBound( kMinBarW + kGripW + kResizeW, c->width + kGripW + kResizeW, avail );

    // Wrap to next row if needed (max 2 rows).
    if ( x > kMargin && x + chipW > kMargin + avail && row < kMaxRows - 1 )
    {
      ++row;
      x = kMargin;
    }

    // If still overflowing on last row, clamp width so it stays in area.
    int placeW = chipW;
    if ( x + placeW > kMargin + avail )
      placeW = qMax( kMinBarW + kGripW + kResizeW, kMargin + avail - x );

    c->frame->setFixedWidth( placeW );
    c->frame->setGeometry( x, row * kRowH, placeW, kRowH );
    c->frame->show();
    c->frame->raise();
    if ( c->tb )
      c->tb->show();

    x += placeW + kSpacing;
    maxRow = qMax( maxRow, row );
  }

  // Hide non-visible frames.
  for ( Chip &c : m_chips )
  {
    if ( !c.visible && c.frame )
      c.frame->hide();
  }

  m_usedRows = maxRow + 1;
  setFixedHeight( m_usedRows * kRowH );
  emit geometryChanged();
}

int RsToolbarFlowHost::insertIndexAt( const QPoint &posInHost ) const
{
  // Order by visual left-to-right, top-to-bottom: map drop point to slot index.
  QList<const Chip *> items;
  for ( const Chip &c : m_chips )
  {
    if ( c.visible && c.frame && ( !m_dragging || &c != m_dragChip ) )
      items.append( &c );
  }
  std::sort( items.begin(), items.end(), []( const Chip *a, const Chip *b ) {
    if ( a->order != b->order )
      return a->order < b->order;
    return a->tb->objectName() < b->tb->objectName();
  } );

  if ( items.isEmpty() )
    return 0;

  // Drop past the right of a chip → after it; above mid of row uses geometric order.
  for ( int i = 0; i < items.size(); ++i )
  {
    const QRect g = items.at( i )->frame->geometry();
    if ( posInHost.y() < g.center().y() - kRowH / 2 && i == 0 )
      return 0;
    // Same row-ish: if cursor left of center, insert before
    if ( g.contains( posInHost ) )
    {
      if ( posInHost.x() < g.center().x() )
        return i;
      return i + 1;
    }
  }
  // Below/after all
  return items.size();
}

void RsToolbarFlowHost::resizeEvent( QResizeEvent *event )
{
  QWidget::resizeEvent( event );
  reflow();
}

void RsToolbarFlowHost::paintEvent( QPaintEvent *event )
{
  QWidget::paintEvent( event );
  // Subtle row separators when two rows are used.
  if ( m_usedRows > 1 )
  {
    QPainter p( this );
    p.setPen( palette().color( QPalette::Mid ) );
    p.drawLine( 0, kRowH, width(), kRowH );
  }
}

bool RsToolbarFlowHost::eventFilter( QObject *watched, QEvent *event )
{
  auto *w = qobject_cast<QWidget *>( watched );
  if ( !w )
    return QWidget::eventFilter( watched, event );

  Chip *chip = chipForFrame( w );
  if ( !chip )
    return QWidget::eventFilter( watched, event );

  const bool isDrag = ( w == chip->dragGrip );
  const bool isResize = ( w == chip->resizeGrip );

  switch ( event->type() )
  {
    case QEvent::MouseButtonPress:
    {
      auto *me = static_cast<QMouseEvent *>( event );
      if ( me->button() != Qt::LeftButton )
        break;
      if ( isDrag )
      {
        m_dragging = true;
        m_dragChip = chip;
        m_dragOffset = me->globalPosition().toPoint();
        if ( chip->frame )
          chip->frame->raise();
        return true;
      }
      if ( isResize )
      {
        m_resizing = true;
        m_resizeChip = chip;
        m_resizeStartW = chip->width;
        m_resizeStartX = me->globalPosition().toPoint().x();
        return true;
      }
      break;
    }
    case QEvent::MouseMove:
    {
      auto *me = static_cast<QMouseEvent *>( event );
      if ( m_resizing && m_resizeChip && ( me->buttons() & Qt::LeftButton ) )
      {
        const int dx = me->globalPosition().toPoint().x() - m_resizeStartX;
        m_resizeChip->width = qBound( kMinBarW, m_resizeStartW + dx, qMax( kMinBarW, width() - 2 * kMargin ) );
        reflow();
        return true;
      }
      if ( m_dragging && m_dragChip && m_dragChip->frame && ( me->buttons() & Qt::LeftButton ) )
      {
        // Follow cursor within host bounds (visual feedback).
        const QPoint local = mapFromGlobal( me->globalPosition().toPoint() );
        int nx = qBound( 0, local.x() - kGripW, qMax( 0, width() - m_dragChip->frame->width() ) );
        int ny = qBound( 0, local.y() - kRowH / 2, qMax( 0, ( kMaxRows - 1 ) * kRowH ) );
        // Snap y to row
        ny = ( ny >= kRowH / 2 ) ? kRowH : 0;
        m_dragChip->frame->move( nx, ny );
        m_dragChip->frame->raise();
        return true;
      }
      break;
    }
    case QEvent::MouseButtonRelease:
    {
      auto *me = static_cast<QMouseEvent *>( event );
      if ( me->button() != Qt::LeftButton )
        break;
      if ( m_resizing )
      {
        m_resizing = false;
        m_resizeChip = nullptr;
        saveSettings();
        reflow();
        return true;
      }
      if ( m_dragging && m_dragChip )
      {
        const QPoint local = mapFromGlobal( me->globalPosition().toPoint() );
        const int insertAt = insertIndexAt( local );

        // Rebuild order indices among visible chips.
        QList<Chip *> vis = visibleChipsSorted();
        vis.removeAll( m_dragChip );
        const int idx = qBound( 0, insertAt, vis.size() );
        vis.insert( idx, m_dragChip );
        for ( int i = 0; i < vis.size(); ++i )
          vis[i]->order = i;

        // Assign remaining (hidden) orders after visible ones.
        int next = vis.size();
        for ( Chip &c : m_chips )
        {
          if ( !c.visible )
            c.order = next++;
        }

        m_dragging = false;
        m_dragChip = nullptr;
        saveSettings();
        reflow();
        return true;
      }
      break;
    }
    default:
      break;
  }
  return QWidget::eventFilter( watched, event );
}
