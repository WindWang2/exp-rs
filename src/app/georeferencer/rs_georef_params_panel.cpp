#include "rs_georef_params_panel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

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
    };
    for ( const auto &m : methods )
      mTransformCombo->addItem( m.second, QVariant::fromValue( static_cast<int>( m.first ) ) );

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

    vbox->addItem( grid );
    sec->layout()->addItem( vbox );
    root->addWidget( sec );
  }

  // ---- Section 4: 坐标系 ----
  {
    QFrame *sec = makeSectionFrame( tr( "坐标系" ), this );
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );

    mSrcCrsLabel = new QLabel( tr( "—" ), sec );
    mDstCrsLabel = new QLabel( tr( "EPSG:32650" ), sec );
    mProjNameLabel = new QLabel( tr( "WGS 84 / UTM zone 50N" ), sec );
    mSrcCrsLabel->setObjectName( QStringLiteral( "rsSrcCrsLabel" ) );
    mDstCrsLabel->setObjectName( QStringLiteral( "rsDstCrsLabel" ) );
    mProjNameLabel->setObjectName( QStringLiteral( "rsProjNameLabel" ) );

    form->addRow( tr( "源 CRS" ), mSrcCrsLabel );
    form->addRow( tr( "目标 CRS" ), mDstCrsLabel );
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

QgsCoordinateReferenceSystem RsGeorefParamsPanel::destCrs() const
{
  // Task 7: return a fixed test CRS until a real picker is wired (Task 8+).
  return QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:32650" ) );
}

double RsGeorefParamsPanel::outputPixelSize() const
{
  return mPixelSize->value();
}

bool RsGeorefParamsPanel::isDemSectionVisible() const
{
  // Task 7 stub: DEM section is added in Task 8.
  return false;
}

QString RsGeorefParamsPanel::demPath() const
{
  return QString();
}

void RsGeorefParamsPanel::setRpcMode( bool /*on*/ )
{
  // Task 7 stub. Task 8 adds the RPC transform method to the combo and the
  // DEM section; here we only need to accept the call without effect.
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
  mOutputPath->setText( path );
}
