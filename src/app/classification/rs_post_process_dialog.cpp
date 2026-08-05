// rs_post_process_dialog.cpp — one algorithm per dialog
#include "rs_post_process_dialog.h"
#include "dialogs/dialog_help_catalog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

QString RsPostProcessDialog::algorithmTitle( Algorithm a )
{
  switch ( a )
  {
    case Algorithm::Sieve:
      return tr( "Sieve 小斑去除" );
    case Algorithm::Majority:
      return tr( "多数滤波" );
    case Algorithm::Clump:
      return tr( "Clump 连通域标记" );
    case Algorithm::Recode:
      return tr( "重编码" );
    case Algorithm::Polygonize:
      return tr( "矢量化 Polygonize" );
  }
  return tr( "后处理" );
}

QString RsPostProcessDialog::algorithmId( Algorithm a )
{
  switch ( a )
  {
    case Algorithm::Sieve:
      return QStringLiteral( "module:classify:postprocess:sieve" );
    case Algorithm::Majority:
      return QStringLiteral( "module:classify:postprocess:majority" );
    case Algorithm::Clump:
      return QStringLiteral( "module:classify:postprocess:clump" );
    case Algorithm::Recode:
      return QStringLiteral( "module:classify:postprocess:recode" );
    case Algorithm::Polygonize:
      return QStringLiteral( "module:classify:postprocess:polygonize" );
  }
  return QStringLiteral( "module:classify:postprocess" );
}

RsPostProcessDialog::RsPostProcessDialog( Algorithm algo, QWidget *parent )
  : QDialog( parent )
  , m_algo( algo )
{
  setWindowTitle( algorithmTitle( algo ) );
  setObjectName( QStringLiteral( "rsPostProcessDialog_%1" )
                   .arg( static_cast<int>( algo ) ) );
  setModal( true );
  resize( 480, 360 );
  setupUi();
}

void RsPostProcessDialog::setupUi()
{
  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 12, 12, 12, 12 );
  root->setSpacing( 10 );

  QString hint;
  switch ( m_algo )
  {
    case Algorithm::Sieve:
      hint = tr( "去除面积小于阈值的连通斑块，并用邻域多数类填充。" );
      break;
    case Algorithm::Majority:
      hint = tr( "滑动窗口众数滤波，平滑分类边界（核大小须为奇数）。" );
      break;
    case Algorithm::Clump:
      hint = tr( "对同类像元做连通域标记，输出斑块编号栅格。" );
      break;
    case Algorithm::Recode:
      hint = tr( "按「旧类 → 新类」表重映射类别编号；未列出的类保持不变。" );
      break;
    case Algorithm::Polygonize:
      hint = tr( "将分类/标签栅格矢量化为面要素（.gpkg 或 .shp）。" );
      break;
  }
  m_hintLabel = new QLabel( hint, this );
  m_hintLabel->setWordWrap( true );
  m_hintLabel->setStyleSheet( QStringLiteral( "color: #656d76;" ) );
  root->addWidget( m_hintLabel );

  auto *form = new QFormLayout;
  form->setSpacing( 6 );

  auto makeBrowse = [this]( QLineEdit **editOut, const QString &obj,
                            const QString &placeholder, bool save,
                            const QString &filter ) {
    auto *row = new QHBoxLayout;
    auto *edit = new QLineEdit( this );
    edit->setObjectName( obj );
    edit->setPlaceholderText( placeholder );
    *editOut = edit;
    auto *btn = new QPushButton( tr( "浏览…" ), this );
    connect( btn, &QPushButton::clicked, this, [this, edit, save, filter, placeholder]() {
      const QString p = save
                          ? QFileDialog::getSaveFileName( this, placeholder, edit->text(), filter )
                          : QFileDialog::getOpenFileName( this, placeholder, edit->text(), filter );
      if ( !p.isEmpty() )
        edit->setText( p );
    } );
    row->addWidget( edit, 1 );
    row->addWidget( btn );
    return row;
  };

  form->addRow( tr( "输入栅格" ),
                makeBrowse( &m_inputEdit, QStringLiteral( "ppInput" ),
                            tr( "分类/标签栅格" ), false,
                            tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) ) );

  const bool isVectorOut = ( m_algo == Algorithm::Polygonize );
  form->addRow( isVectorOut ? tr( "输出矢量" ) : tr( "输出栅格" ),
                makeBrowse( &m_outputEdit, QStringLiteral( "ppOutput" ),
                            isVectorOut ? tr( "输出 .gpkg / .shp" ) : tr( "输出 GeoTIFF" ),
                            true,
                            isVectorOut
                              ? tr( "GeoPackage (*.gpkg);;ESRI Shapefile (*.shp)" )
                              : tr( "GeoTIFF (*.tif)" ) ) );

  // Algorithm-specific parameters
  switch ( m_algo )
  {
    case Algorithm::Sieve:
      m_sieveSpin = new QSpinBox( this );
      m_sieveSpin->setRange( 1, 1000000 );
      m_sieveSpin->setValue( 10 );
      form->addRow( tr( "面积阈值（像元）" ), m_sieveSpin );
      m_connectSpin = new QSpinBox( this );
      m_connectSpin->setRange( 4, 8 );
      m_connectSpin->setSingleStep( 4 );
      m_connectSpin->setValue( 8 );
      form->addRow( tr( "连通性 (4/8)" ), m_connectSpin );
      break;
    case Algorithm::Majority:
      m_majoritySpin = new QSpinBox( this );
      m_majoritySpin->setRange( 3, 7 );
      m_majoritySpin->setSingleStep( 2 );
      m_majoritySpin->setValue( 3 );
      form->addRow( tr( "核大小（奇数）" ), m_majoritySpin );
      break;
    case Algorithm::Clump:
      m_connectSpin = new QSpinBox( this );
      m_connectSpin->setRange( 4, 8 );
      m_connectSpin->setSingleStep( 4 );
      m_connectSpin->setValue( 8 );
      form->addRow( tr( "连通性 (4/8)" ), m_connectSpin );
      break;
    case Algorithm::Recode:
      m_recodeTable = new QTableWidget( 6, 2, this );
      m_recodeTable->setHorizontalHeaderLabels( { tr( "旧类" ), tr( "新类" ) } );
      m_recodeTable->horizontalHeader()->setStretchLastSection( true );
      m_recodeTable->verticalHeader()->setVisible( false );
      m_recodeTable->setMinimumHeight( 140 );
      form->addRow( tr( "重编码表" ), m_recodeTable );
      break;
    case Algorithm::Polygonize:
      break;
  }

  root->addLayout( form );

  m_loadToLayersCb = new QCheckBox(
    tr( "完成后加载结果到分类窗口图层管理" ), this );
  m_loadToLayersCb->setObjectName( QStringLiteral( "ppLoadToLayers" ) );
  m_loadToLayersCb->setChecked( true ); // default on
  m_loadToLayersCb->setToolTip(
    tr( "默认勾选：结果栅格/矢量加入本分类窗口左侧图层树，而非仅写文件。" ) );
  root->addWidget( m_loadToLayersCb );

  auto *buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
  buttons->button( QDialogButtonBox::Ok )->setText( tr( "运行" ) );
  auto *helpBtn = buttons->addButton( tr( "帮助" ), QDialogButtonBox::HelpRole );
  helpBtn->setToolTip( tr( "打开本对话框的帮助说明。" ) );
  connect( helpBtn, &QPushButton::clicked, this, [this]() {
    SicnuDialogHelp::showToolHelp( this, QStringLiteral( "post_process" ), windowTitle() );
  } );
  connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
  connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );
  root->addWidget( buttons );
}

void RsPostProcessDialog::setDefaultInputPath( const QString &path )
{
  if ( m_inputEdit && !path.isEmpty() )
    m_inputEdit->setText( path );
}

void RsPostProcessDialog::setDefaultOutputPath( const QString &path )
{
  if ( m_outputEdit && !path.isEmpty() )
    m_outputEdit->setText( path );
}

bool RsPostProcessDialog::loadToLayerTree() const
{
  return m_loadToLayersCb ? m_loadToLayersCb->isChecked() : true;
}

QString RsPostProcessDialog::defaultOutputSuffix() const
{
  switch ( m_algo )
  {
    case Algorithm::Sieve:
      return QStringLiteral( "_sieve.tif" );
    case Algorithm::Majority:
      return QStringLiteral( "_majority.tif" );
    case Algorithm::Clump:
      return QStringLiteral( "_clump.tif" );
    case Algorithm::Recode:
      return QStringLiteral( "_recode.tif" );
    case Algorithm::Polygonize:
      return QStringLiteral( "_poly.gpkg" );
  }
  return QStringLiteral( "_post.tif" );
}

QMap<int, int> RsPostProcessDialog::collectRecodeMap() const
{
  QMap<int, int> map;
  if ( !m_recodeTable )
    return map;
  for ( int r = 0; r < m_recodeTable->rowCount(); ++r )
  {
    auto *oldItem = m_recodeTable->item( r, 0 );
    auto *newItem = m_recodeTable->item( r, 1 );
    if ( !oldItem || !newItem )
      continue;
    bool okOld = false;
    bool okNew = false;
    const int oldId = oldItem->text().trimmed().toInt( &okOld );
    const int newId = newItem->text().trimmed().toInt( &okNew );
    if ( okOld && okNew )
      map.insert( oldId, newId );
  }
  return map;
}

bool RsPostProcessDialog::buildConfig( RsPostProcessConfig &cfg, QString *errorMessage ) const
{
  cfg = RsPostProcessConfig{};
  // Only one operator enabled for this dialog
  cfg.runSieve = false;
  cfg.runMajority = false;
  cfg.runClump = false;
  cfg.runRecode = false;
  cfg.runPolygonize = false;

  cfg.inputPath = m_inputEdit ? m_inputEdit->text().trimmed() : QString();
  if ( cfg.inputPath.isEmpty() )
  {
    if ( errorMessage )
      *errorMessage = tr( "请指定输入栅格路径" );
    return false;
  }
  if ( !QFileInfo::exists( cfg.inputPath ) )
  {
    if ( errorMessage )
      *errorMessage = tr( "输入文件不存在: %1" ).arg( cfg.inputPath );
    return false;
  }

  QString out = m_outputEdit ? m_outputEdit->text().trimmed() : QString();
  if ( out.isEmpty() )
  {
    const QFileInfo fi( cfg.inputPath );
    out = fi.absolutePath() + QLatin1Char( '/' ) + fi.completeBaseName()
          + defaultOutputSuffix();
  }

  switch ( m_algo )
  {
    case Algorithm::Sieve:
      cfg.runSieve = true;
      cfg.sieveThreshold = m_sieveSpin ? m_sieveSpin->value() : 10;
      cfg.connectedness = m_connectSpin ? m_connectSpin->value() : 8;
      if ( cfg.connectedness != 4 && cfg.connectedness != 8 )
        cfg.connectedness = 8;
      cfg.outputRasterPath = out;
      break;
    case Algorithm::Majority:
    {
      cfg.runMajority = true;
      int k = m_majoritySpin ? m_majoritySpin->value() : 3;
      if ( k % 2 == 0 )
        ++k;
      cfg.majorityKernel = k;
      cfg.outputRasterPath = out;
      break;
    }
    case Algorithm::Clump:
      cfg.runClump = true;
      cfg.connectedness = m_connectSpin ? m_connectSpin->value() : 8;
      if ( cfg.connectedness != 4 && cfg.connectedness != 8 )
        cfg.connectedness = 8;
      cfg.outputRasterPath = out;
      break;
    case Algorithm::Recode:
      cfg.runRecode = true;
      cfg.recodeMap = collectRecodeMap();
      if ( cfg.recodeMap.isEmpty() )
      {
        if ( errorMessage )
          *errorMessage = tr( "请在重编码表中至少填写一行旧类→新类" );
        return false;
      }
      cfg.outputRasterPath = out;
      break;
    case Algorithm::Polygonize:
      cfg.runPolygonize = true;
      // Polygonize needs an intermediate: task saves raster then polygonizes.
      // Use input as "processed" labels path: load → save copy? Looking at task:
      // it runs operators then save then polygonize from output raster.
      // For polygonize-only, we need save of input (or identity) then polygonize.
      // RsPostProcessTask: load → ops → save → polygonize from output raster.
      // So set output raster to a temp-like path next to vector, then polygonize.
      cfg.outputVectorPath = out;
      {
        const QFileInfo fi( out );
        cfg.outputRasterPath = fi.absolutePath() + QLatin1Char( '/' )
                               + fi.completeBaseName() + QStringLiteral( "_labels.tif" );
      }
      // No filter ops: need at least save. Task requires one of the flags;
      // polygonize alone is OK in task if runPolygonize - check task.
      break;
  }

  return true;
}
