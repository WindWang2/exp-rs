// rs_classifier_load_dialog.cpp — Phase 10A.1.3.

#include "rs_classifier_load_dialog.h"
#include "dialogs/dialog_help_catalog.h"
#include "dialogs/dialog_utils.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

RsClassifierLoadDialog::RsClassifierLoadDialog( QWidget *parent )
  : QDialog( parent )
{
  setWindowTitle( tr( "加载分类器模型" ) );
  SicnuUi::polishDialog( this, 440 );

  auto *layout = SicnuUi::makeDialogRootLayout( this );
  layout->addWidget( SicnuUi::makeHintLabel(
    this, tr( "选择算法类型并指定已训练的 OpenCV 模型文件 (.yml)。" ) ) );

  QFrame *sec = SicnuUi::makeSection( this, tr( "模型" ) );
  mRbBayes = new QRadioButton( tr( "NormalBayes（最大似然）" ), sec );
  SicnuDialogHelp::tip( mRbBayes, tr( "正态贝叶斯分类器：假定各类样本服从多元正态分布的最大似然模型" ) );
  mRbSvm = new QRadioButton( tr( "SVM（RBF 核）" ), sec );
  SicnuDialogHelp::tip( mRbSvm, tr( "支持向量机分类器：采用径向基核函数 (RBF) 处理非线性可分地物" ) );
  mRbBayes->setChecked( true );
  auto *grp = new QButtonGroup( this );
  grp->addButton( mRbBayes );
  grp->addButton( mRbSvm );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addWidget(  mRbBayes );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addWidget(  mRbSvm );

  auto *pathRow = new QHBoxLayout;
  mPathEdit = new QLineEdit( sec );
  mPathEdit->setPlaceholderText( tr( "模型文件路径 (.yml)" ) );
  SicnuDialogHelp::tip( mPathEdit, tr( "已保存的 OpenCV 训练模型参数文件路径 (*.yml)" ) );
  auto *browse = new QPushButton( tr( "浏览…" ), sec );
  SicnuUi::markSecondary( browse );
  SicnuDialogHelp::tip( browse, tr( "浏览并选择分类器模型文件" ) );
  pathRow->addWidget( mPathEdit, 1 );
  pathRow->addWidget( browse );
  qobject_cast<QVBoxLayout *>( sec->layout() )->addLayout( pathRow );
  layout->addWidget( sec );

  SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "classifier_load" ) );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
  buttons->button( QDialogButtonBox::Ok )->setText( tr( "确定" ) );
  buttons->button( QDialogButtonBox::Cancel )->setText( tr( "取消" ) );
  SicnuUi::markPrimary( buttons->button( QDialogButtonBox::Ok ) );
  auto *helpBtn = buttons->addButton( tr( "帮助" ), QDialogButtonBox::HelpRole );
  helpBtn->setToolTip( tr( "打开本对话框的帮助说明。" ) );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "classifier_load" ), windowTitle() );
  } );
  layout->addWidget( buttons );

  connect( browse, &QPushButton::clicked,
           this, &RsClassifierLoadDialog::browseForFile );
  connect( buttons, &QDialogButtonBox::accepted,
           this, &RsClassifierLoadDialog::onAccept );
  connect( buttons, &QDialogButtonBox::rejected,
           this, &QDialog::reject );
}

QString RsClassifierLoadDialog::modelPath() const
{
  return mPathEdit ? mPathEdit->text() : QString();
}

void RsClassifierLoadDialog::browseForFile()
{
  const QString path = QFileDialog::getOpenFileName(
    this, tr( "加载分类器模型" ), QString(),
    tr( "OpenCV YAML (*.yml *.yaml *.xml);;所有文件 (*)" ) );
  if ( !path.isEmpty() && mPathEdit )
    mPathEdit->setText( path );
}

void RsClassifierLoadDialog::onAccept()
{
  mKind = mRbSvm && mRbSvm->isChecked()
            ? BackendKind::SvmRbf
            : BackendKind::NormalBayes;
  const QString p = modelPath();
  if ( p.isEmpty() || !QFileInfo::exists( p ) )
    return;
  accept();
}
