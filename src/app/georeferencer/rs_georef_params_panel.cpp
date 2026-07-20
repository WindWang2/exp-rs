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
  QFrame *makeSectionFrame( const QString &title, QWidget *parent )
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
    lay->addWidget( header );
    return frame;
  }
}

RsGeorefParamsPanel::RsGeorefParamsPanel( QWidget *parent )
  : QWidget( parent )
{
  setObjectName( QStringLiteral( "rsGeorefParamsPanel" ) );
  setMinimumWidth( 320 );

  auto *root = new QVBoxLayout( this );
  root->setContentsMargins( 6, 6, 6, 6 );
  root->setSpacing( 8 );

  // ---- Section 1: 坐标变换 ----
  {
    QFrame *sec = makeSectionFrame( tr( "坐标变换" ), this );
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

    form->addRow( tr( "方法" ), mTransformCombo );

    mMinPtsLabel = new QLabel( tr( "—" ), sec );
    mActualPtsLabel = new QLabel( tr( "—" ), sec );
    mDofLabel = new QLabel( tr( "—" ), sec );
    mMinPtsLabel->setObjectName( QStringLiteral( "rsMinPtsLabel" ) );
    mActualPtsLabel->setObjectName( QStringLiteral( "rsActualPtsLabel" ) );
    mDofLabel->setObjectName( QStringLiteral( "rsDofLabel" ) );

    form->addRow( tr( "最少点数" ), mMinPtsLabel );
    form->addRow( tr( "实际可用点数" ), mActualPtsLabel );
    form->addRow( tr( "自由度 DOF" ), mDofLabel );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 2: 重采样 ----
  {
    QFrame *sec = makeSectionFrame( tr( "重采样" ), this );
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
    form->addRow( tr( "算法" ), mResamplingCombo );

    mPixelSize = new QDoubleSpinBox( sec );
    mPixelSize->setObjectName( QStringLiteral( "rsPixelSize" ) );
    mPixelSize->setRange( 0.0, 1.0e9 );
    mPixelSize->setDecimals( 6 );
    mPixelSize->setValue( 0.0 );
    mPixelSize->setSpecialValueText( tr( "auto" ) );
    form->addRow( tr( "输出像元大小" ), mPixelSize );

    mOutputExtent = new QLineEdit( sec );
    mOutputExtent->setObjectName( QStringLiteral( "rsOutputExtent" ) );
    mOutputExtent->setReadOnly( true );
    mOutputExtent->setText( tr( "auto · ref" ) );
    form->addRow( tr( "输出范围" ), mOutputExtent );

    mBackground = new QSpinBox( sec );
    mBackground->setObjectName( QStringLiteral( "rsBackground" ) );
    mBackground->setRange( 0, 65535 );
    mBackground->setValue( 0 );
    form->addRow( tr( "背景值" ), mBackground );

    sec->layout()->addItem( form );
    root->addWidget( sec );
  }

  // ---- Section 3: RMS 误差分布 ----
  {
    QFrame *sec = makeSectionFrame( tr( "RMS 误差分布" ), this );
    auto *vbox = new QVBoxLayout();
    vbox->setContentsMargins( 0, 0, 0, 0 );

    mScatter = new RsRmsScatterWidget( sec );
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

    vbox->addItem( grid );
    vbox->addWidget( mRmsBefore );
    vbox->addWidget( mRmsAfter );
    sec->layout()->addItem( vbox );
    root->addWidget( sec );
  }

  // ---- Section 4: 坐标系 ----
  {
    QFrame *sec = makeSectionFrame( tr( "坐标系" ), this );
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mSrcCrsLabel = new QLabel( tr( "—" ), sec );
    mSrcCrsLabel->setObjectName( QStringLiteral( "rsSrcCrsLabel" ) );

    // Task 11.5.1 — real CRS picker replaces the hard-coded EPSG:32650 label.
    mCrsWidget = new QgsProjectionSelectionWidget( sec );
    mCrsWidget->setObjectName( QStringLiteral( "rsCrsWidget" ) );

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
    QFrame *sec = makeSectionFrame( tr( "输出" ), this );
    auto *row = new QHBoxLayout();
    row->setContentsMargins( 0, 0, 0, 0 );

    mOutputPath = new QLineEdit( sec );
    mOutputPath->setObjectName( QStringLiteral( "rsOutputPath" ) );
    mOutputPath->setPlaceholderText( tr( "/path/to/output.tif" ) );
    connect( mOutputPath, &QLineEdit::textChanged,
             this, [this]( const QString &s ) { emit outputPathChanged( s ); } );

    mBrowseBtn = new QPushButton( tr( "Browse…" ), sec );
    mBrowseBtn->setObjectName( QStringLiteral( "rsBrowseOutputBtn" ) );
    connect( mBrowseBtn, &QPushButton::clicked, this, &RsGeorefParamsPanel::onBrowseOutput );

    row->addWidget( mOutputPath, 1 );
    row->addWidget( mBrowseBtn );
    sec->layout()->addItem( row );
    root->addWidget( sec );
  }

  // ---- Section 6: DEM (RPC mode only) ----
  {
    mDemSection = makeSectionFrame( tr( "DEM (RPC 模式)" ), this );
    mDemSection->setObjectName( QStringLiteral( "rsDemSection" ) );

    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    auto *demRow = new QHBoxLayout();
    demRow->setContentsMargins( 0, 0, 0, 0 );
    mDemPath = new QLineEdit( mDemSection );
    mDemPath->setObjectName( QStringLiteral( "rsDemPath" ) );
    mDemPath->setPlaceholderText( tr( "/path/to/dem.tif (可选)" ) );
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
    form->addRow( tr( "高程偏移" ), mDemZOffset );

    mDemSection->layout()->addItem( form );
    mDemSection->setVisible( false );
    root->addWidget( mDemSection );
  }

  // Hide the RPC entry from the combo until the user enters RPC mode.
  if ( auto *view = qobject_cast<QListView *>( mTransformCombo->view() ) )
  {
    const int rpcIdx = mTransformCombo->findData(
      QVariant::fromValue(
        static_cast<int>( QgsGcpTransformerInterface::TransformMethod::RpcPhysical ) ) );
    if ( rpcIdx >= 0 )
      view->setRowHidden( rpcIdx, true );
  }

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

void RsGeorefParamsPanel::setRpcMode( bool on )
{
  if ( mDemSection )
    mDemSection->setVisible( on );

  if ( !mTransformCombo )
    return;

  // Hide all non-RPC rows when on; hide the RPC row when off.
  auto *view = qobject_cast<QListView *>( mTransformCombo->view() );
  for ( int i = 0; i < mTransformCombo->count(); ++i )
  {
    const auto m = static_cast<QgsGcpTransformerInterface::TransformMethod>(
      mTransformCombo->itemData( i ).toInt() );
    const bool isRpc = ( m == QgsGcpTransformerInterface::TransformMethod::RpcPhysical );
    const bool shouldHide = ( on != isRpc );
    if ( view )
      view->setRowHidden( i, shouldHide );
  }

  if ( on )
  {
    const int rpcIdx = mTransformCombo->findData(
      QVariant::fromValue(
        static_cast<int>( QgsGcpTransformerInterface::TransformMethod::RpcPhysical ) ) );
    if ( rpcIdx >= 0 )
      mTransformCombo->setCurrentIndex( rpcIdx );
  }
  else
  {
    // Switch back to the first non-RPC item (Linear) when leaving RPC mode.
    const auto current = static_cast<QgsGcpTransformerInterface::TransformMethod>(
      mTransformCombo->currentData().toInt() );
    if ( current == QgsGcpTransformerInterface::TransformMethod::RpcPhysical )
    {
      const int linIdx = mTransformCombo->findData(
        QVariant::fromValue(
          static_cast<int>( QgsGcpTransformerInterface::TransformMethod::Linear ) ) );
      if ( linIdx >= 0 )
        mTransformCombo->setCurrentIndex( linIdx );
    }
  }
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
