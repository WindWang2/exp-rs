// rs_merge_classes_dialog.cpp — target-class chooser for merging sub-classes.
#include "rs_merge_classes_dialog.h"

#include "dialogs/dialog_help_catalog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "dialogs/dialog_utils.h"

#include <algorithm>

QMap<int, int> buildRecodeMap( const QList<int> &sourceIds, int targetId )
{
  QMap<int, int> map;
  for ( int id : sourceIds )
    map.insert( id, targetId );
  // The target id must map to itself so its pixels are preserved, not recoded.
  map.insert( targetId, targetId );
  return map;
}

RsMergeClassesDialog::RsMergeClassesDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "Merge Classification Classes" ) );
  setObjectName( QStringLiteral( "rsMergeClassesDialog" ) );
  SicnuUi::polishDialog( this, 380 );
  setModal( true );

  auto *root = SicnuUi::makeDialogRootLayout( this );

  m_sourceLabel = SicnuUi::makeHintLabel( this, QString() );
  m_sourceLabel->setWordWrap( true );
  root->addWidget( m_sourceLabel );

  auto *targetGroup = SicnuUi::makeGroup( this, tr( "Merge Target Class Attributes" ) );
  auto *form = SicnuUi::makeFormLayout( targetGroup );

  m_targetIdLabel = new QLabel( targetGroup );
  SicnuDialogHelp::tip( m_targetIdLabel, tr( "ID of the merged class (always the smallest ID of the selected source classes)" ) );
  form->addRow( tr( "Target ID" ), m_targetIdLabel );

  m_nameEdit = new QLineEdit( targetGroup );
  m_nameEdit->setObjectName( QStringLiteral( "mergeTargetNameEdit" ) );
  SicnuDialogHelp::tip( m_nameEdit, tr( "Display name of the merged class" ) );
  form->addRow( tr( "Target Name" ), m_nameEdit );

  m_colorBtn = new QPushButton( targetGroup );
  m_colorBtn->setObjectName( QStringLiteral( "mergeTargetColorBtn" ) );
  SicnuDialogHelp::tip( m_colorBtn, tr( "Click to choose the display color of the merged class on the map and in the class table" ) );
  connect( m_colorBtn, &QPushButton::clicked, this, &RsMergeClassesDialog::pickColor );
  form->addRow( tr( "Target Color" ), m_colorBtn );

  root->addWidget( targetGroup );

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "merge_classes" ) );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
  buttons->button( QDialogButtonBox::Ok )->setText( tr( "OK" ) );
  buttons->button( QDialogButtonBox::Cancel )->setText( tr( "Cancel" ) );
  SicnuUi::markPrimary( buttons->button( QDialogButtonBox::Ok ) );
  SicnuUi::markSecondary( buttons->button( QDialogButtonBox::Cancel ) );

  auto *helpBtn = buttons->addButton( tr( "Help" ), QDialogButtonBox::HelpRole );
  helpBtn->setToolTip( tr( "Opens the help for this dialog." ) );
  SicnuUi::markSecondary( helpBtn );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "merge_classes" ), windowTitle() );
  } );
  connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
  root->addWidget( buttons );
}

void RsMergeClassesDialog::setSourceClassIds( const QList<int> &ids,
                                              const QString &firstName,
                                              const QColor &firstColor )
{
  m_sourceIds = ids;
  std::sort( m_sourceIds.begin(), m_sourceIds.end() );
  m_sourceIds.erase( std::unique( m_sourceIds.begin(), m_sourceIds.end() ),
                     m_sourceIds.end() );

  QStringList idTexts;
  idTexts.reserve( m_sourceIds.size() );
  for ( int id : m_sourceIds )
    idTexts.append( QString::number( id ) );
  m_sourceLabel->setText( tr( "Merge the following classes: %1" ).arg( idTexts.join( QStringLiteral( ", " ) ) ) );

  if ( m_sourceIds.isEmpty() )
    return;

  m_targetIdLabel->setText( QString::number( m_sourceIds.first() )
                            + tr( " (auto)" ) );
  m_nameEdit->setText( firstName.isEmpty() ? QString::number( m_sourceIds.first() ) : firstName );
  m_color = firstColor.isValid() ? firstColor : QColor( QStringLiteral( "#888888" ) );
  refreshColorButton();
}

QString RsMergeClassesDialog::targetName() const
{
  return m_nameEdit->text().trimmed();
}

QColor RsMergeClassesDialog::targetColor() const
{
  return m_color;
}

int RsMergeClassesDialog::targetClassId() const
{
  return m_sourceIds.isEmpty() ? -1 : m_sourceIds.first();
}

void RsMergeClassesDialog::pickColor()
{
  const QColor c = QColorDialog::getColor( m_color, this, tr( "Choose Target Color" ) );
  if ( c.isValid() )
  {
    m_color = c;
    refreshColorButton();
  }
}

void RsMergeClassesDialog::refreshColorButton()
{
  m_colorBtn->setText( m_color.name() );
  // The stylesheet color name must be lowercase (#rrggbb) for setStyleSheet.
  m_colorBtn->setStyleSheet(
    QStringLiteral( "background-color: %1;" ).arg( m_color.name().toLower() ) );
}
