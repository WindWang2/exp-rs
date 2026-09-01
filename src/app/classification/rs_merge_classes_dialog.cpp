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
  setWindowTitle( tr( "合并分类类别" ) );
  setObjectName( QStringLiteral( "rsMergeClassesDialog" ) );
  SicnuUi::polishDialog( this, 380 );
  setModal( true );

  auto *root = SicnuUi::makeDialogRootLayout( this );

  m_sourceLabel = SicnuUi::makeHintLabel( this, QString() );
  m_sourceLabel->setWordWrap( true );
  root->addWidget( m_sourceLabel );

  auto *targetGroup = SicnuUi::makeGroup( this, tr( "合并目标类别属性" ) );
  auto *form = SicnuUi::makeFormLayout( targetGroup );

  m_targetIdLabel = new QLabel( targetGroup );
  SicnuDialogHelp::tip( m_targetIdLabel, tr( "合并后新类别的 ID（固定取所选源类别的最小 ID）" ) );
  form->addRow( tr( "目标 ID" ), m_targetIdLabel );

  m_nameEdit = new QLineEdit( targetGroup );
  m_nameEdit->setObjectName( QStringLiteral( "mergeTargetNameEdit" ) );
  SicnuDialogHelp::tip( m_nameEdit, tr( "合并后新类别的显示名称" ) );
  form->addRow( tr( "目标名称" ), m_nameEdit );

  m_colorBtn = new QPushButton( targetGroup );
  m_colorBtn->setObjectName( QStringLiteral( "mergeTargetColorBtn" ) );
  SicnuDialogHelp::tip( m_colorBtn, tr( "点击选择合并后新类别在地图与分类表中的显示颜色" ) );
  connect( m_colorBtn, &QPushButton::clicked, this, &RsMergeClassesDialog::pickColor );
  form->addRow( tr( "目标颜色" ), m_colorBtn );

  root->addWidget( targetGroup );

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "merge_classes" ) );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
  buttons->button( QDialogButtonBox::Ok )->setText( tr( "确定" ) );
  buttons->button( QDialogButtonBox::Cancel )->setText( tr( "取消" ) );
  SicnuUi::markPrimary( buttons->button( QDialogButtonBox::Ok ) );
  SicnuUi::markSecondary( buttons->button( QDialogButtonBox::Cancel ) );

  auto *helpBtn = buttons->addButton( tr( "帮助" ), QDialogButtonBox::HelpRole );
  helpBtn->setToolTip( tr( "打开本对话框的帮助说明。" ) );
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
  m_sourceLabel->setText( tr( "合并以下类别: %1" ).arg( idTexts.join( QStringLiteral( ", " ) ) ) );

  if ( m_sourceIds.isEmpty() )
    return;

  m_targetIdLabel->setText( QString::number( m_sourceIds.first() )
                            + tr( " (自动)" ) );
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
  const QColor c = QColorDialog::getColor( m_color, this, tr( "选择目标颜色" ) );
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
