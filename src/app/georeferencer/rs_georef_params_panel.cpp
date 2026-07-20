#include "rs_georef_params_panel.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "qgsprojectionselectionwidget.h"
#include "rs_rms_scatter_widget.h"

namespace
{
  void setHelp( QWidget *w, const QString &tip )
  {
    if ( !w )
      return;
    w->setToolTip( tip );
    w->setWhatsThis( tip );
    w->setStatusTip( tip );
  }

  QFrame *makeSectionFrame( const QString &title, QWidget *parent, const QString &sectionTip = {} )
  {
    auto *frame = new QFrame( parent );
    frame->setObjectName( QStringLiteral( "rsParamSection" ) );
    frame->setFrameShape( QFrame::StyledPanel );
    auto *lay = new QVBoxLayout( frame );
    lay->setContentsMargins( 8, 6, 8, 8 );
    lay->setSpacing( 6 );
    auto *header = new QLabel( title, frame );
    header->setObjectName( QStringLiteral( "rsParamSectionTitle" ) );
    QFont f = header->font();
    f.setBold( true );
    header->setFont( f );
    if ( !sectionTip.isEmpty() )
    {
      header->setToolTip( sectionTip );
      header->setWhatsThis( sectionTip );
      frame->setToolTip( sectionTip );
      frame->setWhatsThis( sectionTip );
    }
    lay->addWidget( header );
    return frame;
  }
}

RsGeorefParamsPanel::RsGeorefParamsPanel( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsGeorefParamsPanel" ) );
  setMinimumWidth( 320 );
  setHelp( this, tr(
    "校正参数面板：选择变换方法、重采样与输出路径，并查看 GCP 拟合残差。"
    "设置完成后点工具栏「运行」加入任务列表。" ) );

  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 6, 6, 6, 6 );
  root->setSpacing( 8 );

  // ---- Section 1: 坐标变换 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "坐标变换" ), this,
      tr( "根据 GCP 拟合源像素到目标坐标的几何模型。点数不足时「运行」会禁用。" ) );
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mTransformCombo = new QComboBox( sec );
    mTransformCombo->setObjectName( QStringLiteral( "rsTransformCombo" ) );
    using TM = QgsGcpTransformerInterface::TransformMethod;
    const QVector<QPair<TM, QString>> methods = {
      { TM::Linear, tr( "Linear (线性)" ) },
      { TM::Helmert, tr( "Helmert" ) },
      { TM::PolynomialOrder1, tr( "Polynomial Order 1 (一次多项式)" ) },
      { TM::PolynomialOrder2, tr( "Polynomial Order 2 (二次多项式)" ) },
      { TM::PolynomialOrder3, tr( "Polynomial Order 3 (三次多项式)" ) },
      { TM::ThinPlateSpline, tr( "Thin Plate Spline (薄板样条)" ) },
      { TM::Projective, tr( "Projective (透视)" ) },
      { TM::RpcPhysical, tr( "RPC Physical (RFM 物理模型)" ) },
    };
    for ( const auto &m : methods )
      mTransformCombo->addItem( m.second, QVariant::fromValue( static_cast<int>( m.first ) ) );

    // Ensure the combo view is a QListView so we can hide individual rows
    // when toggling RPC mode (see setRpcMode()).
    mTransformCombo->setView( new QListView( mTransformCombo ) );

    connect( mTransformCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, [this]( int ) { emit transformMethodChanged(); } );

    setHelp( mTransformCombo, tr(
      "变换方法：\n"
      "• Linear / Helmert：全局相似/线性，点数少时可用\n"
      "• 多项式 1–3：常见影像配准，阶数越高越灵活但易过拟合\n"
      "• TPS：局部变形强，适合复杂畸变\n"
      "• Projective：透视/扫描图\n"
      "• RPC Physical：仅 Image→Map，依赖源影像 RPC 元数据与可选 DEM" ) );
    form->addRow( tr( "方法" ), mTransformCombo );

    mMinPtsLabel = new QLabel( tr( "—" ), sec );
    mActualPtsLabel = new QLabel( tr( "—" ), sec );
    mDofLabel = new QLabel( tr( "—" ), sec );
    mMinPtsLabel->setObjectName( QStringLiteral( "rsMinPtsLabel" ) );
    mActualPtsLabel->setObjectName( QStringLiteral( "rsActualPtsLabel" ) );
    mDofLabel->setObjectName( QStringLiteral( "rsDofLabel" ) );
    setHelp( mMinPtsLabel, tr( "当前方法要求的最少启用 GCP 数。" ) );
    setHelp( mActualPtsLabel, tr( "当前已启用、参与拟合的 GCP 数量。" ) );
    setHelp( mDofLabel, tr( "自由度 = 实际点数 − 最少点数。>0 时可用残差评估精度。" ) );

    form->addRow( tr( "最少点数" ), mMinPtsLabel );
    form->addRow( tr( "实际可用点数" ), mActualPtsLabel );
    form->addRow( tr( "自由度 DOF" ), mDofLabel );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 2: 重采样 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "重采样" ), this,
      tr( "写出校正结果栅格时的像元插值方式与输出分辨率。" ) );
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mResamplingCombo = new QComboBox( sec );
    mResamplingCombo->setObjectName( QStringLiteral( "rsResamplingCombo" ) );
    using RM = QgsImageWarper::ResamplingMethod;
    mResamplingCombo->addItem( tr( "Nearest Neighbour" ), QVariant::fromValue( static_cast<int>( RM::NearestNeighbour ) ) );
    mResamplingCombo->addItem( tr( "Bilinear" ), QVariant::fromValue( static_cast<int>( RM::Bilinear ) ) );
    mResamplingCombo->addItem( tr( "Cubic" ), QVariant::fromValue( static_cast<int>( RM::Cubic ) ) );
    mResamplingCombo->addItem( tr( "Cubic Spline" ), QVariant::fromValue( static_cast<int>( RM::CubicSpline ) ) );
    mResamplingCombo->addItem( tr( "Lanczos" ), QVariant::fromValue( static_cast<int>( RM::Lanczos ) ) );
    connect( mResamplingCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
             this, [this]( int ) { emit resamplingMethodChanged(); } );
    setHelp( mResamplingCombo, tr(
      "重采样算法：\n"
      "• Nearest：保持类别/整数值，分类图适用\n"
      "• Bilinear：连续影像常用\n"
      "• Cubic / Lanczos：更平滑，计算稍慢" ) );
    form->addRow( tr( "算法" ), mResamplingCombo );

    mPixelSize = new QDoubleSpinBox( sec );
    mPixelSize->setObjectName( QStringLiteral( "rsPixelSize" ) );
    mPixelSize->setRange( 0.0, 1.0e9 );
    mPixelSize->setDecimals( 6 );
    mPixelSize->setValue( 0.0 );
    mPixelSize->setSpecialValueText( tr( "auto" ) );
    setHelp( mPixelSize, tr( "输出像元大小（目标 CRS 单位）。auto 表示由引擎根据输入自动估计。" ) );
    form->addRow( tr( "输出像元大小" ), mPixelSize );

    mOutputExtent = new QLineEdit( sec );
    mOutputExtent->setObjectName( QStringLiteral( "rsOutputExtent" ) );
    mOutputExtent->setReadOnly( true );
    mOutputExtent->setText( tr( "auto · ref" ) );
    setHelp( mOutputExtent, tr( "输出地理范围（只读）。当前为自动/参考范围策略。" ) );
    form->addRow( tr( "输出范围" ), mOutputExtent );

    mBackground = new QSpinBox( sec );
    mBackground->setObjectName( QStringLiteral( "rsBackground" ) );
    mBackground->setRange( 0, 65535 );
    mBackground->setValue( 0 );
    setHelp( mBackground, tr( "无数据覆盖区域填充的背景像元值。" ) );
    form->addRow( tr( "背景值" ), mBackground );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 3: RMS 误差分布 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "RMS 误差分布" ), this,
      tr( "显示当前拟合残差。目标：总 RMS 与最大残差尽量小且点分布均匀。" ) );
    auto *vbox = new QVBoxLayout();
    vbox->setContentsMargins( 0, 0, 0, 0 );

    mScatter = new RsRmsScatterWidget( sec );
    setHelp( mScatter, tr( "残差散点图：横纵为 X/Y 残差。离群点可禁用或重新取点。" ) );
    vbox->addWidget( mScatter, 0, Qt::AlignHCenter );

    auto *grid = new QFormLayout();
    grid->setContentsMargins( 0, 0, 0, 0 );
    mXRms = new QLabel( tr( "—" ), sec );
    mYRms = new QLabel( tr( "—" ), sec );
    mTotalRms = new QLabel( tr( "—" ), sec );
    mMaxRms = new QLabel( tr( "—" ), sec );
    mXRms->setObjectName( QStringLiteral( "rsXRmsLabel" ) );
    mYRms->setObjectName( QStringLiteral( "rsYRmsLabel" ) );
    mTotalRms->setObjectName( QStringLiteral( "rsTotalRmsLabel" ) );
    mMaxRms->setObjectName( QStringLiteral( "rsMaxRmsLabel" ) );
    setHelp( mTotalRms, tr( "启用 GCP 残差均方根（像素或目标单位，视拟合路径而定）。" ) );
    setHelp( mMaxRms, tr( "残差最大的点，优先检查该 GCP 是否取错。" ) );
    grid->addRow( tr( "X RMS" ), mXRms );
    grid->addRow( tr( "Y RMS" ), mYRms );
    grid->addRow( tr( "Total RMS" ), mTotalRms );
    grid->addRow( tr( "最大残差" ), mMaxRms );

    // Task 11.5.5 — before/after RMS readout for RPC linear-bias refinement.
    // Empty until the main window calls setRefinementRms().
    mRmsBefore = new QLabel( QString(), sec );
    mRmsBefore->setObjectName( QStringLiteral( "rsRmsBefore" ) );
    mRmsAfter = new QLabel( QString(), sec );
    mRmsAfter->setObjectName( QStringLiteral( "rsRmsAfter" ) );
    setHelp( mRmsBefore, tr( "RPC 精化前 RMS（有 ≥3 个 GCP 时显示）。" ) );
    setHelp( mRmsAfter, tr( "RPC 线性偏差精化后 RMS；绿字表示精化改善。" ) );

    vbox->addItem( grid );
    vbox->addWidget( mRmsBefore );
    vbox->addWidget( mRmsAfter );
    sec->layout()->addItem( vbox );
    root->addWidget( sec );
  }

  // ---- Section 4: 坐标系 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "坐标系" ), this,
      tr( "目标坐标系用于拟合与写出 GeoTIFF。I2M 下 Map 画布会跟随目标 CRS。" ) );
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mSrcCrsLabel = new QLabel( tr( "—" ), sec );
    mSrcCrsLabel->setObjectName( QStringLiteral( "rsSrcCrsLabel" ) );
    setHelp( mSrcCrsLabel, tr( "源影像坐标系（若已定义）。" ) );

    // Task 11.5.1 — real CRS picker replaces the hard-coded EPSG:32650 label.
    mCrsWidget = new QgsProjectionSelectionWidget( sec );
    mCrsWidget->setObjectName( QStringLiteral( "rsCrsWidget" ) );
    setHelp( mCrsWidget, tr( "选择校正结果的目标 CRS（如 UTM / 国家 2000）。" ) );

    // Restore last user choice (default to EPSG:32650 to preserve previous
    // behaviour from Task 11.4 when no setting exists yet).
    {
      const QString lastAuthid = QSettings()
                                   .value( QStringLiteral( "Georeferencer/lastDestCrs" ),
                                           QStringLiteral( "EPSG:32650" ) )
                                   .toString();
      QgsCoordinateReferenceSystem saved;
      if ( !lastAuthid.isEmpty() )
        saved.createFromOgcWmsCrs( lastAuthid );
      if ( saved.isValid() )
        mCrsWidget->setCrs( saved );
    }

    connect( mCrsWidget, &QgsProjectionSelectionWidget::crsChanged, this,
             [this]( const QgsCoordinateReferenceSystem &crs ) {
               QSettings().setValue( QStringLiteral( "Georeferencer/lastDestCrs" ),
                                     crs.authid() );
               if ( mProjNameLabel )
                 mProjNameLabel->setText( crs.description().isEmpty() ? crs.authid()
                                                                     : crs.description() );
               emit destCrsChanged();
             } );

    mProjNameLabel = new QLabel( sec );
    mProjNameLabel->setObjectName( QStringLiteral( "rsProjNameLabel" ) );
    {
      const QgsCoordinateReferenceSystem cur = mCrsWidget->crs();
      mProjNameLabel->setText( cur.description().isEmpty() ? cur.authid() : cur.description() );
    }

    form->addRow( tr( "源 CRS" ), mSrcCrsLabel );
    form->addRow( tr( "目标 CRS" ), mCrsWidget );
    form->addRow( tr( "投影名" ), mProjNameLabel );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 5: 输出 ----
  {
    QFrame *sec = makeSectionFrame(
      tr( "输出" ), this,
      tr( "校正结果 GeoTIFF 路径。必须填写后才能「运行」。" ) );
    auto *row = new QHBoxLayout();
    row->setContentsMargins( 0, 0, 0, 0 );

    mOutputPath = new QLineEdit( sec );
    mOutputPath->setObjectName( QStringLiteral( "rsOutputPath" ) );
    mOutputPath->setPlaceholderText( tr( "/path/to/output.tif" ) );
    connect( mOutputPath, &QLineEdit::textChanged,
             this, [this]( const QString &s ) { emit outputPathChanged( s ); } );
    setHelp( mOutputPath, tr( "输出文件完整路径（建议 .tif）。任务列表会记录此路径。" ) );

    mBrowseBtn = new QPushButton( tr( "Browse…" ), sec );
    mBrowseBtn->setObjectName( QStringLiteral( "rsBrowseOutputBtn" ) );
    connect( mBrowseBtn, &QPushButton::clicked, this, &RsGeorefParamsPanel::onBrowseOutput );
    setHelp( mBrowseBtn, tr( "浏览选择输出文件位置。" ) );

    row->addWidget( mOutputPath, 1 );
    row->addWidget( mBrowseBtn );
    sec->layout()->addItem( row );
    root->addWidget( sec );
  }

  // ---- Section 6: DEM (RPC mode only) ----
  {
    mDemSection = makeSectionFrame(
      tr( "DEM (RPC 模式)" ), this,
      tr( "仅 RPC Physical 方法显示。可选 DEM 与高程偏移改善正射/投影精度。" ) );
    mDemSection->setObjectName( QStringLiteral( "rsDemSection" ) );

    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    auto *demRow = new QHBoxLayout();
    demRow->setContentsMargins( 0, 0, 0, 0 );
    mDemPath = new QLineEdit( mDemSection );
    mDemPath->setObjectName( QStringLiteral( "rsDemPath" ) );
    mDemPath->setPlaceholderText( tr( "/path/to/dem.tif (可选)" ) );
    setHelp( mDemPath, tr( "数字高程模型路径（可选）。用于 RPC 高度相关投影。" ) );
    mDemBrowseBtn = new QPushButton( tr( "Browse…" ), mDemSection );
    mDemBrowseBtn->setObjectName( QStringLiteral( "rsDemBrowseBtn" ) );
    connect( mDemBrowseBtn, &QPushButton::clicked, this, [this]() {
      const QString path = QFileDialog::getOpenFileName(
        this,
        tr( "选择 DEM 文件" ),
        mDemPath->text(),
        tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) );
      if ( !path.isEmpty() )
        setDemPath( path );
    } );
    setHelp( mDemBrowseBtn, tr( "选择 DEM 栅格文件。" ) );
    demRow->addWidget( mDemPath, 1 );
    demRow->addWidget( mDemBrowseBtn );
    form->addRow( tr( "DEM 路径" ), demRow );

    mDemZOffset = new QDoubleSpinBox( mDemSection );
    mDemZOffset->setObjectName( QStringLiteral( "rsDemZOffset" ) );
    mDemZOffset->setRange( -10000.0, 10000.0 );
    mDemZOffset->setDecimals( 2 );
    mDemZOffset->setValue( 0.0 );
    mDemZOffset->setSuffix( tr( " m" ) );
    // Task 11.5.4 — propagate spin-box edits so the main window can
    // recompute the RPC fit (and the warp pipeline picks up RPC_HEIGHT).
    connect( mDemZOffset, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
             this, [this]( double ) { emit demZOffsetChanged(); } );
    setHelp( mDemZOffset, tr( "相对 DEM 的高程偏移（米），传入 RPC_HEIGHT 选项。" ) );
    form->addRow( tr( "高程偏移" ), mDemZOffset );

    mDemSection->layout()->addItem( form );
    mDemSection->setVisible( false );
    root->addWidget( mDemSection );
  }

  // Default profile is ImageToImage — hide RPC until I2M setProfile().
  applyProfileToMethodCombo();

  root->addStretch( 1 );
}

QgsGcpTransformerInterface::TransformMethod RsGeorefParamsPanel::transformMethod() const
{
  const int v = mTransformCombo->currentData().toInt();
  return static_cast<QgsGcpTransformerInterface::TransformMethod>( v );
}

QgsImageWarper::ResamplingMethod RsGeorefParamsPanel::resamplingMethod() const
{
  const int v = mResamplingCombo->currentData().toInt();
  return static_cast<QgsImageWarper::ResamplingMethod>( v );
}

QString RsGeorefParamsPanel::outputPath() const
{
  return mOutputPath->text().trimmed();
}

void RsGeorefParamsPanel::setTransformMethod( QgsGcpTransformerInterface::TransformMethod m )
{
  if ( !mTransformCombo )
    return;
  const int idx = mTransformCombo->findData( QVariant::fromValue( static_cast<int>( m ) ) );
  if ( idx >= 0 )
    mTransformCombo->setCurrentIndex( idx );
}

void RsGeorefParamsPanel::setResamplingMethod( QgsImageWarper::ResamplingMethod m )
{
  if ( !mResamplingCombo )
    return;
  const int idx = mResamplingCombo->findData( QVariant::fromValue( static_cast<int>( m ) ) );
  if ( idx >= 0 )
    mResamplingCombo->setCurrentIndex( idx );
}

void RsGeorefParamsPanel::setOutputPath( const QString &path )
{
  if ( mOutputPath )
    mOutputPath->setText( path );
}

void RsGeorefParamsPanel::setDemPath( const QString &path )
{
  if ( mDemPath )
    mDemPath->setText( path );
}

QgsCoordinateReferenceSystem RsGeorefParamsPanel::destCrs() const
{
  // Task 11.5.1 — picker is the source of truth. Fall back to EPSG:32650 if
  // the widget was never constructed (defensive — shouldn't happen).
  if ( mCrsWidget )
    return mCrsWidget->crs();
  return QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:32650" ) );
}

void RsGeorefParamsPanel::setDestCrs( const QgsCoordinateReferenceSystem &crs )
{
  if ( !mCrsWidget )
    return;
  // QgsProjectionSelectionWidget emits crsChanged() when its current CRS
  // actually changes; our slot persists + emits destCrsChanged(). If the
  // incoming CRS equals the current one no signal would fire, so we fan it
  // out unconditionally here to honour the "setter triggers signal" contract
  // the test (and main-window recomputeFit wiring) depends on.
  const bool sameCrs = ( mCrsWidget->crs() == crs );
  mCrsWidget->setCrs( crs );
  if ( sameCrs )
  {
    QSettings().setValue( QStringLiteral( "Georeferencer/lastDestCrs" ), crs.authid() );
    if ( mProjNameLabel )
      mProjNameLabel->setText( crs.description().isEmpty() ? crs.authid() : crs.description() );
    emit destCrsChanged();
  }
}

double RsGeorefParamsPanel::outputPixelSize() const
{
  return mPixelSize->value();
}

bool RsGeorefParamsPanel::isDemSectionVisible() const
{
  // Use !isHidden() rather than isVisible(): the latter is false when the
  // window has not yet been shown, but the DEM-section's own visibility
  // intent is independently observable.  Tests construct the window without
  // showing it, so we report the local (unrealized) visibility state.
  return mDemSection && !mDemSection->isHidden();
}

QString RsGeorefParamsPanel::demPath() const
{
  return mDemPath ? mDemPath->text().trimmed() : QString();
}

double RsGeorefParamsPanel::demZOffset() const
{
  return mDemZOffset ? mDemZOffset->value() : 0.0;
}

void RsGeorefParamsPanel::setDemZOffset( double z )
{
  if ( mDemZOffset )
    mDemZOffset->setValue( z );
}

void RsGeorefParamsPanel::setProfile( Profile p )
{
  mProfile = p;
  applyProfileToMethodCombo();
  if ( mProfile == Profile::ImageToImage )
  {
    setRpcMode( false );
  }
  else
  {
    setRpcMode( transformMethod() ==
                QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
  }
}

void RsGeorefParamsPanel::applyProfileToMethodCombo()
{
  if ( !mTransformCombo )
    return;

  auto *view = qobject_cast<QListView *>( mTransformCombo->view() );
  const int rpcIdx = mTransformCombo->findData(
    QVariant::fromValue(
      static_cast<int>( QgsGcpTransformerInterface::TransformMethod::RpcPhysical ) ) );

  // I2I: hide RPC row. I2M: show every method (including RPC).
  for ( int i = 0; i < mTransformCombo->count(); ++i )
  {
    const auto m = static_cast<QgsGcpTransformerInterface::TransformMethod>(
      mTransformCombo->itemData( i ).toInt() );
    const bool isRpc = ( m == QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
    const bool hide = ( mProfile == Profile::ImageToImage && isRpc );
    if ( view )
      view->setRowHidden( i, hide );
  }

  if ( mProfile == Profile::ImageToImage
       && transformMethod() == QgsGcpTransformerInterface::TransformMethod::RpcPhysical )
  {
    const int linIdx = mTransformCombo->findData(
      QVariant::fromValue(
        static_cast<int>( QgsGcpTransformerInterface::TransformMethod::Linear ) ) );
    if ( linIdx >= 0 )
      mTransformCombo->setCurrentIndex( linIdx );
  }

  Q_UNUSED( rpcIdx )
}

void RsGeorefParamsPanel::setRpcMode( bool on )
{
  // DEM section only — method combo is controlled by setProfile().
  if ( mDemSection )
    mDemSection->setVisible( on );
}

void RsGeorefParamsPanel::setRmsValues( int /*total*/, int /*enabled*/,
                                        double rmsPx, double xRms, double yRms,
                                        double maxRms, int maxRmsRowId )
{
  auto fmt = []( double v ) { return QString::number( v, 'f', 3 ); };
  mXRms->setText( fmt( xRms ) + tr( " px" ) );
  mYRms->setText( fmt( yRms ) + tr( " px" ) );
  mTotalRms->setText( fmt( rmsPx ) + tr( " px" ) );
  if ( maxRmsRowId >= 0 )
    mMaxRms->setText( tr( "%1 px (行 #%2)" ).arg( fmt( maxRms ) ).arg( maxRmsRowId + 1 ) );
  else
    mMaxRms->setText( fmt( maxRms ) + tr( " px" ) );
}

void RsGeorefParamsPanel::setResidualScatter( const QVector<QPointF> &dxdy )
{
  if ( mScatter )
    mScatter->setResiduals( dxdy );
}

void RsGeorefParamsPanel::setRefinementRms( double before, double after )
{
  if ( mRmsBefore )
    mRmsBefore->setText( tr( "精化前 RMS: %1 px" ).arg( before, 0, 'f', 3 ) );
  if ( mRmsAfter )
  {
    mRmsAfter->setText( tr( "精化后 RMS: %1 px" ).arg( after, 0, 'f', 3 ) );
    mRmsAfter->setStyleSheet( after < before
                                ? QStringLiteral( "color: #208830;" )
                                : QStringLiteral( "color: #5f6b7a;" ) );
  }
}

void RsGeorefParamsPanel::clearRefinementRms()
{
  if ( mRmsBefore )
    mRmsBefore->setText( tr( "精化前 RMS: —" ) );
  if ( mRmsAfter )
  {
    mRmsAfter->setText( tr( "精化后 RMS: —" ) );
    mRmsAfter->setStyleSheet( QString() );
  }
}

void RsGeorefParamsPanel::setMinimumGcpCount( int n )
{
  mMinPtsLabel->setText( QString::number( n ) );
}

void RsGeorefParamsPanel::setActualGcpCount( int n )
{
  mActualPtsLabel->setText( QString::number( n ) );

  // Update DOF: actual - min for current method. If actual < min, DOF = 0.
  bool ok = false;
  const int minN = mMinPtsLabel->text().toInt( &ok );
  if ( ok )
  {
    const int dof = std::max( 0, n - minN );
    mDofLabel->setText( QString::number( dof ) );
  }
}

void RsGeorefParamsPanel::onBrowseOutput()
{
  const QString path = QFileDialog::getSaveFileName(
    this,
    tr( "选择输出 GeoTIFF" ),
    mOutputPath->text(),
    tr( "GeoTIFF (*.tif *.tiff);;All files (*)" ) );
  if ( path.isEmpty() )
    return;
  setOutputPath( path );
}
