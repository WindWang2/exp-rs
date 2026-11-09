// dialog_utils.cpp — Shared utilities for dialog UI chrome
#include "dialog_utils.h"
#include "design_tokens.h"
#include "widgets/control_chrome.h"

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"
#include "data/raster_grid_compat.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariant>

#include <qgsproject.h>
#include <raster/qgsrasterlayer.h>

// swarm-review APP-D-2: store the layer id (not a raw QgsRasterLayer*) so a
// layer removed from the project cannot leave a dangling combo entry.
void populateRasterLayerCombo( QComboBox *combo, bool clearFirst )
{
  if ( !combo )
    return;

  if ( clearFirst )
    combo->clear();

  const auto layers = QgsProject::instance()->mapLayers();
  for ( auto it = layers.constBegin(); it != layers.constEnd(); ++it )
  {
    auto *rl = qobject_cast<QgsRasterLayer *>( it.value() );
    if ( rl && rl->isValid() )
      combo->addItem( rl->name(), QVariant( rl->id() ) );
  }
}

// swarm-review APP-D-2: resolve combo entries through the project layer
// registry by id; returns nullptr for stale entries.
QgsRasterLayer *rasterLayerFromComboData( const QVariant &data )
{
  if ( !data.isValid() || !data.canConvert<QString>() )
    return nullptr;
  return qobject_cast<QgsRasterLayer *>(
    QgsProject::instance()->mapLayer( data.toString() ) );
}

QgsRasterLayer *currentRasterLayerFromCombo( const QComboBox *combo )
{
  if ( !combo )
    return nullptr;
  return rasterLayerFromComboData( combo->currentData() );
}

namespace SicnuUi
{

using Tokens::spacing;

namespace
{
void allowToolTextWrap( QLabel *label )
{
  label->setWordWrap( true );
  // Keep the preferred width for QFormLayout's height calculation, while
  // allowing the layout to allocate less than the implicit text minimum.
  if ( label->minimumWidth() == 0 )
    label->setMinimumWidth( 1 );
}

void wrapToolSectionTitle( QGroupBox *group )
{
  if ( group->objectName() != QStringLiteral( "rsDialogGroup" ) || group->title().isEmpty() )
    return;

  const QString title = group->title();
  auto *heading = new QLabel( title, group );
  heading->setObjectName( QStringLiteral( "rsDialogSectionTitle" ) );
  heading->setTextFormat( Qt::PlainText );
  allowToolTextWrap( heading );
  if ( auto *box = qobject_cast<QBoxLayout *>( group->layout() ) )
    box->insertWidget( 0, heading );
  else if ( auto *form = qobject_cast<QFormLayout *>( group->layout() ) )
    form->insertRow( 0, heading );
  else
  {
    delete heading;
    return;
  }
  group->setTitle( QString() );
  group->setAccessibleName( title );
}

QVBoxLayout *toolParameterColumn()
{
  auto *column = new QVBoxLayout( new QWidget );
  column->setContentsMargins( 0, 0, 0, 0 );
  column->setSpacing( spacing( 1 ) );
  return column;
}

void stackToolFormRows( QFormLayout *form )
{
  for ( int i = 0; i < form->rowCount(); ++i )
  {
    const QLayoutItem *labelItem = form->itemAt( i, QFormLayout::LabelRole );
    if ( !labelItem || !labelItem->widget() )
      continue;

    const auto row = form->takeRow( i );
    auto *column = toolParameterColumn();
    column->addWidget( row.labelItem->widget() );
    delete row.labelItem;
    if ( row.fieldItem )
    {
      if ( auto *fieldLayout = row.fieldItem->layout() )
        column->addLayout( fieldLayout );
      else if ( auto *field = row.fieldItem->widget() )
      {
        column->addWidget( field );
        delete row.fieldItem;
      }
      else
        column->addItem( row.fieldItem );
    }
    form->insertRow( i, column->parentWidget() );
  }
}

void configureToolForm( QLayout *layout )
{
  if ( auto *form = qobject_cast<QFormLayout *>( layout ) )
  {
    stackToolFormRows( form );
    form->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );
  }
  if ( auto *row = qobject_cast<QHBoxLayout *>( layout ); row && row->count() == 2 )
  {
    QWidget *field = row->itemAt( 0 )->widget();
    auto *action = qobject_cast<QPushButton *>( row->itemAt( 1 )->widget() );
    if ( field && field->property( "rsActionField" ).toBool() && action )
    {
      row->setDirection( QBoxLayout::TopToBottom );
      row->setAlignment( action, Qt::AlignLeading );
    }
  }

  for ( int i = 0; i < layout->count(); ++i )
  {
    QLayoutItem *item = layout->itemAt( i );
    if ( auto *label = qobject_cast<QLabel *>( item->widget() ); label && label->pixmap().isNull() )
      allowToolTextWrap( label );
    if ( auto *group = qobject_cast<QGroupBox *>( item->widget() ) )
      wrapToolSectionTitle( group );
    QLayout *child = item->layout();
    if ( item->widget() )
      child = item->widget()->layout();
    if ( child )
      configureToolForm( child );
  }
}

class ToolButtonBoxAdapter : public QObject
{
public:
  explicit ToolButtonBoxAdapter( QDialogButtonBox *box ) : QObject( box ) {}

  bool eventFilter( QObject *watched, QEvent *event ) override
  {
    auto *box = static_cast<QDialogButtonBox *>( watched );
    if ( event->type() == QEvent::Resize || event->type() == QEvent::Show || event->type() == QEvent::FontChange ||
         event->type() == QEvent::LayoutRequest )
    {
      const int gap = spacing( 2 );
      int rowWidth = box->contentsMargins().left() + box->contentsMargins().right();
      for ( const QAbstractButton *button : box->buttons() )
      {
        if ( !button->isHidden() )
          rowWidth += button->sizeHint().width() + gap;
      }
      const Qt::Orientation orientation = box->width() < rowWidth ? Qt::Vertical : Qt::Horizontal;
      if ( box->orientation() != orientation )
        box->setOrientation( orientation );
    }
    return false;
  }
};

class ToolCheckLabel : public QLabel
{
public:
  using QLabel::QLabel;

protected:
  void mouseReleaseEvent( QMouseEvent *event ) override
  {
    if ( auto *check = qobject_cast<QCheckBox *>( buddy() );
         check && event->button() == Qt::LeftButton && rect().contains( event->position().toPoint() ) )
      check->click();
    else
      QLabel::mouseReleaseEvent( event );
  }
};
}

void polishDialog( QDialog *dlg, int minWidth )
{
  if ( !dlg )
    return;
  dlg->setObjectName( QStringLiteral( "rsToolDialog" ) );
  if ( dlg->minimumWidth() < minWidth )
    dlg->setMinimumWidth( minWidth );
  dlg->setSizeGripEnabled( true );
}

QVBoxLayout *makeDialogRootLayout( QWidget *host )
{
  auto *root = new QVBoxLayout( host );
  root->setContentsMargins( spacing( 3 ), spacing( 3 ), spacing( 3 ), spacing( 3 ) );
  root->setSpacing( spacing( 2 ) );
  return root;
}

QFrame *makeSection( QWidget *parent, const QString &title, const QString &tip )
{
  auto *frame = new QFrame( parent );
  frame->setObjectName( QStringLiteral( "rsDialogSection" ) );
  frame->setFrameShape( QFrame::StyledPanel );
  auto *lay = makeSectionLayout( frame );

  auto *header = new QLabel( title, frame );
  header->setObjectName( QStringLiteral( "rsDialogSectionTitle" ) );
  QFont f = header->font();
  f.setBold( true );
  f.setPointSizeF( f.pointSizeF() + 0.5 );
  header->setFont( f );
  if ( !tip.isEmpty() )
  {
    header->setToolTip( tip );
    header->setWhatsThis( tip );
    header->setStatusTip( tip );
    frame->setToolTip( tip );
  }
  lay->addWidget( header );
  return frame;
}

QGroupBox *makeGroup( QWidget *parent, const QString &title, const QString &tip )
{
  auto *g = new QGroupBox( title, parent );
  g->setObjectName( QStringLiteral( "rsDialogGroup" ) );
  QFont f = g->font();
  f.setBold( true );
  g->setFont( f );
  if ( !tip.isEmpty() )
  {
    g->setToolTip( tip );
    g->setWhatsThis( tip );
    g->setStatusTip( tip );
  }
  return g;
}

QFormLayout *makeFormLayout( QWidget *parent )
{
  auto *form = new QFormLayout( parent );
  form->setLabelAlignment( Qt::AlignRight | Qt::AlignVCenter );
  form->setFieldGrowthPolicy( QFormLayout::ExpandingFieldsGrow );
  form->setContentsMargins( spacing( 2 ), spacing( 2 ), spacing( 2 ), spacing( 2 ) );
  form->setHorizontalSpacing( spacing( 3 ) );
  form->setVerticalSpacing( spacing( 2 ) );
  return form;
}

QLabel *makeToolFieldLabel( const QString &text, QWidget *editor )
{
  auto *label = new QLabel( text );
  label->setTextFormat( Qt::PlainText );
  allowToolTextWrap( label );
  label->setBuddy( editor );
  return label;
}

QWidget *makeToolParameterRow( const QString &text, QWidget *editor )
{
  auto *column = toolParameterColumn();
  column->addWidget( makeToolFieldLabel( text, editor ) );
  column->addWidget( editor );
  return column->parentWidget();
}

QHBoxLayout *makeToolCheckRow( QCheckBox *check )
{
  auto *label = new ToolCheckLabel( check->text() );
  label->setTextFormat( Qt::PlainText );
  allowToolTextWrap( label );
  label->setBuddy( check );
  label->setToolTip( check->toolTip() );
  check->setAccessibleName( check->text() );
  check->setText( QString() );

  auto *row = makeControlRow();
  row->addWidget( check, 0, Qt::AlignVCenter );
  row->addWidget( label, 1 );
  return row;
}

QScrollArea *makeToolFormScroll( QWidget *body )
{
  configureToolForm( body->layout() );
  for ( auto *combo : body->findChildren<QComboBox *>() )
  {
    if ( combo->sizeAdjustPolicy() != QComboBox::AdjustToMinimumContentsLengthWithIcon )
    {
      combo->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
      combo->setMinimumContentsLength( 10 );
    }
  }

  auto *scroll = new QScrollArea;
  scroll->setObjectName( QStringLiteral( "rsToolFormScroll" ) );
  scroll->setFrameShape( QFrame::NoFrame );
  scroll->setWidgetResizable( true );
  scroll->setWidget( body );
  QObject::connect( qApp, &QApplication::focusChanged, scroll, [scroll]( QWidget *, QWidget *focus ) {
    if ( QWidget *contents = scroll->widget(); contents && focus && contents->isAncestorOf( focus ) )
      scroll->ensureWidgetVisible( focus );
  } );
  return scroll;
}

QVBoxLayout *makeScrollableToolBody( QDialog *dialog, QVBoxLayout *form )
{
  auto *body = new QWidget;
  body->setLayout( form );
  form->setContentsMargins( 0, 0, spacing( 1 ), 0 );
  form->addStretch();
  auto *root = makeDialogRootLayout( dialog );
  root->addWidget( makeToolFormScroll( body ), 1 );
  return root;
}

void adaptToolButtonBox( QDialogButtonBox *box )
{
  box->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Fixed );
  box->installEventFilter( new ToolButtonBoxAdapter( box ) );
}

QLabel *makeHintLabel( QWidget *parent, const QString &text )
{
  auto *lbl = new QLabel( text, parent );
  lbl->setObjectName( QStringLiteral( "rsDialogHint" ) );
  lbl->setWordWrap( true );
  return lbl;
}

void markPrimary( QPushButton *btn )
{
  if ( !btn )
    return;
  if ( btn->objectName().isEmpty() )
    btn->setObjectName( QStringLiteral( "rsPrimaryButton" ) );
  btn->setDefault( true );
  btn->setMinimumWidth( 96 );
  styleActionButton( *btn, ActionTone::Primary );
}

void markSecondary( QPushButton *btn )
{
  if ( !btn )
    return;
  if ( btn->objectName().isEmpty() )
    btn->setObjectName( QStringLiteral( "rsSecondaryButton" ) );
  styleActionButton( *btn, ActionTone::Secondary );
}

QHBoxLayout *makeActionRow( QWidget *parent )
{
  Q_UNUSED( parent );
  auto *row = makeControlRow();
  row->setContentsMargins( 0, spacing( 1 ), 0, 0 );
  return row;
}

} // namespace SicnuUi

QString rasterGridCompatibilityMessage( const QString &rasterA,
                                        const QString &rasterB,
                                        bool allowPixelSizeMismatch )
{
  GdalDatasetWrapper a;
  GdalDatasetWrapper b;
  if ( !a.open( rasterA ) || !b.open( rasterB ) )
    return QObject::tr( "Cannot open the images; pixel-grid compatibility cannot be checked." );

  sicnu::data::GridCompatReport report =
      sicnu::data::compareGrids( sicnu::processing::gridFromDataset( a ),
                                 sicnu::processing::gridFromDataset( b ) );
  if ( allowPixelSizeMismatch )
  {
    QVector<sicnu::data::GridCompatIssue> filtered;
    for ( const auto &issue : report.issues )
    {
      if ( issue.verdict != sicnu::data::GridCompatVerdict::PixelSizeMismatch )
        filtered.append( issue );
    }
    report.issues = filtered;
  }
  if ( report.compatible() )
    return QString();

  QString message;
  const auto issues = report.issues;
  for ( int i = 0; i < issues.size(); ++i )
  {
    if ( i == 0 )
      message = issues[i].message;
    else if ( issues[i].blocking || i == 1 )
      message += QLatin1Char( '\n' ) + issues[i].message;
  }
  return message;
}
